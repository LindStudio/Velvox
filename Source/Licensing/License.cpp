#include "License.h"
#include "../Text/I18n.h"

namespace vocalypse::licensing
{
    using i18n::tr;

    namespace
    {
        constexpr juce::int64 dayMs = 24LL * 60 * 60 * 1000;

        juce::var jsonBody (std::initializer_list<std::pair<const char*, juce::String>> fields)
        {
            auto* o = new juce::DynamicObject();
            for (const auto& [name, value] : fields)
                o->setProperty (name, value);
            return juce::var (o);
        }

        juce::String shortKey (const juce::String& key)
        {
            return key.length() > 8 ? key.substring (0, 4) + juce::String::fromUTF8 ("\xe2\x80\xa6") + key.getLastCharacters (4) : key;
        }

        juce::String messageFor (const Reply& r)
        {
            if (r.http >= 500)
                return tr ("Der Lizenz-Server antwortet gerade nicht. Versuch es in ein paar Minuten noch einmal.",
                           "The licence server is not responding right now. Try again in a few minutes.");
            if (! r.reached)
                return tr ("Keine Verbindung zum Lizenz-Server. Prüf die Internetverbindung und versuch es noch einmal.",
                           "No connection to the licence server. Check the internet connection and try again.");
            if (r.error == "key of another product")
                return tr ("Dieser Schlüssel gehört zu einem anderen Produkt. Über „Gratis-Schlüssel holen“ bekommst du einen Schlüssel für EMBER.",
                           "This key belongs to another product. Use “Get free key” to get a key for EMBER.");
            if (r.http == 403 && r.detail.containsIgnoreCase ("limit"))
                return tr ("Dieser Schlüssel ist schon auf der erlaubten Anzahl Computer aktiv. Gib ihn zuerst auf einem anderen Computer frei (≡ › Lizenz › Diesen Computer freigeben).",
                           "This key is already active on the allowed number of computers. Release it on another computer first (≡ › Licence › Release this computer).");
            if (r.keyStatus == "expired")
                return tr ("Dieser Schlüssel ist abgelaufen.", "This key has expired.");
            // Polar: 403 = revoked, disabled or expired (the limit is handled above)
            if (r.keyStatus == "disabled" || r.keyStatus == "revoked" || r.http == 403)
                return tr ("Dieser Schlüssel wurde deaktiviert. Schreib uns an support@lind-audio.com, wenn das ein Fehler ist.",
                           "This key has been disabled. Write to support@lind-audio.com if that is a mistake.");
            return tr ("Dieser Schlüssel passt nicht. Kopier ihn genau so, wie er in der E-Mail steht.",
                       "This key does not work. Copy it exactly as it appears in the email.");
        }
    }

    Status evaluate (const Record& r, juce::int64 nowMs, bool licensingOn, int trialDaysAllowed) noexcept
    {
        Status s;
        if (! licensingOn)
            return s;   // open

        if (r.key.isNotEmpty() && r.instanceId.isNotEmpty() && ! r.revoked)
        {
            s.access = Access::licensed;
            s.checkDue = nowMs - r.lastCheckMs >= recheckDays * dayMs || nowMs < r.lastCheckMs;
            return s;
        }

        const juce::int64 start = r.firstRunMs > 0 ? r.firstRunMs : nowMs;
        const juce::int64 used = juce::jmax ((juce::int64) 0, nowMs - start);
        const juce::int64 left = juce::jmax (0, trialDaysAllowed) * dayMs - used;   // 0 days: locked at once
        if (left > 0)
        {
            s.access = Access::trial;
            s.trialDaysLeft = (int) ((left + dayMs - 1) / dayMs);
        }
        else
        {
            s.access = Access::locked;
        }
        return s;
    }

    Reply parseReply (const juce::String& json, int httpStatus, Call call,
                      const juce::String& organizationId, const juce::String& benefitId)
    {
        // Polar customer-portal licence API (docs: polar.sh/docs/api-reference/customer-portal/license-keys):
        //   activate   200 {id (activation), license_key:{status, organization_id, benefit_id, customer:{name}}}
        //   validate   200 {status, organization_id, benefit_id, customer:{name}, activation:{id}}
        //   deactivate 204 (no body)
        //   errors     403 {error:"NotPermitted", detail} (revoked / disabled / expired / activation limit),
        //              404 {error:"ResourceNotFound"} (unknown key or activation), 422 (malformed request)
        Reply r;
        r.http = httpStatus;
        const auto v = juce::JSON::parse (json);
        const bool success = httpStatus >= 200 && httpStatus < 300;
        // 4xx: the server answered (invalid key, limit reached ...); 0 / 5xx / no JSON: not reached
        r.reached = httpStatus >= 200 && httpStatus < 500 && (v.isObject() || (success && json.trim().isEmpty()));
        if (! r.reached)
            return r;

        if (! success)
        {
            r.error = v.getProperty ("error", {}).toString();
            if (const auto d = v.getProperty ("detail", {}); d.isString())
                r.detail = d.toString();
            return r;
        }
        if (call == Call::deactivate)
        {
            r.ok = true;
            return r;
        }

        const auto key = call == Call::activate ? v.getProperty ("license_key", {}) : v;
        if (! key.isObject())
            return r;
        r.keyStatus = key.getProperty ("status", {}).toString();
        if (const auto customer = key.getProperty ("customer", {}); customer.isObject())
            r.customer = customer.getProperty ("name", {}).toString();
        if (call == Call::activate)
            r.instanceId = v.getProperty ("id", {}).toString();
        else if (const auto act = v.getProperty ("activation", {}); act.isObject())
            r.instanceId = act.getProperty ("id", {}).toString();

        r.ok = r.keyStatus == "granted" && (call == Call::validate || r.instanceId.isNotEmpty());
        if (r.ok)
        {
            const auto org = key.getProperty ("organization_id", {}).toString();
            const auto benefit = key.getProperty ("benefit_id", {}).toString();
            if ((organizationId.isNotEmpty() && org != organizationId) || (benefitId.isNotEmpty() && benefit != benefitId))
            {
                r.ok = false;
                r.error = "key of another product";
            }
        }
        return r;
    }

    juce::String cleanKey (const juce::String& key)
    {
        // Polar keys: UUID, optionally with a prefix such as EMBER_
        return key.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
    }

    juce::String signatureOf (const Record& r, const FileIdentity& id)
    {
        // Not a secret against a determined cracker - the salt only makes a hand-edited file invalid
        const auto text = id.salt + "|" + r.key + "|" + r.instanceId + "|" + r.customer + "|"
                          + juce::String (r.firstRunMs) + "|" + juce::String (r.lastCheckMs) + "|" + (r.revoked ? "1" : "0");
        // FNV-1a 64, two seeds (juce_cryptography is not linked; this only has to notice an edit)
        juce::String out;
        for (juce::uint64 h : { (juce::uint64) 0xcbf29ce484222325ULL, (juce::uint64) 0x84222325cbf29ce4ULL })
        {
            for (auto* c = text.toRawUTF8(); *c != 0; ++c)
                h = (h ^ (juce::uint8) *c) * 0x100000001b3ULL;
            out << juce::String::toHexString ((juce::int64) h);
        }
        return out;
    }

    Record readRecord (const juce::File& f, const FileIdentity& id)
    {
        Record r;
        const auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName (id.tag))
            return r;
        r.key = xml->getStringAttribute ("key");
        r.instanceId = xml->getStringAttribute ("instance");
        r.customer = xml->getStringAttribute ("customer");
        r.firstRunMs = xml->getStringAttribute ("firstRun").getLargeIntValue();
        r.lastCheckMs = xml->getStringAttribute ("lastCheck").getLargeIntValue();
        r.revoked = xml->getBoolAttribute ("revoked");
        if (xml->getStringAttribute ("sig") != signatureOf (r, id))
        {
            // Edited by hand: the key is dropped, the trial counts from the stored start (never a fresh trial)
            Record dropped;
            dropped.firstRunMs = r.firstRunMs > 0 ? r.firstRunMs : 1;
            return dropped;
        }
        return r;
    }

    bool writeRecord (const juce::File& f, const Record& r, const FileIdentity& id)
    {
        juce::XmlElement xml (id.tag);
        xml.setAttribute ("key", r.key);
        xml.setAttribute ("instance", r.instanceId);
        xml.setAttribute ("customer", r.customer);
        xml.setAttribute ("firstRun", juce::String (r.firstRunMs));
        xml.setAttribute ("lastCheck", juce::String (r.lastCheckMs));
        xml.setAttribute ("revoked", r.revoked);
        xml.setAttribute ("sig", signatureOf (r, id));
        f.getParentDirectory().createDirectory();
        return xml.writeTo (f);
    }

    Transport internetTransport()
    {
        return [] (const juce::String& url, const juce::var& json) -> std::pair<int, juce::String>
        {
            const auto body = juce::JSON::toString (json, true);
            int status = 0;
            const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                     .withExtraHeaders ("Accept: application/json\r\nContent-Type: application/json")
                                     .withConnectionTimeoutMs (8000)
                                     .withStatusCode (&status)
                                     .withHttpRequestCmd ("POST");
            auto stream = juce::URL (url).withPOSTData (body).createInputStream (options);
            if (stream == nullptr)
                return { 0, {} };
            return { status, stream->readEntireStreamAsString() };
        };
    }

    //==============================================================================

    Manager::Manager (Config c) : cfg (std::move (c))
    {
        if (! cfg.now)
            cfg.now = [] { return juce::Time::currentTimeMillis(); };
        if (! cfg.on)
            return;   // no file, no thread, no network

        if (cfg.file == juce::File())
            cfg.file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                           .getChildFile ("Lind Audio").getChildFile (cfg.productName).getChildFile (cfg.productName + ".license");
        if (! cfg.transport)
            cfg.transport = internetTransport();
        if (cfg.asynchronous)
            pool = std::make_unique<juce::ThreadPool> (juce::ThreadPoolOptions{}.withThreadName (cfg.productName + " licence").withNumberOfThreads (1));

        rec = readRecord (cfg.file, cfg.identity);
        fileTime = cfg.file.getLastModificationTime();
        if (rec.firstRunMs <= 0)
        {
            rec.firstRunMs = cfg.now();
            store();
        }
        update();
        checkIfDue();
    }

    Manager::~Manager()
    {
        *alive = false;
        if (pool != nullptr)
            pool->removeAllJobs (true, 10000);   // a request in flight ends with its 8 s timeout
    }

    Status Manager::status() const
    {
        return evaluate (rec, cfg.now(), cfg.on, cfg.trialDays);
    }

    void Manager::update()
    {
        locked.store (status().access == Access::locked, std::memory_order_relaxed);
        sendChangeMessage();
    }

    void Manager::store()
    {
        if (cfg.on)
        {
            writeRecord (cfg.file, rec, cfg.identity);
            fileTime = cfg.file.getLastModificationTime();
        }
    }

    void Manager::reloadIfChanged()
    {
        if (! cfg.on || busy.load() || ! cfg.file.existsAsFile())
            return;
        const auto t = cfg.file.getLastModificationTime();
        if (t == fileTime)
            return;
        fileTime = t;
        const auto fresh = readRecord (cfg.file, cfg.identity);
        if (fresh.key == rec.key && fresh.instanceId == rec.instanceId && fresh.revoked == rec.revoked)
            return;
        rec = fresh;
        update();
    }

    void Manager::run (std::function<void()> job)
    {
        if (pool == nullptr)
            job();
        else
            pool->addJob (std::move (job));
    }

    void Manager::activate (const juce::String& keyIn, Done done)
    {
        const auto key = cleanKey (keyIn);
        if (! cfg.on || key.isEmpty())
        {
            if (done) done (false, tr ("Gib den Schlüssel aus der E-Mail von Lind Audio ein.", "Enter the key from the Lind Audio email."));
            return;
        }
        if (busy.exchange (true))
        {
            if (done) done (false, tr ("Einen Moment – die Aktivierung läuft noch.", "One moment – the activation is still running."));
            return;
        }
        const auto form = jsonBody ({ { "key", key }, { "organization_id", cfg.organizationId },
                                      { "label", juce::SystemStats::getComputerName().isNotEmpty() ? juce::SystemStats::getComputerName() : cfg.productName } });

        auto transport = cfg.transport;
        const auto api = cfg.apiBase, org = cfg.organizationId, benefit = cfg.benefitId;
        const bool async = pool != nullptr;
        auto flag = alive;
        run ([this, flag, async, transport, form, key, api, org, benefit, done]
        {
            const auto [code, body] = transport (api + "activate", form);
            const auto reply = parseReply (body, code, Call::activate, org, benefit);
            if (reply.error == "key of another product" && reply.instanceId.isNotEmpty())
            {
                // the store already counted this computer for that other product: give the slot back
                transport (api + "deactivate", jsonBody ({ { "key", key }, { "organization_id", org }, { "activation_id", reply.instanceId } }));
            }
            auto apply = [this, flag, reply, key, done]
            {
                if (! *flag)
                    return;
                busy = false;
                if (reply.ok && reply.instanceId.isNotEmpty())
                {
                    rec.key = key;
                    rec.instanceId = reply.instanceId;
                    rec.customer = reply.customer;
                    rec.revoked = false;
                    rec.lastCheckMs = cfg.now();
                    store();
                    update();
                    if (done) done (true, i18n::format (tr ("{0} ist aktiviert. Danke!", "{0} is activated. Thank you!"), { cfg.productName }));
                }
                else if (done)
                {
                    done (false, messageFor (reply));
                }
            };
            if (async) juce::MessageManager::callAsync (std::move (apply)); else apply();
        });
    }

    void Manager::deactivate (Done done)
    {
        if (! cfg.on || rec.instanceId.isEmpty() || busy.exchange (true))
        {
            if (done) done (false, {});
            return;
        }
        const auto form = jsonBody ({ { "key", rec.key }, { "organization_id", cfg.organizationId }, { "activation_id", rec.instanceId } });
        auto transport = cfg.transport;
        const auto api = cfg.apiBase;
        const bool async = pool != nullptr;
        auto flag = alive;
        run ([this, flag, async, transport, form, api, done]
        {
            const auto [code, body] = transport (api + "deactivate", form);
            const auto reply = parseReply (body, code, Call::deactivate, {}, {});
            auto apply = [this, flag, reply, done]
            {
                if (! *flag)
                    return;
                busy = false;
                // 404: the store no longer knows this activation (removed in the dashboard) - free it here as well
                if (reply.ok || reply.http == 404)
                {
                    const auto firstRun = rec.firstRunMs;
                    rec = {};
                    rec.firstRunMs = firstRun;   // the trial does not start again
                    store();
                    update();
                    if (done) done (true, tr ("Dieser Computer ist freigegeben. Du kannst den Schlüssel jetzt woanders aktivieren.",
                                              "This computer is released. You can now activate the key elsewhere."));
                }
                else if (done)
                {
                    done (false, messageFor (reply));
                }
            };
            if (async) juce::MessageManager::callAsync (std::move (apply)); else apply();
        });
    }

    void Manager::checkIfDue()
    {
        if (! cfg.on || ! status().checkDue || busy.exchange (true))
            return;
        const auto form = jsonBody ({ { "key", rec.key }, { "organization_id", cfg.organizationId }, { "activation_id", rec.instanceId } });
        auto transport = cfg.transport;
        const auto api = cfg.apiBase, org = cfg.organizationId, benefit = cfg.benefitId;
        const bool async = pool != nullptr;
        auto flag = alive;
        run ([this, flag, async, transport, form, api, org, benefit]
        {
            const auto [code, body] = transport (api + "validate", form);
            const auto reply = parseReply (body, code, Call::validate, org, benefit);
            auto apply = [this, flag, reply]
            {
                if (! *flag)
                    return;
                busy = false;
                // Locked only by a clear answer: 2xx with valid = false, or 404 (key / activation unknown).
                // Offline, 429 (too many requests), other 4xx and 5xx: stays activated, asked again next time.
                const bool clearAnswer = (reply.http >= 200 && reply.http < 300) || reply.http == 404;
                if (! reply.reached || ! clearAnswer)
                    return;
                rec.lastCheckMs = cfg.now();
                rec.revoked = ! reply.ok;
                store();
                update();
            };
            if (async) juce::MessageManager::callAsync (std::move (apply)); else apply();
        });
    }

    juce::String Manager::describe() const
    {
        const auto s = status();
        switch (s.access)
        {
            case Access::open:     return {};
            case Access::licensed: return i18n::format (tr ("Aktiviert ({0}){1}", "Activated ({0}){1}"),
                                                        { shortKey (rec.key), rec.customer.isNotEmpty() ? juce::String (", ") + rec.customer : juce::String() });
            case Access::trial:    return s.trialDaysLeft == 1 ? tr ("Testversion: noch 1 Tag", "Trial: 1 day left")
                                                               : i18n::format (tr ("Testversion: noch {0} Tage", "Trial: {0} days left"), { juce::String (s.trialDaysLeft) });
            case Access::locked:   return rec.revoked ? tr ("Schlüssel ungültig – bitte neu aktivieren", "Key invalid – please activate again")
                                                      : cfg.trialDays > 0 ? tr ("Testzeit vorbei – bitte aktivieren", "Trial over – please activate")
                                                                          : tr ("Nicht aktiviert – bitte Schlüssel eingeben", "Not activated – please enter your key");
        }
        return {};
    }
}
