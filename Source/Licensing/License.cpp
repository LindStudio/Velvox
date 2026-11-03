#include "License.h"
#include "../ProductInfo.h"
#include "../Text/I18n.h"

namespace vocalypse::licensing
{
    using i18n::tr;

    namespace
    {
        constexpr juce::int64 dayMs = 24LL * 60 * 60 * 1000;
        constexpr const char* apiBase = "https://api.lemonsqueezy.com/v1/licenses/";

        // Not a secret against a determined cracker - it only makes a hand-edited file invalid
        constexpr const char* salt = "VOCALYPSE/Lnda/Vlvx/7c1e";

        juce::String shortKey (const juce::String& key)
        {
            return key.length() > 8 ? key.substring (0, 4) + juce::String::fromUTF8 ("\xe2\x80\xa6") + key.getLastCharacters (4) : key;
        }

        juce::String messageFor (const Reply& r)
        {
            if (! r.reached)
                return tr ("Keine Verbindung zum Lizenz-Server. Prüf die Internetverbindung und versuch es noch einmal.",
                           "No connection to the licence server. Check the internet connection and try again.");
            if (r.error.containsIgnoreCase ("activation limit"))
                return tr ("Dieser Schlüssel ist schon auf der erlaubten Anzahl Computer aktiv. Gib ihn zuerst auf einem anderen Computer frei (Einstellungen > Lizenz > Freigeben).",
                           "This key is already active on the allowed number of computers. Release it on another computer first (Settings > Licence > Release).");
            if (r.keyStatus == "expired")
                return tr ("Dieser Schlüssel ist abgelaufen.", "This key has expired.");
            if (r.keyStatus == "disabled")
                return tr ("Dieser Schlüssel wurde deaktiviert. Schreib uns, wenn das ein Fehler ist.",
                           "This key has been disabled. Write to us if that is a mistake.");
            return tr ("Dieser Schlüssel passt nicht. Kopier ihn genau so aus der Bestell-Mail.",
                       "This key does not work. Copy it exactly as it is in the order mail.");
        }
    }

    Status evaluate (const Record& r, juce::int64 nowMs, bool licensingOn) noexcept
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
        const juce::int64 left = trialDays * dayMs - used;
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

    Reply parseReply (const juce::String& json, int httpStatus, const juce::String& okField,
                      juce::int64 storeId, juce::int64 productId)
    {
        Reply r;
        const auto v = juce::JSON::parse (json);
        // 4xx: the server answered (invalid key, limit reached ...); 0 / 5xx / no JSON: not reached
        r.reached = httpStatus >= 200 && httpStatus < 500 && v.isObject();
        if (! r.reached)
            return r;

        r.error = v.getProperty ("error", {}).toString();
        if (const auto key = v.getProperty ("license_key", {}); key.isObject())
            r.keyStatus = key.getProperty ("status", {}).toString();
        if (const auto inst = v.getProperty ("instance", {}); inst.isObject())
            r.instanceId = inst.getProperty ("id", {}).toString();
        const auto meta = v.getProperty ("meta", {});
        if (meta.isObject())
            r.customer = meta.getProperty ("customer_name", {}).toString();

        r.ok = (bool) v.getProperty (juce::Identifier (okField), false);
        if (r.ok && meta.isObject())
        {
            const auto store = (juce::int64) meta.getProperty ("store_id", 0);
            const auto product = (juce::int64) meta.getProperty ("product_id", 0);
            if ((storeId != 0 && store != storeId) || (productId != 0 && product != productId))
            {
                r.ok = false;
                r.error = "key of another product";
            }
        }
        if (r.ok && (r.keyStatus == "disabled" || r.keyStatus == "expired"))
            r.ok = false;
        return r;
    }

    juce::String signatureOf (const Record& r)
    {
        const auto text = juce::String (salt) + "|" + r.key + "|" + r.instanceId + "|" + r.customer + "|"
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

    Record readRecord (const juce::File& f)
    {
        Record r;
        const auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName ("VocalypseLicence"))
            return r;
        r.key = xml->getStringAttribute ("key");
        r.instanceId = xml->getStringAttribute ("instance");
        r.customer = xml->getStringAttribute ("customer");
        r.firstRunMs = xml->getStringAttribute ("firstRun").getLargeIntValue();
        r.lastCheckMs = xml->getStringAttribute ("lastCheck").getLargeIntValue();
        r.revoked = xml->getBoolAttribute ("revoked");
        if (xml->getStringAttribute ("sig") != signatureOf (r))
        {
            // Edited by hand: the key is dropped, the trial counts from the stored start (never a fresh trial)
            Record dropped;
            dropped.firstRunMs = r.firstRunMs > 0 ? r.firstRunMs : 1;
            return dropped;
        }
        return r;
    }

    bool writeRecord (const juce::File& f, const Record& r)
    {
        juce::XmlElement xml ("VocalypseLicence");
        xml.setAttribute ("key", r.key);
        xml.setAttribute ("instance", r.instanceId);
        xml.setAttribute ("customer", r.customer);
        xml.setAttribute ("firstRun", juce::String (r.firstRunMs));
        xml.setAttribute ("lastCheck", juce::String (r.lastCheckMs));
        xml.setAttribute ("revoked", r.revoked);
        xml.setAttribute ("sig", signatureOf (r));
        f.getParentDirectory().createDirectory();
        return xml.writeTo (f);
    }

    Transport internetTransport()
    {
        return [] (const juce::String& url, const juce::StringPairArray& form) -> std::pair<int, juce::String>
        {
            juce::String body;
            for (int i = 0; i < form.size(); ++i)
                body << (i > 0 ? "&" : "") << juce::URL::addEscapeChars (form.getAllKeys()[i], true)
                     << "=" << juce::URL::addEscapeChars (form.getAllValues()[i], true);
            int status = 0;
            const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                     .withExtraHeaders ("Accept: application/json\r\nContent-Type: application/x-www-form-urlencoded")
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
    namespace
    {
        Config buildConfig()
        {
            Config c;
            c.storeId = product::lemonStoreId;
            c.productId = product::lemonProductId;
            return c;
        }
    }

    Manager::Manager() : Manager (buildConfig()) {}

    Manager::Manager (Config c) : cfg (std::move (c))
    {
        if (! cfg.now)
            cfg.now = [] { return juce::Time::currentTimeMillis(); };
        if (! cfg.on)
            return;   // no file, no thread, no network

        if (cfg.file == juce::File())
            cfg.file = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                           .getChildFile (product::companyName).getChildFile (product::name).getChildFile ("VOCALYPSE.license");
        if (! cfg.transport)
            cfg.transport = internetTransport();
        if (cfg.asynchronous)
            pool = std::make_unique<juce::ThreadPool> (juce::ThreadPoolOptions{}.withThreadName ("VOCALYPSE licence").withNumberOfThreads (1));

        rec = readRecord (cfg.file);
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
        return evaluate (rec, cfg.now(), cfg.on);
    }

    void Manager::update()
    {
        locked.store (status().access == Access::locked, std::memory_order_relaxed);
        sendChangeMessage();
    }

    void Manager::store()
    {
        if (cfg.on)
            writeRecord (cfg.file, rec);
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
        const auto key = keyIn.trim();
        if (! cfg.on || key.isEmpty() || busy.exchange (true))
        {
            if (done) done (false, tr ("Gib den Schlüssel aus der Bestell-Mail ein.", "Enter the key from the order mail."));
            return;
        }
        juce::StringPairArray form;
        form.set ("license_key", key);
        form.set ("instance_name", juce::SystemStats::getComputerName().isNotEmpty() ? juce::SystemStats::getComputerName() : juce::String ("VOCALYPSE"));

        auto transport = cfg.transport;
        const auto storeId = cfg.storeId, productId = cfg.productId;
        const bool async = pool != nullptr;
        auto flag = alive;
        run ([this, flag, async, transport, form, key, storeId, productId, done]
        {
            const auto [code, body] = transport (juce::String (apiBase) + "activate", form);
            const auto reply = parseReply (body, code, "activated", storeId, productId);
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
                    if (done) done (true, tr ("VOCALYPSE ist aktiviert. Danke!", "VOCALYPSE is activated. Thank you!"));
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
        juce::StringPairArray form;
        form.set ("license_key", rec.key);
        form.set ("instance_id", rec.instanceId);
        auto transport = cfg.transport;
        const bool async = pool != nullptr;
        auto flag = alive;
        run ([this, flag, async, transport, form, done]
        {
            const auto [code, body] = transport (juce::String (apiBase) + "deactivate", form);
            const auto reply = parseReply (body, code, "deactivated", 0, 0);
            auto apply = [this, flag, reply, done]
            {
                if (! *flag)
                    return;
                busy = false;
                if (reply.ok)
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
        juce::StringPairArray form;
        form.set ("license_key", rec.key);
        form.set ("instance_id", rec.instanceId);
        auto transport = cfg.transport;
        const auto storeId = cfg.storeId, productId = cfg.productId;
        const bool async = pool != nullptr;
        auto flag = alive;
        run ([this, flag, async, transport, form, storeId, productId]
        {
            const auto [code, body] = transport (juce::String (apiBase) + "validate", form);
            const auto reply = parseReply (body, code, "valid", storeId, productId);
            auto apply = [this, flag, reply]
            {
                if (! *flag)
                    return;
                busy = false;
                if (! reply.reached)
                    return;   // offline: stays activated, asked again next time
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
            case Access::locked:   return rec.revoked ? tr ("Schlüssel ungültig - bitte neu aktivieren", "Key invalid - please activate again")
                                                      : tr ("Testzeit vorbei - bitte aktivieren", "Trial over - please activate");
        }
        return {};
    }

    juce::String Manager::buyUrl()
    {
        return product::buyUrl;
    }
}
