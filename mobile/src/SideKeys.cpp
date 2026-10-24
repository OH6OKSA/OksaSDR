#include "SideKeys.h"

#include <QTimer>

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

bool macTextEntryAvailable()
{
    return qEnvironmentVariableIsSet("AETHER_MAC_ENTRY_TEST");
}

void showMacTextEntry(const QString&, const QString&, const QString&, bool, const QString&, const QString&,
                      std::function<void(bool ok, const QString& text)> done)
{
    // Desktop test of the path: the variable's text, as if typed and OK'd.
    QTimer::singleShot(200, [done = std::move(done)] { done(true, qEnvironmentVariable("AETHER_MAC_ENTRY_TEST")); });
}

void startMacKeyDiagnostics()
{
}

QString macKeyRouting()
{
    return {};
}

} // namespace AetherSDR::mobile
