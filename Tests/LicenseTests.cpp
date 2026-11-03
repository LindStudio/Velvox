// Licence check (Source/Licensing/License.h): trial, activation, weekly re-check, release - against a
// fake Lemon Squeezy server, never the network.
#include <juce_core/juce_core.h>
#include "../Source/Licensing/License.h"

namespace
{
    using namespace vocalypse::licensing;
    constexpr juce::int64 day = 24LL * 60 * 60 * 1000;

    juce::String activated (const juce::String& instance, int store = 7, int product = 42)
    {
        return R"({"activated":true,"error":null,"license_key":{"id":1,"status":"active","key":"38b1460a-5104-4067-a91d-77b872934d51","activation_limit":2,"activation_usage":1},)"
               R"("instance":{"id":")" + instance + R"(","name":"PC"},"meta":{"store_id":)" + juce::String (store)
               + R"(,"product_id":)" + juce::String (product) + R"(,"customer_name":"Jakob"}})";
    }

    struct FakeServer
    {
        int status = 200;
        juce::String body;
        juce::StringArray calls;
        Transport transport()
        {
            return [this] (const juce::String& url, const juce::StringPairArray& form) -> std::pair<int, juce::String>
            {
                calls.add (url.fromLastOccurrenceOf ("/", false, false) + " " + form["license_key"] + " " + form["instance_id"]);
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
            auto ok = parseReply (activated ("inst-1"), 200, "activated", 7, 42);
            expect (ok.ok && ok.reached);
            expectEquals (ok.instanceId, juce::String ("inst-1"));
            expectEquals (ok.customer, juce::String ("Jakob"));
            expect (parseReply (activated ("i"), 200, "activated", 0, 0).ok, "ids 0: not checked");
            expect (! parseReply (activated ("i", 8), 200, "activated", 7, 42).ok, "key of another store");
            expect (! parseReply (activated ("i", 7, 43), 200, "activated", 7, 42).ok, "key of another product");

            const auto bad = parseReply (R"({"activated":false,"error":"license_key not found."})", 404, "activated", 7, 42);
            expect (bad.reached && ! bad.ok);
            const auto limit = parseReply (R"({"activated":false,"error":"This license key has reached the activation limit."})", 400, "activated", 0, 0);
            expect (limit.reached && ! limit.ok && limit.error.contains ("activation limit"));
            const auto disabled = parseReply (R"({"valid":true,"error":null,"license_key":{"status":"disabled"},"meta":{}})", 200, "valid", 0, 0);
            expect (disabled.reached && ! disabled.ok);
            expect (! parseReply ({}, 0, "valid", 0, 0).reached, "no network");
            expect (! parseReply ("<html>", 502, "valid", 0, 0).reached, "server error");
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
            c.storeId = 7; c.productId = 42;
            c.asynchronous = false;

            auto m = std::make_unique<Manager> (c);
            expect (m->status().access == Access::trial && ! m->isLocked());
            expect (m->describe().contains ("14"));
            expectEquals (server.calls.size(), 0, "no network during the trial");

            now += 15 * day;
            m = std::make_unique<Manager> (c);
            expect (m->isLocked(), "trial over: locked (the start was stored)");

            server.body = R"({"activated":false,"error":"license_key not found."})"; server.status = 404;
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
            server.status = 404; server.body = R"({"valid":false,"error":"license_key not found."})";
            m->checkIfDue();
            expect (m->isLocked() && m->record().revoked);

            // Activated again, then released for another computer: the trial does not restart
            server.status = 200; server.body = activated ("inst-10");
            m->activate ("KEY-1", nullptr);
            expect (! m->isLocked());
            server.body = R"({"deactivated":true,"error":null,"license_key":{"status":"inactive"},"meta":{"store_id":7,"product_id":42}})";
            m->deactivate ([&] (bool ok, const juce::String&) { result = ok; });
            expect (result && m->isLocked() && m->record().key.isEmpty());
            expect (server.calls.getReference (server.calls.size() - 1) == "deactivate KEY-1 inst-10");
        }
    }
};

static LicenseTests licenseTests;
