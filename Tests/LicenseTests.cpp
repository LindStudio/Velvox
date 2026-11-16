// Licence check (Source/Licensing/License.h): trial, activation, weekly re-check, release - against a
// fake Polar server, never the network.
#include <juce_core/juce_core.h>
#include "../Source/Licensing/License.h"

namespace
{
    using namespace vocalypse::licensing;
    constexpr juce::int64 day = 24LL * 60 * 60 * 1000;

    juce::String activated (const juce::String& instance, const juce::String& org = "org-7", const juce::String& benefit = "ben-42")
    {
        return R"({"id":")" + instance + R"(","label":"PC","license_key":{"status":"granted","organization_id":")" + org
               + R"(","benefit_id":")" + benefit + R"(","customer":{"name":"Jakob"}}})";
    }

    struct FakeServer
    {
        int status = 200;
        juce::String body;
        juce::StringArray calls;
        Transport transport()
        {
            return [this] (const juce::String& url, const juce::var& json) -> std::pair<int, juce::String>
            {
                calls.add (url.fromLastOccurrenceOf ("/", false, false) + " " + json["key"].toString() + " " + json["activation_id"].toString());
                return { status, body };
            };
        }
    };
}

class LicenseTests final : public juce::UnitTest
{
public:
    LicenseTests() : juce::UnitTest ("VOCALYPSE licence", "Vocalypse") {}

    void runTest() override
    {
        beginTest ("Switched off (the build default until the store exists): open, nothing locked");
        {
            expect (evaluate ({}, 0, false).access == Access::open);
            Config c;
            c.on = false;
            Manager m (c);
            expect (! m.isLocked() && m.status().access == Access::open && m.describe().isEmpty());
            expect (! enabledInBuild, "the shipped build has no licence check yet (docs/LICENSING.md)");
        }

        beginTest ("Trial: 14 days from the first start, then locked");
        {
            Record r;
            r.firstRunMs = 1000 * day;
            auto s = evaluate (r, 1000 * day, true);
            expect (s.access == Access::trial);
            expectEquals (s.trialDaysLeft, 14);
            s = evaluate (r, 1000 * day + 13 * day + day / 2, true);
            expect (s.access == Access::trial);
            expectEquals (s.trialDaysLeft, 1);
            expect (evaluate (r, 1014 * day, true).access == Access::locked);
            expect (evaluate (r, 1000 * day, true, 0).access == Access::locked, "0 trial days: locked from the first start");
            expect (evaluate (r, 1000 * day + 3 * day, true, 7).trialDaysLeft == 4, "own trial length");
            r.firstRunMs = 0;
            expect (evaluate (r, 5, true).access == Access::trial, "no start stored: the trial starts now");
        }

        beginTest ("Activated: stays activated (also offline); re-check due after a week");
        {
            Record r;
            r.firstRunMs = 1;
            r.key = "k"; r.instanceId = "i"; r.lastCheckMs = 100 * day;
            expect (evaluate (r, 400 * day, true).access == Access::licensed);
            expect (! evaluate (r, 106 * day, true).checkDue);
            expect (evaluate (r, 107 * day, true).checkDue);
            expect (evaluate (r, 99 * day, true).checkDue, "clock turned back: check");
            r.revoked = true;
            expect (evaluate (r, 400 * day, true).access == Access::locked);
        }

        beginTest ("Server answers: activated, invalid key, limit, another product, disabled, no network");
        {
            auto ok = parseReply (activated ("inst-1"), 200, Call::activate, "org-7", "ben-42");
            expect (ok.ok && ok.reached);
            expectEquals (ok.instanceId, juce::String ("inst-1"));
            expectEquals (ok.customer, juce::String ("Jakob"));
            expect (parseReply (activated ("i"), 200, Call::activate, {}, {}).ok, "ids empty: not checked");
            expect (! parseReply (activated ("i", "org-8"), 200, Call::activate, "org-7", "ben-42").ok, "key of another organisation");
            expect (! parseReply (activated ("i", "org-7", "ben-43"), 200, Call::activate, "org-7", "ben-42").ok, "key of another product");

            const auto bad = parseReply (R"({"error":"ResourceNotFound","detail":"Not found"})", 404, Call::activate, "org-7", {});
            expect (bad.reached && ! bad.ok);
            const auto limit = parseReply (R"({"error":"NotPermitted","detail":"License key activation limit reached"})", 403, Call::activate, {}, {});
            expect (limit.reached && ! limit.ok && limit.detail.contains ("limit"));
            const auto disabled = parseReply (R"({"status":"disabled","organization_id":"org-7"})", 200, Call::validate, {}, {});
            expect (disabled.reached && ! disabled.ok);
            expect (! parseReply ({}, 0, Call::validate, {}, {}).reached, "no network");
            expect (! parseReply ("<html>", 502, Call::validate, {}, {}).reached, "server error");
        }

        beginTest ("Stored record: signed; a hand-edited file loses the key, never restarts the trial");
        {
            juce::TemporaryFile tmp (".license");
            Record r;
            r.key = "abc"; r.instanceId = "i"; r.customer = "J"; r.firstRunMs = 77; r.lastCheckMs = 88;
            expect (writeRecord (tmp.getFile(), r));
            const auto back = readRecord (tmp.getFile());
            expect (back.key == "abc" && back.instanceId == "i" && back.firstRunMs == 77 && back.lastCheckMs == 88);
            tmp.getFile().replaceWithText (tmp.getFile().loadFileAsString().replace ("lastCheck=\"88\"", "lastCheck=\"99999\""));
            const auto edited = readRecord (tmp.getFile());
            expect (edited.key.isEmpty() && edited.firstRunMs == 77);
            expect (readRecord (juce::File()).key.isEmpty());
        }

        beginTest ("Manager: trial -> activate -> weekly check -> invalid locks -> offline keeps -> release");
        {
            juce::TemporaryFile tmp (".license");
            FakeServer server;
            juce::int64 now = 500 * day;
            Config c;
            c.on = true;
            c.file = tmp.getFile();
            c.transport = server.transport();
            c.now = [&now] { return now; };
            c.organizationId = "org-7"; c.benefitId = "ben-42";
            c.asynchronous = false;

            auto m = std::make_unique<Manager> (c);
            expect (m->status().access == Access::trial && ! m->isLocked());
            expect (m->describe().contains ("14"));
            expectEquals (server.calls.size(), 0, "no network during the trial");

            now += 15 * day;
            m = std::make_unique<Manager> (c);
            expect (m->isLocked(), "trial over: locked (the start was stored)");

            server.body = R"({"error":"ResourceNotFound","detail":"Not found"})"; server.status = 404;
            bool result = true; juce::String message;
            m->activate ("wrong", [&] (bool ok, const juce::String& msg) { result = ok; message = msg; });
            expect (! result && message.isNotEmpty() && m->isLocked());

            server.body = activated ("inst-9"); server.status = 200;
            m->activate ("  KEY-1  ", [&] (bool ok, const juce::String&) { result = ok; });
            expect (result && ! m->isLocked());
            expect (server.calls[1] == "activate KEY-1 ", server.calls[1]);
            expect (m->describe().contains ("Jakob"));

            // Reloaded from the file: still activated, no check before a week
            m = std::make_unique<Manager> (c);
            expect (m->status().access == Access::licensed);
            expectEquals (server.calls.size(), 2);

            // A week later, offline: stays activated
            now += 8 * day;
            server.status = 0; server.body = {};
            m = std::make_unique<Manager> (c);
            expectEquals (server.calls.size(), 3);
            expect (server.calls[2] == "validate KEY-1 inst-9", server.calls[2]);
            expect (! m->isLocked(), "offline: stays activated");

            // Online, the store refunded / disabled the key: locked
            server.status = 404; server.body = R"({"error":"ResourceNotFound","detail":"Not found"})";
            m->checkIfDue();
            expect (m->isLocked() && m->record().revoked);

            // Activated again, then released for another computer: the trial does not restart
            server.status = 200; server.body = activated ("inst-10");
            m->activate ("KEY-1", nullptr);
            expect (! m->isLocked());
            server.status = 204; server.body = {};
            m->deactivate ([&] (bool ok, const juce::String&) { result = ok; });
            expect (result && m->isLocked() && m->record().key.isEmpty());
            expect (server.calls.getReference (server.calls.size() - 1) == "deactivate KEY-1 inst-10");
        }
    }
};

static LicenseTests licenseTests;
