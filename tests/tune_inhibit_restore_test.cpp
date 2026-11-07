// TUNE's PA inhibit restores only the outputs that TUNE itself turned off.
//
// Inhibit during TUNE turns a band's RCA TX1-3 / ACC TX output off for the
// tune carrier. The restore used to turn on every output whose preference was
// set, so an output already off on that band (no amplifier there) came back on
// after TUNE and could key an amplifier on a band the operator had excluded
// (upstream #6525, fixed there by #6526). This test pins, on RadioModel with a
// captured command plane:
//   - only an output that is on and has its preference set is turned off;
//   - the restore turns on exactly those, not an output that was already off;
//   - a preference changed during TUNE neither strands nor invents a write;
//   - a deliberate edit of an inhibited field during TUNE takes it over;
//   - a second start in the same cycle keeps what it holds and sends its
//     disables again (a refused TUNE must not leave the next one uninhibited),
//     a start on another band gives the old band back first, and a second
//     restore sends nothing.
//
// Socket-free. No radio, no RF.

#include "TestSettingsProfile.h"
#include "core/AppSettings.h"
#include "core/backends/flex/FlexBackend.h"
#include "core/backends/flex/RadioConnection.h"
#include "models/RadioModel.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <memory>

namespace AetherSDR {
// RadioModel and RadioConnection befriend a class of this name for socket-free
// tests; this is this file's definition of it.
class TxOperationIntegrationTestAccess {
public:
    static void useCommandPlane(RadioModel& radio, RadioConnection& connection,
                                QStringList& written)
    {
        connection.m_commandSinkForTest =
            [&written](quint32, const QString& command) { written << command; };
        radio.m_connection = &connection;
    }
    static void releaseCommandPlane(RadioModel& radio) { radio.m_connection = nullptr; }
    // One GEN band (no TX slice, so the TX frequency falls to GEN).
    static void setBand(RadioModel& radio, bool accTx, bool tx1, bool tx2, bool tx3)
    {
        RadioModel::TxBandInfo band;
        band.bandId = 24;
        band.bandName = QStringLiteral("GEN");
        band.accTx = accTx;
        band.tx1 = tx1;
        band.tx2 = tx2;
        band.tx3 = tx3;
        radio.m_txBandSettings.clear();
        radio.m_txBandSettings.insert(band.bandId, band);
    }
    static void addBand(RadioModel& radio, int id, const QString& name,
                        bool accTx, bool tx1, bool tx2, bool tx3)
    {
        RadioModel::TxBandInfo band;
        band.bandId = id;
        band.bandName = name;
        band.accTx = accTx;
        band.tx1 = tx1;
        band.tx2 = tx2;
        band.tx3 = tx3;
        radio.m_txBandSettings.insert(id, band);
    }
    static void clearBands(RadioModel& radio) { radio.m_txBandSettings.clear(); }
    static void apply(RadioModel& radio) { radio.applyTuneInhibit(); }
    static void restore(RadioModel& radio) { radio.restoreTuneInhibit(); }
};
} // namespace AetherSDR

using namespace AetherSDR;

namespace {
using Access = TxOperationIntegrationTestAccess;
int failures = 0;

void check(bool ok, const QString& message)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(message));
    failures += !ok;
}

void prefer(bool accTx, bool tx1, bool tx2, bool tx3)
{
    auto& s = AppSettings::instance();
    const auto b = [](bool on) { return on ? QStringLiteral("True") : QStringLiteral("False"); };
    s.setValue(QStringLiteral("TuneInhibitAccTx"), b(accTx));
    s.setValue(QStringLiteral("TuneInhibitTx1"), b(tx1));
    s.setValue(QStringLiteral("TuneInhibitTx2"), b(tx2));
    s.setValue(QStringLiteral("TuneInhibitTx3"), b(tx3));
}

QString got(const QStringList& written) { return written.join(QStringLiteral(" | ")); }

void run()
{
    RadioConnection connection;   // unopened: no socket, no thread
    QStringList written;
    RadioModel radio;
    Access::useCommandPlane(radio, connection, written);

    // 1. TX1 on, TX2 off on the band; all four preferences set.
    Access::setBand(radio, false, true, false, false);
    prefer(true, true, true, true);
    Access::apply(radio);
    check(written == QStringList{QStringLiteral("interlock bandset 24 tx1_enabled=0")},
          QStringLiteral("only the output that is on is turned off (got: %1)").arg(got(written)));
    written.clear();
    Access::restore(radio);
    check(written == QStringList{QStringLiteral("interlock bandset 24 tx1_enabled=1")},
          QStringLiteral("restore turns on TX1 only, not the TX2 / TX3 / ACC that were off (got: %1)")
              .arg(got(written)));

    // 2. A preference turned off during TUNE does not strand TX1 off.
    written.clear();
    Access::setBand(radio, false, true, true, false);
    prefer(false, true, true, false);
    Access::apply(radio);
    prefer(false, false, false, false);
    written.clear();
    Access::restore(radio);
    check(written == QStringList{QStringLiteral("interlock bandset 24 tx1_enabled=1"),
                                 QStringLiteral("interlock bandset 24 tx2_enabled=1")},
          QStringLiteral("a preference cleared during TUNE still restores what TUNE turned off (got: %1)")
              .arg(got(written)));

    // 3. A preference turned on during TUNE invents no restore.
    written.clear();
    Access::setBand(radio, true, true, true, true);
    prefer(false, true, false, false);
    Access::apply(radio);
    prefer(true, true, true, true);
    written.clear();
    Access::restore(radio);
    check(written == QStringList{QStringLiteral("interlock bandset 24 tx1_enabled=1")},
          QStringLiteral("a preference set during TUNE adds no write (got: %1)").arg(got(written)));

    // 4. A deliberate edit of an inhibited field during TUNE takes it over.
    written.clear();
    Access::setBand(radio, false, true, true, false);
    prefer(false, true, true, false);
    Access::apply(radio);
    radio.sendCommand(QStringLiteral("interlock bandset 24 tx2_enabled=0"));
    written.clear();
    Access::restore(radio);
    check(written == QStringList{QStringLiteral("interlock bandset 24 tx1_enabled=1")},
          QStringLiteral("the operator's TX2 edit during TUNE is left alone (got: %1)").arg(got(written)));

    // 5. A second start (the first TUNE refused, its end never came) keeps
    //    TX1 held and disables it again; a second restore sends nothing.
    written.clear();
    Access::setBand(radio, false, true, false, false);
    prefer(false, true, false, false);
    Access::apply(radio);
    Access::setBand(radio, false, false, false, false);   // the echo of our own write
    Access::apply(radio);
    check(written == QStringList{QStringLiteral("interlock bandset 24 tx1_enabled=0"),
                                 QStringLiteral("interlock bandset 24 tx1_enabled=0")},
          QStringLiteral("a second start in the cycle inhibits TX1 again (got: %1)").arg(got(written)));
    written.clear();
    Access::restore(radio);
    Access::restore(radio);
    check(written == QStringList{QStringLiteral("interlock bandset 24 tx1_enabled=1")},
          QStringLiteral("restore once, then nothing (got: %1)").arg(got(written)));

    // 5b. A start on another band gives the held band back first.
    written.clear();
    Access::setBand(radio, false, true, false, false);
    prefer(false, true, false, false);
    Access::apply(radio);
    // The TX frequency now maps to another band (another GEN row stands in
    // for it: no TX slice here, so the lookup takes the GEN row).
    Access::clearBands(radio);
    Access::addBand(radio, 9, QStringLiteral("GEN"), false, false, true, false);
    prefer(false, true, true, false);
    written.clear();
    Access::apply(radio);
    check(written == QStringList{QStringLiteral("interlock bandset 24 tx1_enabled=1"),
                                 QStringLiteral("interlock bandset 9 tx2_enabled=0")},
          QStringLiteral("another band: the held band's TX1 back, then the new band's TX2 off (got: %1)").arg(got(written)));
    written.clear();
    Access::restore(radio);
    check(written == QStringList{QStringLiteral("interlock bandset 9 tx2_enabled=1")},
          QStringLiteral("and the restore gives the new band's TX2 back (got: %1)").arg(got(written)));

    // 6. Nothing to inhibit: nothing written either way.
    written.clear();
    Access::setBand(radio, false, false, false, false);
    prefer(true, true, true, true);
    Access::apply(radio);
    Access::restore(radio);
    check(written.isEmpty(), QStringLiteral("all outputs off: no writes (got: %1)").arg(got(written)));

    Access::releaseCommandPlane(radio);
}
} // namespace

int main(int argc, char** argv)
{
    TestSettingsProfile profile(QStringLiteral("tune-inhibit-restore"));
    QCoreApplication app(argc, argv);
    run();
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
