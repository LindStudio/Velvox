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
            c.storeId = product::lemonStoreId;
            c.productId = product::lemonProductId;
            return c;
        }
    }

    Manager::Manager() : Manager (buildConfig()) {}
}
