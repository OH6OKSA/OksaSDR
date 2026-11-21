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

void startUlanziKeys(std::function<void(int, bool)>)
{
}

void setUlanziKeysOn(bool)
{
}

#ifndef Q_OS_ANDROID
void startKeyLog()
{
}
#endif

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

// A desktop test of the repair's path (roadmap 122): with
// AETHER_MAC_ROUTING_TEST set the routing reads as lost.
QString macKeyRouting()
{
    return qEnvironmentVariableIsSet("AETHER_MAC_ROUTING_TEST") ? QStringLiteral("test: no first responder") : QString();
}

bool macKeyRoutingLost()
{
    return qEnvironmentVariableIsSet("AETHER_MAC_ROUTING_TEST");
}

} // namespace AetherSDR::mobile
