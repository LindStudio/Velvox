#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <memory>

// Licence check (Lemon Squeezy License API), prepared 2026-11-03 - OFF until Jakob has a store:
// configure with -DVOCALYPSE_LICENSING=ON and fill in the store / product ids in ProductInfo.h.
// Product-neutral since 2026-11-12 (Config: name, file identity, shop link), so the single plugins
// (EMBER: -DEMBER_LICENSING=ON, Products/Ember/Source/EmberLicence.h) use the same code.
//
// Rules (docs/LICENSING.md):
//   - 14 days trial from the first start on a computer, everything works.
//   - Activation: the key from the order mail; Lemon Squeezy counts the computers (activation limit).
//   - Activated stays activated offline. Online it is checked again at most once a week, in the
//     background; only an explicit "invalid / disabled / expired" from the store locks it again.
//   - Locked (trial over, no valid key): the plugin passes the vocal through unprocessed and shows
//     the activation card. Projects, presets and settings are never touched.
//   - Network only on a background thread, never on the audio thread, never blocks loading.
#ifndef VOCALYPSE_LICENSING
 #define VOCALYPSE_LICENSING 0
#endif

namespace vocalypse::licensing
{
    inline constexpr bool enabledInBuild = VOCALYPSE_LICENSING != 0;
    inline constexpr int trialDays = 14;
    inline constexpr int recheckDays = 7;

    enum class Access { open, trial, licensed, locked };   // open: licensing switched off

    // What is stored per computer (signed, so it cannot be edited by hand)
    struct Record
    {
        juce::String key, instanceId, customer;
        juce::int64 firstRunMs = 0, lastCheckMs = 0;
        bool revoked = false;   // the store answered: this key is not (or no longer) valid
    };

    struct Status
    {
        Access access = Access::open;
        int trialDaysLeft = 0;
        bool checkDue = false;   // an activated key should be confirmed online again
    };

    Status evaluate (const Record&, juce::int64 nowMs, bool licensingOn) noexcept;

    // One answer of the License API (activate / validate / deactivate)
    struct Reply
    {
        bool ok = false;            // activated / valid / deactivated
        bool reached = false;       // false: no network / server error - nothing is decided
        juce::String error, instanceId, customer, keyStatus;
    };

    // `okField`: "activated", "valid" or "deactivated". A key of another store / product is refused.
    Reply parseReply (const juce::String& json, int httpStatus, const juce::String& okField,
                      juce::int64 storeId, juce::int64 productId);

    // Which product a licence file belongs to: the salt makes another product's file (or a hand-edited one) invalid
    struct FileIdentity
    {
        juce::String salt = "VOCALYPSE/Lnda/Vlvx/7c1e";
        juce::String tag = "VocalypseLicence";
    };

    juce::String signatureOf (const Record&, const FileIdentity& = {});
    Record readRecord (const juce::File&, const FileIdentity& = {});   // missing / edited file: empty record
    bool writeRecord (const juce::File&, const Record&, const FileIdentity& = {});

    // HTTP POST (form data) -> (status, body). Status 0 = not reached.
    using Transport = std::function<std::pair<int, juce::String> (const juce::String& url, const juce::StringPairArray& form)>;
    Transport internetTransport();

    struct Config
    {
        bool on = enabledInBuild;
        juce::String productName = "VOCALYPSE";   // in messages, the computer's name at the store, the default file
        FileIdentity identity;
        juce::String buyUrl;              // the shop / checkout page
        juce::File file;                  // default: <app data>/Lind Audio/<productName>/<productName>.license
        Transport transport;              // default: internetTransport()
        std::function<juce::int64()> now; // default: current time
        juce::int64 storeId = 0, productId = 0;   // 0 = not checked
        bool asynchronous = true;         // tests: false (the request runs inside the call)
    };

    // One per process (juce::SharedResourcePointer<Manager> in VOCALYPSE, a subclass with its own
    // configuration per single plugin). Message thread, except isLocked().
    class Manager : public juce::ChangeBroadcaster
    {
    public:
        Manager();                        // VOCALYPSE's configuration (VocalypseLicence.cpp)
        explicit Manager (Config);
        ~Manager() override;

        Status status() const;
        const Record& record() const noexcept { return rec; }
        bool isOn() const noexcept { return cfg.on; }

        // Audio thread: pass the vocal through unprocessed
        bool isLocked() const noexcept { return locked.load (std::memory_order_relaxed); }

        // done (ok, message for the user) on the message thread
        using Done = std::function<void (bool ok, const juce::String& message)>;
        void activate (const juce::String& key, Done done);
        void deactivate (Done done);      // frees this computer (to move the licence)
        void checkIfDue();                // background re-check once a week

        juce::String describe() const;    // one line for the settings sheet
        const juce::String& buyUrl() const noexcept { return cfg.buyUrl; }
        const juce::String& productName() const noexcept { return cfg.productName; }

    private:
        void store();
        void update();
        void run (std::function<void()> job);

        Config cfg;
        Record rec;
        std::atomic<bool> locked { false };
        std::atomic<bool> busy { false };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
        std::unique_ptr<juce::ThreadPool> pool;
    };
}
