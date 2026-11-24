#include "core/control/RadioCatalogue.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <cstdio>

using namespace AetherSDR;
using namespace AetherSDR::control;

// The real source factory is the subject. Both cases explicitly disable local
// discovery: no UDP socket, USB enumeration, radio backend, or settings access.
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    for (const bool simulator : {false, true}) {
        ControlResourceStore store;
        RadioCatalogue catalogue(makeLocalRadioDiscoverySource({false, simulator}), &store);
        catalogue.start();
        const QJsonObject value = store.get({QStringLiteral("radioCatalogue"), {}, {}})->value;
        const QJsonArray entries = value.value(QStringLiteral("entries")).toArray();
        const QJsonArray sources = value.value(QStringLiteral("sources")).toArray();
        if (entries.size() != (simulator ? 1 : 0) || sources.size() != (simulator ? 1 : 0)
            || (simulator && (sources.first() != QStringLiteral("sim")
                || entries.first().toObject().value(QStringLiteral("serial")) != QStringLiteral("DEMO-0001")
                || entries.first().toObject().value(QStringLiteral("transport")) != QStringLiteral("sim")))) {
            std::fprintf(stderr, "production source must honor passive/simulator-only options\n");
            return 1;
        }
    }
    // A client that names its families gets only those: OksaSDR asks for
    // "flex", so the HL2 and ANAN sweeps (a broadcast probe every five
    // seconds) are never made. enabledSources() opens no socket.
    {
        const QStringList all = makeLocalRadioDiscoverySource({true, false})->enabledSources();
        const QStringList flexOnly =
            makeLocalRadioDiscoverySource({true, false, {QStringLiteral("flex")}})->enabledSources();
        if (!all.contains(QStringLiteral("flex")) || !all.contains(QStringLiteral("hl2"))
            || !all.contains(QStringLiteral("anan")) || flexOnly != QStringList{QStringLiteral("flex")}) {
            std::fprintf(stderr, "a named family list must limit the local sources\n");
            return 1;
        }
    }
    return 0;
}
