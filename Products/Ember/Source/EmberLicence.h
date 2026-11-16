#pragma once

#include "Licensing/License.h"

// EMBER's licence (Polar.sh licence keys since 2026-11-16 - Lemon Squeezy took no new stores; same rules as
// VOCALYPSE: docs/LICENSING.md). OFF unless CMake is run with -DEMBER_LICENSING=ON. Jakob's values from the
// Polar dashboard go here (docs/products/EMBER.md "Lizenz"); keys of another organisation / benefit are refused.
#ifndef EMBER_LICENSING
 #define EMBER_LICENSING 0
#endif

namespace ember
{
    // Polar organisation "Lind Audio" and product "EMBER De-Esser" (Jakob 2026-11-16). A key carries no product
    // id, only its licence-key benefit: polarBenefitId (empty = not checked, any key of the organisation works).
    // SANDBOX (sandbox.polar.sh, 2026-11-16): the live account gets its own ids -> replace all three + polarSandbox = false
    inline constexpr bool polarSandbox = true;
    inline constexpr const char* polarOrganizationId = "406e9435-111f-4a64-af63-0ca5a5b2aba2";
    inline constexpr const char* polarProductId = "5c48f7e6-e680-4a23-8007-1054bc0871c7";   // for the checkout link
    inline constexpr const char* polarBenefitId = "dc8884ec-f532-492d-ac7d-58e64a2b5ec3";  // "EMBER De-Esser Product Key", 2 computers
    inline constexpr const char* buyUrl = "https://lind-audio.com";   // TODO: Polar checkout link

    inline vocalypse::licensing::Config licenceConfig()
    {
        vocalypse::licensing::Config c;
        c.on = EMBER_LICENSING != 0;
        c.productName = "EMBER";   // file: %APPDATA%\Lind Audio\EMBER\EMBER.license
        c.identity = { "EMBER/Lnda/Embr/3f9a", "EmberLicence" };
        c.buyUrl = buyUrl;
        c.trialDays = 0;      // Jakob 2026-11-15: EMBER only runs with a key ...
        c.freeKey = true;     // ... which is free and comes by mail (mailing list)
        c.apiBase = polarSandbox ? vocalypse::licensing::polarSandboxApi : vocalypse::licensing::polarApi;
        c.organizationId = polarOrganizationId;
        c.benefitId = polarBenefitId;
        return c;
    }

    // One per process: juce::SharedResourcePointer<EmberLicence>
    class EmberLicence final : public vocalypse::licensing::Manager
    {
    public:
        EmberLicence() : Manager (licenceConfig()) {}
    };
}
