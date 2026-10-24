#include "SideKeys.h"

namespace AetherSDR::mobile {

// Qt's scan codes tell left and right apart here (Android, desktop).
bool sideKeysAvailable()
{
    return false;
}

void startSideKeys(std::function<void(SideKey key, bool pressed)>)
{
}

void setSideKeysSuspended(bool)
{
}

int sideKeysSeen()
{
    return 0;
}

bool runningAsIosAppOnMac()
{
    return false;
}

} // namespace AetherSDR::mobile
