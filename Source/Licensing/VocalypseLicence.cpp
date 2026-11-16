// VOCALYPSE's licence configuration (the single plugins have their own, e.g. Products/Ember/Source/EmberLicence.h)
#include "License.h"
#include "../ProductInfo.h"

namespace vocalypse::licensing
{
    namespace
    {
        Config buildConfig()
        {
            Config c;
            c.productName = product::name;
            c.buyUrl = product::buyUrl;
            c.organizationId = product::polarOrganizationId;
            c.benefitId = product::polarBenefitId;
            return c;
        }
    }

    Manager::Manager() : Manager (buildConfig()) {}
}
