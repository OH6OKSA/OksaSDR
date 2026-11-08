// The NRS level range follows the firmware (upstream #6498): FlexLib 4.3.16
// caps Slice.NRSLevel at 50, older firmware takes 100. FlexBackend reports the
// maximum in its capabilities, announces a change when the software version
// becomes known, and clamps a direct request; other DSP levels keep 100.

#include "core/backends/flex/FlexBackend.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QStringList>

#include <cstdio>

using namespace AetherSDR;

namespace {

int g_failed = 0;

void check(bool ok, const char* name)
{
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", name);
    if (!ok) {
        ++g_failed;
    }
}

SliceDspRequest level(SliceDspRequest::Feature feature, int value)
{
    SliceDspRequest request;
    request.feature = feature;
    request.field = SliceDspRequest::Field::Level;
    request.level = value;
    return request;
}

int maximum(const FlexBackend& backend)
{
    const auto control = backend.capabilities().nrsLevelControl;
    return control ? control->levelMaximum : -1;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    FlexBackend backend;
    QStringList commands;
    backend.setCommandSink([&](const QString& command) { commands.append(command); });
    backend.setSliceCommandSink([&](const QString& command) { commands.append(command); });
    QString version;
    backend.setFirmwareVersionProvider([&] { return version; });
    QSignalSpy announced(&backend, &IRadioBackend::capabilitiesChanged);

    check(maximum(backend) == 100, "unknown firmware: NRS up to 100");

    version = QStringLiteral("4.3.16.42034");
    check(maximum(backend) == 50, "4.3.16: NRS up to 50");
    backend.refreshFirmwareCapabilities();
    check(announced.count() == 1, "the new maximum is announced once");
    backend.refreshFirmwareCapabilities();
    check(announced.count() == 1, "an unchanged maximum is not announced again");

    commands.clear();
    backend.requestSliceDsp(0, level(SliceDspRequest::Feature::Nrs, 100));
    check(commands == QStringList{QStringLiteral("slice set 0 speex_nr_level=50")},
          "4.3.16: an NRS request of 100 is sent as 50");
    commands.clear();
    backend.requestSliceDsp(0, level(SliceDspRequest::Feature::Nrs, 17));
    check(commands == QStringList{QStringLiteral("slice set 0 speex_nr_level=17")},
          "4.3.16: an NRS request within the range is sent as it is");
    commands.clear();
    backend.requestSliceDsp(1, level(SliceDspRequest::Feature::Nrf, 100));
    check(commands == QStringList{QStringLiteral("slice set 1 nrf_level=100")},
          "4.3.16: other DSP levels keep 100");

    version = QStringLiteral("4.3.17");
    check(maximum(backend) == 50, "later firmware: 50");

    for (const QString& older : {QStringLiteral("4.2.20.41343"), QStringLiteral("4.3.15.99999"),
                                 QStringLiteral("1.4.0.0"), QStringLiteral("4.3.16.42034junk"),
                                 QStringLiteral("unknown"), QString()}) {
        version = older;
        commands.clear();
        backend.requestSliceDsp(0, level(SliceDspRequest::Feature::Nrs, 100));
        check(maximum(backend) == 100 && commands == QStringList{QStringLiteral("slice set 0 speex_nr_level=100")},
              qPrintable(QStringLiteral("older, protocol-only or malformed version \"%1\": 100").arg(older)));
    }
    backend.refreshFirmwareCapabilities();
    check(announced.count() == 2, "going back to 100 is announced");

    std::printf("\n%d NRS level range test(s) failed.\n", g_failed);
    return g_failed == 0 ? 0 : 1;
}
