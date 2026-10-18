#include "AudioSession.h"
#include "DevLink.h"
#include "EchoAudit.h"
#include "LogBuffer.h"
#include "MobileEngine.h"
#ifdef Q_OS_ANDROID
#include "../android/AndroidJni.h"
#endif

#include "core/AppSettings.h"
#include "core/QtAudioBackendGuard.h"
#include "models/Nr2SettingsModel.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLoggingCategory>
#include <QGuiApplication>
#include <QSysInfo>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <functional>

// Objects named `name` under `root`, including inside Loader-made items
// (those have no QObject parent, so findChildren() alone misses them):
// the visual tree is walked too.
static QList<QObject*> findNamedObjects(QObject* root, const QString& name)
{
    QList<QObject*> found = root->findChildren<QObject*>(name);
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
        if (item->objectName() == name && !found.contains(item)) {
            found << item;
        }
        for (QObject* child : item->children()) {
            if (child->objectName() == name && !found.contains(child)) {
                found << child;
            }
        }
        for (QQuickItem* child : item->childItems()) {
            walk(child);
        }
    };
    if (auto* window = qobject_cast<QQuickWindow*>(root)) {
        walk(window->contentItem());
    }
    return found;
}

#ifdef Q_OS_ANDROID
Q_LOGGING_CATEGORY(lcFonts, "aether.mobile.android")
// Qt falls back from Roboto only to a few Android fonts, so symbols the UI
// uses (☰ ⋯ ▾ ⌫ ● ✓) drew as boxes. The system's Noto symbol fonts carry
// them: register those files as fallbacks for common-script text.
static void addAndroidSymbolFallbacks()
{
    const QString symbols = QStringLiteral("\u2630\u22EF\u25BE\u232B\u25CF\u2713");
    QStringList families;
    const QDir dir(QStringLiteral("/system/fonts"));
    for (const QString& file : dir.entryList({QStringLiteral("*Symbols*.ttf"), QStringLiteral("*Symbols*.otf")})) {
        const int id = QFontDatabase::addApplicationFont(dir.filePath(file));
        for (const QString& family : QFontDatabase::applicationFontFamilies(id)) {
            if (!families.contains(family)) {
                families << family;
                QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Common, family);
            }
        }
    }
    QString missing;
    for (const QChar c : symbols) {
        bool found = false;
        for (const QString& family : families) {
            found = found || QFontMetrics(QFont(family)).inFont(c);
        }
        if (!found) {
            missing += c;
        }
    }
    qCInfo(lcFonts).noquote() << "symbol fonts:" << families.join(QStringLiteral(", "))
                      << "missing:" << (missing.isEmpty() ? QStringLiteral("none") : missing);
}
#endif

int main(int argc, char* argv[])
{
    AetherSDR::QtAudioBackendGuard::applyAtStartup();

    QGuiApplication app(argc, argv);
    AetherSDR::mobile::LogBuffer::install();
#ifdef Q_OS_ANDROID
    addAndroidSymbolFallbacks();
    AetherSDR::mobile::androidStartup();
#endif
    // Before any audio device opens: play-and-record session (iOS).
    AetherSDR::mobile::configureAudioSession();
    // Settings and config paths derive from these names; the mobile client
    // keeps its own settings, apart from a desktop install on the same host.
    // They stay as they are after the rename to OksaSDR, so settings and
    // voice macros carry over.
    QGuiApplication::setApplicationName(QStringLiteral("AetherSDR-Mobile"));
    QGuiApplication::setOrganizationName(QStringLiteral("AetherSDR"));
    QGuiApplication::setApplicationVersion(QStringLiteral(AETHERSDR_VERSION));
    qInfo().noquote() << QStringLiteral("OksaSDR %1 (build %2)")
                             .arg(QStringLiteral(AETHERSDR_VERSION), QStringLiteral(AETHER_MOBILE_BUILD_NUMBER));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption demoOption(QStringLiteral("demo"),
        QStringLiteral("Connect to the built-in demo radio as soon as it is listed."));
    parser.addOption(demoOption);
    const QCommandLineOption connectOption(QStringLiteral("connect"),
        QStringLiteral("Connect to the FlexRadio at <address> on start."), QStringLiteral("address"));
    parser.addOption(connectOption);
    const QCommandLineOption modeOption(QStringLiteral("mode"),
        QStringLiteral("With --demo: set the demo slice to <mode> after connecting (screenshots)."),
        QStringLiteral("mode"));
    parser.addOption(modeOption);
    const QCommandLineOption controlsOption(QStringLiteral("controls"),
        QStringLiteral("Control panel: visible or hidden (remembered), or drawer to also open the drawer (screenshots)."),
        QStringLiteral("state"));
    parser.addOption(controlsOption);
    const QCommandLineOption controlsTabOption(QStringLiteral("controls-tab"),
        QStringLiteral("Controls tab to show (0 DSP, 1 Audio, 2 TX, 3 CW, 4 View, 5 Profiles, 6 Memory); on a "
                       "tablet, the one section pinned for this run."), QStringLiteral("index"));
    parser.addOption(controlsTabOption);
    const QCommandLineOption controlsScrollOption(QStringLiteral("controls-scroll"),
        QStringLiteral("With --controls drawer: scroll the shown tab down by <px> (screenshots)."), QStringLiteral("px"));
    parser.addOption(controlsScrollOption);
    const QCommandLineOption reconnectTestOption(QStringLiteral("reconnect-test"),
        QStringLiteral("With --connect: disconnect after 8 s and reconnect at 11 s."));
    parser.addOption(reconnectTestOption);
    const QCommandLineOption frequencyDialogOption(QStringLiteral("frequency-dialog"),
        QStringLiteral("Open the frequency entry dialog (screenshots)."));
    parser.addOption(frequencyDialogOption);
    const QCommandLineOption meterChooserOption(QStringLiteral("popup"),
        QStringLiteral("Open a chooser popup by object name, e.g. meterChooser or modeChooser (screenshots)."),
        QStringLiteral("name"));
    parser.addOption(meterChooserOption);
    const QCommandLineOption previewNoSliceOption(QStringLiteral("preview-no-slice"),
        QStringLiteral("Show the no-slice notice over the display (screenshots)."));
    parser.addOption(previewNoSliceOption);
    const QCommandLineOption dvkTestOption(QStringLiteral("dvk-record-test"),
        QStringLiteral("Development: record 3 s into this empty voice keyer slot, listen to it, then clear it "
                       "(no transmission)."), QStringLiteral("slot"));
    parser.addOption(dvkTestOption);
    const QCommandLineOption tuneTestOption(QStringLiteral("tune-test"),
        QStringLiteral("Development: key TUNE (the radio's tune power) 10 s after start for <ms>, then unkey. "
                       "Transmits: only with the operator's consent."), QStringLiteral("ms"));
    parser.addOption(tuneTestOption);
    const QCommandLineOption txChainOption(QStringLiteral("tx-chain"),
        QStringLiteral("Development: set this device's TX processing, e.g. compOn=1,compThreshold=-30 "
                       "(remembered)."), QStringLiteral("settings"));
    parser.addOption(txChainOption);
    const QCommandLineOption cwxTestOption(QStringLiteral("cwx-test"),
        QStringLiteral("Development: send <text> as CWX at 10 W on 14.045 MHz CW (only if this app's slice "
                       "transmits), logging the local sidetone; power, mode and frequency restored after."),
        QStringLiteral("text"));
    parser.addOption(cwxTestOption);
    const QCommandLineOption buttonStyleOption(QStringLiteral("button-style"),
        QStringLiteral("Button colours: classic, light or lit (remembered)."), QStringLiteral("style"));
    parser.addOption(buttonStyleOption);
    const QCommandLineOption filterEdgesOption(QStringLiteral("filter-edges-test"),
        QStringLiteral("Development, receive only: set the filter to <low>,<high> Hz, log the radio's edges, then "
                       "put the old ones back."), QStringLiteral("low,high"));
    parser.addOption(filterEdgesOption);
    const QCommandLineOption spotFilterOption(QStringLiteral("spot-filter"),
        QStringLiteral("Spot filter <modes>,<max age min>, e.g. cw,0 (remembered)."), QStringLiteral("filter"));
    parser.addOption(spotFilterOption);
    const QCommandLineOption radioCommandsOption(QStringLiteral("radio-commands"),
        QStringLiteral("Development: send these radio commands (separated by ';') 7 s after start and log the "
                       "replies."), QStringLiteral("commands"));
    parser.addOption(radioCommandsOption);
    const QCommandLineOption setModeOption(QStringLiteral("set-mode"),
        QStringLiteral("Development: set this app's slice to <mode> on a real radio once connected "
                       "(--mode touches only the demo)."), QStringLiteral("mode"));
    parser.addOption(setModeOption);
    const QCommandLineOption cwPaddlesOption(QStringLiteral("cw-paddles"),
        QStringLiteral("CW keying on the screen: on (paddles) or off (straight key) (remembered)."), QStringLiteral("state"));
    parser.addOption(cwPaddlesOption);
    const QCommandLineOption micGainOption(QStringLiteral("mic-gain-db"),
        QStringLiteral("This device's microphone gain in dB, -20 .. +30 (remembered)."), QStringLiteral("dB"));
    parser.addOption(micGainOption);
    const QCommandLineOption micLevelTestOption(QStringLiteral("mic-level-test"),
        QStringLiteral("Development: a 2 s PTT burst at 10 W on 14.250 MHz with this device's microphone and "
                       "the radio's mic level at <level>, logging forward power; restored after."),
        QStringLiteral("level"));
    parser.addOption(micLevelTestOption);
    const QCommandLineOption pttToggleTestOption(QStringLiteral("ptt-toggle-test"),
        QStringLiteral("Development: PTT in tap on/off mode at 10 W on 14.250 MHz: tap, 2 s, tap; logs the "
                       "latch, power and mode restored after."));
    parser.addOption(pttToggleTestOption);
    const QCommandLineOption cwDecodeTestOption(QStringLiteral("cw-decode-test"),
        QStringLiteral("Development, receive only: CW on <MHz> (e.g. the 14.100 beacons) with the decoder on for "
                       "60 s, logging what it decodes; mode, frequency and decoder restored after."),
        QStringLiteral("MHz"));
    parser.addOption(cwDecodeTestOption);
    const QCommandLineOption paddleTestOption(QStringLiteral("paddle-test"),
        QStringLiteral("Development: paddles on 14.045 MHz CW at 10 W (only if this app's slice transmits): dit "
                       "0.6 s, dah 0.6 s, both 0.8 s; logs the elements; power, mode, frequency restored after."));
    parser.addOption(paddleTestOption);
    const QCommandLineOption cqTestOption(QStringLiteral("cq-test"),
        QStringLiteral("Development, with the operator's permission: CQ with <call> on the quietest of five 20 m "
                       "CW frequencies at 25 W and 22 WPM after QRL?, then the decoder listens 60 s; stops on high "
                       "SWR; power, speed, mode, frequency and decoder restored after."), QStringLiteral("call"));
    parser.addOption(cqTestOption);
    const QCommandLineOption paddleTestModeOption(QStringLiteral("paddle-test-mode"),
        QStringLiteral("With --paddle-test: the radio's iambic mode A or B for the test (restored after)."),
        QStringLiteral("mode"));
    parser.addOption(paddleTestModeOption);
    const QCommandLineOption keyTestOption(QStringLiteral("key-test"),
        QStringLiteral("Development: the straight key held 1 s on 14.045 MHz CW at 10 W (only if this app's slice "
                       "transmits); power, mode, frequency restored after."));
    parser.addOption(keyTestOption);
    const QCommandLineOption pttTestOption(QStringLiteral("ptt-test"),
        QStringLiteral("Development: transmit for <ms> with this device's microphone at 10 W on 14.250 MHz "
                       "(only if this app's slice transmits), logging the Mic meter; power restored after."),
        QStringLiteral("ms"));
    parser.addOption(pttTestOption);
    const QCommandLineOption panDisplayOption(QStringLiteral("pan-display"),
        QStringLiteral("Development: 12 s after start set this app's pan display setting <key>=<value> "
                       "(average, fps, wfRate)."), QStringLiteral("setting"));
    parser.addOption(panDisplayOption);
    const QCommandLineOption echoAuditOption(QStringLiteral("echo-audit"),
        QStringLiteral("Development: change each radio setting a little, check the radio's status brings it "
                       "back, restore it, then quit (no keying; VOX, antennas, MTU, callsign left out)."));
    parser.addOption(echoAuditOption);
    const QCommandLineOption memoryEditTestOption(QStringLiteral("memory-edit-test"),
        QStringLiteral("Development: save a memory \"OksaSDR test\", edit its name, frequency and mode, "
                       "log what the radio holds, remove it, then quit (other memories untouched)."));
    parser.addOption(memoryEditTestOption);
    const QCommandLineOption spotTestOption(QStringLiteral("spot-test"),
        QStringLiteral("Development: add (three test spots near the slice on the radio, 5 min) or clear them."),
        QStringLiteral("action"));
    parser.addOption(spotTestOption);
    const QCommandLineOption dxClusterOption(QStringLiteral("dx-cluster"),
        QStringLiteral("Development: set the DX cluster to <host>:<port>,<callsign> (remembered) and "
                       "connect 5 s after start; with no callsign only set it (the radio's is then used)."),
        QStringLiteral("server"));
    parser.addOption(dxClusterOption);
    const QCommandLineOption tnfTestOption(QStringLiteral("tnf-test"),
        QStringLiteral("Development: add (a notch at the passband centre, 200 Hz, deep) or clear "
                       "(remove every notch on the radio)."), QStringLiteral("action"));
    parser.addOption(tnfTestOption);
    const QCommandLineOption spectrum3dOption(QStringLiteral("spectrum3d"),
        QStringLiteral("2D waterfall or 3D stacked trace: on or off (remembered)."), QStringLiteral("state"));
    parser.addOption(spectrum3dOption);
    const QCommandLineOption analogMetersOption(QStringLiteral("analog-meters"),
        QStringLiteral("Meter faces: on (analog) or off (bars) (remembered)."), QStringLiteral("state"));
    parser.addOption(analogMetersOption);
    const QCommandLineOption metersOption(QStringLiteral("meters"),
        QStringLiteral("Receive meter choices <left>,<right>, e.g. S,Volts (remembered)."), QStringLiteral("kinds"));
    parser.addOption(metersOption);
    const QCommandLineOption txMetersOption(QStringLiteral("tx-meters"),
        QStringLiteral("Transmit meter choices <left>,<right>, e.g. SWR,Power (remembered)."), QStringLiteral("kinds"));
    parser.addOption(txMetersOption);
    const QCommandLineOption devLinkOption(QStringLiteral("devlink"),
        QStringLiteral("Developer link to <host>, or off (remembered)."), QStringLiteral("host"));
    parser.addOption(devLinkOption);
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
        QStringLiteral("Save the window to <file> after <ms> (default 8000), muted, then quit."),
        QStringLiteral("file"));
    const QCommandLineOption screenshotDelayOption(QStringLiteral("screenshot-delay"),
        QStringLiteral("Delay before --screenshot, in milliseconds."), QStringLiteral("ms"),
        QStringLiteral("8000"));
    parser.addOption(screenshotOption);
    parser.addOption(screenshotDelayOption);
    const QCommandLineOption sizeOption(QStringLiteral("size"),
        QStringLiteral("Window size in points, e.g. 1024x768, to check other iPad layouts."),
        QStringLiteral("WxH"));
    parser.addOption(sizeOption);
    parser.process(app);

    // Settings load before any model or the AudioEngine is constructed.
    AetherSDR::AppSettings::instance().load();
    static_cast<void>(AetherSDR::Nr2SettingsModel::instance());
    // The radio shows a station name in every client's station list. iOS
    // gives apps no device name (the host name reads "localhost"), so name
    // the station after the app unless the user set one (the app's former
    // default counts as unset).
    {
        const QString station =
            AetherSDR::AppSettings::instance().value(QStringLiteral("StationName")).toString();
        if ((station.isEmpty() || station == QLatin1String("AetherSDR-Mobile"))
            && QSysInfo::machineHostName() == QLatin1String("localhost")) {
            AetherSDR::AppSettings::instance().setValue(QStringLiteral("StationName"), QStringLiteral("OksaSDR"));
        }
    }
    AetherSDR::AppSettings::instance().initializeGuiClientIdentity();

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    AetherSDR::mobile::MobileEngine engine;

    if (parser.isSet(controlsOption)) {
        engine.setControlsVisible(parser.value(controlsOption) == QLatin1String("visible"));
    }

    QQmlApplicationEngine qml;
    QObject::connect(&qml, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    qml.loadFromModule("AetherMobile", "Main");
    if (parser.isSet(devLinkOption)) {
        const QString host = parser.value(devLinkOption);
        if (host == QLatin1String("off")) {
            AetherSDR::mobile::DevLink::instance()->setEnabled(false);
        } else {
            AetherSDR::mobile::DevLink::instance()->setHost(host);
            AetherSDR::mobile::DevLink::instance()->setEnabled(true);
        }
    }
    AetherSDR::mobile::DevLink::instance()->attach(&engine, &qml);

    // Frames drawn, for the performance read-out.
    if (!qml.rootObjects().isEmpty()) {
        if (auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().constFirst())) {
            QObject::connect(window, &QQuickWindow::frameSwapped, &engine,
                             [&engine] { engine.countFrame(); }, Qt::DirectConnection);
        }
    }
    if (parser.isSet(sizeOption) && !qml.rootObjects().isEmpty()) {
        const QStringList wh = parser.value(sizeOption).split(QLatin1Char('x'));
        if (auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().constFirst()); window && wh.size() == 2) {
            window->resize(wh.at(0).toInt(), wh.at(1).toInt());
        }
    }

    if (parser.isSet(demoOption)) {
        engine.connectToDemoWhenListed();
        if (parser.isSet(modeOption)) {
            const QString mode = parser.value(modeOption);
            QObject::connect(&engine, &AetherSDR::mobile::MobileEngine::modesChanged, &engine,
                             [&engine, mode] { engine.setMode(mode); });
        }
    } else if (parser.isSet(connectOption)) {
        const QString address = parser.value(connectOption);
        engine.connectToAddress(address);
        if (parser.isSet(reconnectTestOption)) {
            // Disconnect at 8 s and connect again at 11 s (screenshot later).
            QTimer::singleShot(8000, &engine, [&engine] { engine.disconnectRadio(); });
            QTimer::singleShot(11000, &engine, [&engine, address] { engine.connectToAddress(address); });
        }
    }

    if (parser.isSet(metersOption)) {
        const QStringList kinds = parser.value(metersOption).split(QLatin1Char(','));
        if (kinds.size() == 2) {
            engine.setMeterLeft(kinds.at(0));
            engine.setMeterRight(kinds.at(1));
        }
    }
    if (parser.isSet(txMetersOption)) {
        const QStringList kinds = parser.value(txMetersOption).split(QLatin1Char(','));
        if (kinds.size() == 2) {
            engine.setMeter(QStringLiteral("left"), true, kinds.at(0));
            engine.setMeter(QStringLiteral("right"), true, kinds.at(1));
        }
    }
    if (parser.isSet(analogMetersOption)) {
        engine.setAnalogMeters(parser.value(analogMetersOption) == QLatin1String("on"));
    }
    if (parser.isSet(spectrum3dOption)) {
        engine.setSpectrum3D(parser.value(spectrum3dOption) == QLatin1String("on"));
    }
    if (parser.isSet(frequencyDialogOption)) {
        QTimer::singleShot(6000, &app, [&qml] {
            for (QObject* root : qml.rootObjects()) {
                if (QObject* dialog = root->findChild<QObject*>(QStringLiteral("frequencyDialog"))) {
                    QMetaObject::invokeMethod(dialog, "open");
                }
            }
        });
    }
    if (parser.isSet(dvkTestOption)) {
        const int slot = parser.value(dvkTestOption).toInt();
        QTimer::singleShot(10000, &app, [&engine, slot] {
            const QVariantList dvkList = engine.dvkSlots();
            if (slot < 1 || slot > dvkList.size() || dvkList.at(slot - 1).toMap().value(QStringLiteral("seconds")).toDouble() > 0) {
                qWarning() << "dvk-record-test: slot" << slot << "is not empty; not touching it";
                return;
            }
            engine.dvkRecord(slot);
            QTimer::singleShot(3000, &engine, [&engine, slot] { engine.dvkRecord(slot); });
            QTimer::singleShot(5000, &engine, [&engine, slot] {
                qInfo() << "dvk-record-test: slot after recording" << engine.dvkSlots().at(slot - 1);
                engine.dvkPreview(slot);
            });
            QTimer::singleShot(9000, &engine, [&engine, slot] {
                engine.dvkStop();
                engine.dvkClear(slot);
            });
            QTimer::singleShot(11000, &engine, [&engine, slot] {
                qInfo() << "dvk-record-test: slot after clear" << engine.dvkSlots().at(slot - 1);
            });
        });
    }
    if (parser.isSet(echoAuditOption)) {
        auto* audit = new AetherSDR::mobile::EchoAudit(engine, &app);
        QObject::connect(audit, &AetherSDR::mobile::EchoAudit::finished, &app, &QCoreApplication::quit);
        QTimer::singleShot(14000, audit, &AetherSDR::mobile::EchoAudit::start);
    }
    if (parser.isSet(panDisplayOption)) {
        const QStringList kv = parser.value(panDisplayOption).split(QLatin1Char('='));
        if (kv.size() == 2) {
            QTimer::singleShot(12000, &app, [&engine, kv] {
                qInfo() << "pan-display test: before" << engine.panDisplay();
                engine.setPanDisplay(kv.at(0), kv.at(1).toInt());
                QTimer::singleShot(3000, &engine, [&engine] {
                    qInfo() << "pan-display test: after" << engine.panDisplay();
                });
            });
        }
    }
    if (parser.isSet(memoryEditTestOption)) {
        auto findTest = [&engine](const QString& name) {
            for (const QVariant& v : engine.memories()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("name")).toString() == name) {
                    return m;
                }
            }
            return QVariantMap();
        };
        QTimer::singleShot(8000, &app, [&engine] { engine.saveMemory(QStringLiteral("OksaSDR test")); });
        QTimer::singleShot(11000, &app, [&engine, findTest] {
            const QVariantMap m = findTest(QStringLiteral("OksaSDR test"));
            qInfo() << "memory-edit-test: saved" << m;
            if (!m.isEmpty()) {
                engine.editMemory(m.value(QStringLiteral("index")).toInt(), QStringLiteral("OksaSDR edited"), 14.2345,
                                  QStringLiteral("CW"));
            }
        });
        QTimer::singleShot(14000, &app, [&engine, findTest] {
            const QVariantMap m = findTest(QStringLiteral("OksaSDR edited"));
            qInfo() << "memory-edit-test: edited" << m;
            const QVariantMap left = m.isEmpty() ? findTest(QStringLiteral("OksaSDR test")) : m;
            if (!left.isEmpty()) {
                engine.removeMemory(left.value(QStringLiteral("index")).toInt());
            }
        });
        QTimer::singleShot(17000, &app, [&engine] {
            qInfo() << "memory-edit-test: left" << engine.memories().size() << "memories";
            QCoreApplication::quit();
        });
    }
    if (parser.isSet(tuneTestOption)) {
        const int ms = std::clamp(parser.value(tuneTestOption).toInt(), 500, 5000);
        // A clear frequency first (never the 14.100 beacon), and only when
        // this app's own slice is the one that transmits.
        QTimer::singleShot(8000, &app, [&engine] { engine.setFrequencyMhz(14.250); });
        QTimer::singleShot(10000, &app, [&engine, ms] {
            bool ownTx = false;
            for (const QVariant& v : engine.slices()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == engine.activeSliceId()) {
                    ownTx = m.value(QStringLiteral("tx")).toBool();
                }
            }
            if (!ownTx || std::abs(engine.frequencyMhz() - 14.250) > 0.0005) {
                qWarning() << "tune-test: not keying (own slice transmits:" << ownTx
                           << "frequency:" << engine.frequencyMhz() << ")";
                return;
            }
            if (!engine.tuning()) {
                qInfo() << "tune-test: TUNE on for" << ms << "ms";
                engine.toggleTune();
            }
            QTimer::singleShot(ms, &engine, [&engine] {
                if (engine.tuning()) {
                    engine.toggleTune();
                }
                qInfo() << "tune-test: TUNE off";
            });
        });
    }
    if (parser.isSet(txChainOption)) {
        for (const QString& kv : parser.value(txChainOption).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QStringList p = kv.split(QLatin1Char('='));
            if (p.size() == 2) {
                engine.setTxChain(p.at(0).trimmed(), p.at(1).trimmed().toDouble());
            }
        }
        qInfo() << "tx-chain:" << engine.txChain();
    }
    if (parser.isSet(dxClusterOption)) {
        const QStringList parts = parser.value(dxClusterOption).split(QLatin1Char(','));
        const QStringList hostPort = parts.value(0).split(QLatin1Char(':'));
        engine.setDxCluster(QStringLiteral("host"), hostPort.value(0));
        engine.setDxCluster(QStringLiteral("port"), hostPort.value(1, QStringLiteral("7300")).toInt());
        engine.setDxCluster(QStringLiteral("callsign"), parts.value(1));
        // Without a callsign only the settings change (e.g. back to defaults).
        if (!parts.value(1).isEmpty()) {
            QTimer::singleShot(5000, &app, [&engine] { engine.connectDxCluster(); });
        }
        QTimer::singleShot(9000, &app, [&engine] { qInfo() << "dx-cluster:" << engine.dxCluster(); });
    }
    if (parser.isSet(spotTestOption)) {
        const QString action = parser.value(spotTestOption);
        QTimer::singleShot(5000, &app, [&engine, action] { engine.spotTest(action); });
        QTimer::singleShot(7000, &app, [&engine] { qInfo() << "spot-test: spots" << engine.spots().size(); });
    }
    if (parser.value(tnfTestOption) == QLatin1String("add")) {
        QTimer::singleShot(5000, &app, [&engine] { engine.addTnf(0.0); });
        QTimer::singleShot(6000, &app, [&engine] {
            const QVariantList list = engine.tnfs();
            if (!list.isEmpty()) {
                const int id = list.last().toMap().value(QStringLiteral("id")).toInt();
                engine.setTnf(id, QStringLiteral("width"), 200);
                engine.setTnf(id, QStringLiteral("depth"), 2);
            }
            qInfo() << "tnf-test: notches" << list;
        });
    } else if (parser.value(tnfTestOption) == QLatin1String("clear")) {
        QTimer::singleShot(5000, &app, [&engine] {
            for (const QVariant& v : engine.tnfs()) {
                engine.removeTnf(v.toMap().value(QStringLiteral("id")).toInt());
            }
        });
        QTimer::singleShot(7000, &app, [&engine] { qInfo() << "tnf-test: left" << engine.tnfs(); });
    }
    if (parser.isSet(cwxTestOption)) {
        const QString text = parser.value(cwxTestOption);
        auto* saved = new QVariantMap;
        QTimer::singleShot(8000, &app, [&engine, saved] {
            (*saved)[QStringLiteral("freq")] = engine.frequencyMhz();
            (*saved)[QStringLiteral("mode")] = engine.mode();
            (*saved)[QStringLiteral("decoder")] = engine.cwDecoderEnabled();
            engine.setMode(QStringLiteral("CW"));
            engine.setFrequencyMhz(14.045);
            engine.setCwDecoderEnabled(true);
        });
        QTimer::singleShot(11000, &app, [&engine, saved, text] {
            bool ownTx = false;
            for (const QVariant& v : engine.slices()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == engine.activeSliceId()) {
                    ownTx = m.value(QStringLiteral("tx")).toBool();
                }
            }
            if (!ownTx || engine.mode() != QLatin1String("CW") || std::abs(engine.frequencyMhz() - 14.045) > 0.0005) {
                qWarning() << "cwx-test: not sending (own TX:" << ownTx << engine.mode() << engine.frequencyMhz() << ")";
                return;
            }
            (*saved)[QStringLiteral("power")] = engine.rfPower();
            engine.setRfPower(10);
            QTimer::singleShot(700, &engine, [&engine, text] {
                qInfo() << "cwx-test: sending" << text << "at" << engine.cwSpeed() << "WPM";
                engine.sendCwx(text);
            });
            auto* sampler = new QTimer(&engine);
            QObject::connect(sampler, &QTimer::timeout, &engine, [&engine] {
                qInfo().noquote() << QStringLiteral("cwx-test: tx=%1 power=%2 W sidetone %3")
                                         .arg(engine.radioTransmitting()).arg(engine.forwardPowerW(), 0, 'f', 1)
                                         .arg(engine.cwxSidetoneStats());
            });
            sampler->start(500);
            QTimer::singleShot(14000, &engine, [&engine, saved, sampler] {
                sampler->stop();
                engine.stopCwx();
                qInfo().noquote() << "cwx-test: decoder" << engine.cwDecoderCounts() << "|"
                                  << engine.cwDecodedText().right(200).simplified();
                engine.setRfPower(saved->value(QStringLiteral("power")).toInt());
                engine.setCwDecoderEnabled(saved->value(QStringLiteral("decoder")).toBool());
                engine.setMode(saved->value(QStringLiteral("mode")).toString());
                engine.setFrequencyMhz(saved->value(QStringLiteral("freq")).toDouble());
                qInfo() << "cwx-test: restored" << *saved << "sidetone" << engine.cwxSidetoneStats();
            });
        });
    }
    if (parser.isSet(buttonStyleOption)) {
        engine.setButtonStyle(parser.value(buttonStyleOption));
    }
    if (parser.isSet(filterEdgesOption)) {
        const QStringList e = parser.value(filterEdgesOption).split(QLatin1Char(','));
        auto* old = new QPair<int, int>;
        QTimer::singleShot(8000, &app, [&engine, e, old] {
            *old = {engine.filterLow(), engine.filterHigh()};
            qInfo() << "filter-edges-test: was" << old->first << old->second;
            engine.setFilterEdges(e.value(0).toInt(), e.value(1).toInt());
        });
        QTimer::singleShot(10000, &app, [&engine] {
            qInfo() << "filter-edges-test: radio now" << engine.filterLow() << engine.filterHigh();
        });
        QTimer::singleShot(11000, &app, [&engine, old] { engine.setFilterEdges(old->first, old->second); });
        QTimer::singleShot(13000, &app, [&engine] {
            qInfo() << "filter-edges-test: restored" << engine.filterLow() << engine.filterHigh();
        });
    }
    if (parser.isSet(spotFilterOption)) {
        const QStringList f = parser.value(spotFilterOption).split(QLatin1Char(','));
        engine.setSpotModes(f.value(0, QStringLiteral("all")));
        engine.setSpotMaxAgeMin(f.value(1, QStringLiteral("0")).toInt());
    }
    if (parser.isSet(radioCommandsOption)) {
        const QStringList commands = parser.value(radioCommandsOption).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        QTimer::singleShot(7000, &app, [&engine, commands] {
            for (const QString& c : commands) {
                engine.devCommand(c.trimmed());
            }
        });
    }
    if (parser.isSet(setModeOption)) {
        const QString mode = parser.value(setModeOption);
        QTimer::singleShot(6000, &app, [&engine, mode] { engine.setMode(mode); });
    }
    if (parser.isSet(cwPaddlesOption)) {
        engine.setCwPaddles(parser.value(cwPaddlesOption) == QLatin1String("on"));
    }
    if (parser.isSet(micGainOption)) {
        engine.setMicGainDb(parser.value(micGainOption).toDouble());
    }
    if (parser.isSet(micLevelTestOption)) {
        const int testLevel = std::clamp(parser.value(micLevelTestOption).toInt(), 0, 100);
        QTimer::singleShot(8000, &app, [&engine] { engine.setFrequencyMhz(14.250); });
        QTimer::singleShot(10000, &app, [&engine, testLevel] {
            bool ownTx = false;
            for (const QVariant& v : engine.slices()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == engine.activeSliceId()) {
                    ownTx = m.value(QStringLiteral("tx")).toBool();
                }
            }
            if (!ownTx || std::abs(engine.frequencyMhz() - 14.250) > 0.0005 || engine.mode() != QLatin1String("USB")) {
                qWarning() << "mic-level-test: not keying";
                return;
            }
            const int power = engine.rfPower();
            const int micLevel = engine.radioMicLevel();
            engine.setRfPower(10);
            auto* sum = new QVariantMap;
            auto* sampler = new QTimer(&engine);
            QObject::connect(sampler, &QTimer::timeout, &engine, [&engine, sum] {
                if (engine.radioTransmitting()) {
                    const QString k = QString::number(engine.radioMicLevel());
                    (*sum)[k + "n"] = sum->value(k + "n").toInt() + 1;
                    (*sum)[k + "w"] = sum->value(k + "w").toDouble() + engine.forwardPowerW();
                    (*sum)[k + "mic"] = engine.micLevelDbfs();
                }
            });
            sampler->start(100);
            auto burst = [&engine](int level, int at) {
                QTimer::singleShot(at, &engine, [&engine, level] { engine.setRadioMicLevel(level); });
                QTimer::singleShot(at + 800, &engine, [&engine] { engine.setPtt(true, QStringLiteral("mic-level-test")); });
                QTimer::singleShot(at + 2800, &engine, [&engine] { engine.setPtt(false, QStringLiteral("mic-level-test")); });
            };
            burst(testLevel, 500);
            QTimer::singleShot(4500, &engine, [&engine, sampler, sum, power, micLevel, testLevel] {
                sampler->stop();
                {
                    const QString k = QString::number(testLevel);
                    const int n = sum->value(k + "n").toInt();
                    qInfo().noquote() << QStringLiteral("mic-level-test: radio mic level %1: average %2 W over %3 samples, Mic meter %4 dB")
                                             .arg(k).arg(n ? sum->value(k + "w").toDouble() / n : 0.0, 0, 'f', 2).arg(n)
                                             .arg(sum->value(k + "mic").toDouble(), 0, 'f', 1);
                }
                engine.setRadioMicLevel(micLevel);
                engine.setRfPower(power);
                qInfo() << "mic-level-test: restored mic level" << micLevel << "power" << power;
            });
        });
    }
    if (parser.isSet(pttToggleTestOption)) {
        QTimer::singleShot(8000, &app, [&engine] { engine.setFrequencyMhz(14.250); });
        QTimer::singleShot(10000, &app, [&engine] {
            const int power = engine.rfPower();
            const bool wasToggle = engine.pttToggle();
            engine.setRfPower(10);
            engine.setPttToggle(true);
            auto log = [&engine](const char* what) {
                qInfo().noquote() << QStringLiteral("ptt-toggle-test: %1: latched=%2 tx=%3")
                                         .arg(QLatin1String(what)).arg(engine.pttLatched()).arg(engine.radioTransmitting());
            };
            QTimer::singleShot(500, &engine, [&engine, log] { engine.togglePtt(); log("tapped on"); });
            QTimer::singleShot(2000, &engine, [log] { log("1.5 s later"); });
            QTimer::singleShot(2500, &engine, [&engine, log] { engine.togglePtt(); log("tapped off"); });
            QTimer::singleShot(3500, &engine, [&engine, log, power, wasToggle] {
                log("1 s later");
                engine.setPttToggle(wasToggle);
                engine.setRfPower(power);
                qInfo() << "ptt-toggle-test: restored power" << power << "toggle" << wasToggle;
            });
        });
    }
    if (parser.isSet(cwDecodeTestOption)) {
        const double mhz = parser.value(cwDecodeTestOption).toDouble();
        auto* saved = new QVariantMap;
        QTimer::singleShot(8000, &app, [&engine, saved, mhz] {
            (*saved)[QStringLiteral("freq")] = engine.frequencyMhz();
            (*saved)[QStringLiteral("mode")] = engine.mode();
            (*saved)[QStringLiteral("decoder")] = engine.cwDecoderEnabled();
            engine.setMode(QStringLiteral("CW"));
            engine.setFrequencyMhz(mhz);
            engine.setCwDecoderEnabled(true);
            qInfo() << "cw-decode-test: listening on" << mhz << "MHz CW";
        });
        for (int s = 20; s <= 70; s += 10) {
            QTimer::singleShot(8000 + s * 1000, &app, [&engine, s] {
                qInfo().noquote() << QStringLiteral("cw-decode-test %1 s: %2, sens %3, %4 | %5")
                                         .arg(s).arg(engine.cwDecoderStats()).arg(engine.cwDecoderSensitivity())
                                         .arg(engine.cwDecoderCounts(), engine.cwDecodedText().right(160).simplified());
            });
        }
        QTimer::singleShot(8000 + 71000, &app, [&engine, saved] {
            engine.setCwDecoderEnabled(saved->value(QStringLiteral("decoder")).toBool());
            engine.setMode(saved->value(QStringLiteral("mode")).toString());
            engine.setFrequencyMhz(saved->value(QStringLiteral("freq")).toDouble());
            qInfo() << "cw-decode-test: restored" << *saved;
        });
    }
    if (parser.isSet(paddleTestOption)) {
        auto* saved = new QVariantMap;
        (*saved)[QStringLiteral("testMode")] = parser.value(paddleTestModeOption).toUpper();
        QTimer::singleShot(8000, &app, [&engine, saved] {
            (*saved)[QStringLiteral("freq")] = engine.frequencyMhz();
            (*saved)[QStringLiteral("mode")] = engine.mode();
            engine.setMode(QStringLiteral("CW"));
            engine.setFrequencyMhz(14.045);
        });
        QTimer::singleShot(11000, &app, [&engine, saved] {
            bool ownTx = false;
            for (const QVariant& v : engine.slices()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == engine.activeSliceId()) {
                    ownTx = m.value(QStringLiteral("tx")).toBool();
                }
            }
            if (!ownTx || engine.mode() != QLatin1String("CW") || std::abs(engine.frequencyMhz() - 14.045) > 0.0005) {
                qWarning() << "paddle-test: not keying";
                return;
            }
            (*saved)[QStringLiteral("power")] = engine.rfPower();
            (*saved)[QStringLiteral("iambicMode")] = engine.radioSetup().value(QStringLiteral("iambicMode"));
            engine.setRfPower(10);
            if (!(*saved)[QStringLiteral("testMode")].toString().isEmpty()) {
                engine.setRadioSetup(QStringLiteral("iambicMode"),
                                     (*saved)[QStringLiteral("testMode")].toString() == QLatin1String("A") ? 0 : 1);
            }
            auto step = [&engine](int at, bool dit, bool dah, const char* what) {
                QTimer::singleShot(at, &engine, [&engine, dit, dah, what] {
                    engine.setCwPaddleState(dit, dah);
                    qInfo().noquote() << QStringLiteral("paddle-test: %1 (elements so far %2, %3 WPM)")
                                             .arg(QLatin1String(what)).arg(engine.paddleElements()).arg(engine.cwSpeed());
                });
            };
            step(700, true, false, "dit down");
            step(1300, false, false, "dit up");
            step(2300, false, true, "dah down");
            step(2900, false, false, "dah up");
            step(3900, true, true, "both down");
            step(4700, false, false, "both up");
            QTimer::singleShot(7000, &engine, [&engine, saved] {
                qInfo() << "paddle-test: elements" << engine.paddleElements() << "iambic mode was"
                        << saved->value(QStringLiteral("iambicMode")) << "test" << saved->value(QStringLiteral("testMode"));
                if (!saved->value(QStringLiteral("testMode")).toString().isEmpty()) {
                    engine.setRadioSetup(QStringLiteral("iambicMode"), saved->value(QStringLiteral("iambicMode")));
                }
                engine.setRfPower(saved->value(QStringLiteral("power")).toInt());
                engine.setMode(saved->value(QStringLiteral("mode")).toString());
                engine.setFrequencyMhz(saved->value(QStringLiteral("freq")).toDouble());
                qInfo() << "paddle-test: restored" << *saved;
            });
        });
    }
    if (parser.isSet(cqTestOption)) {
        const QString call = parser.value(cqTestOption).trimmed().toUpper();
        auto* st = new QVariantMap;
        auto* timer = new QTimer(&engine);
        static const double candidates[] = {14.032, 14.037, 14.043, 14.048, 14.053};
        QTimer::singleShot(8000, &app, [&engine, st, timer, call] {
            bool ownTx = false;
            for (const QVariant& v : engine.slices()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == engine.activeSliceId()) {
                    ownTx = m.value(QStringLiteral("tx")).toBool();
                }
            }
            if (!ownTx || call.isEmpty()) {
                qWarning() << "cq-test: not keying (own TX" << ownTx << ")";
                return;
            }
            (*st)[QStringLiteral("freq")] = engine.frequencyMhz();
            (*st)[QStringLiteral("mode")] = engine.mode();
            (*st)[QStringLiteral("power")] = engine.rfPower();
            (*st)[QStringLiteral("speed")] = engine.cwSpeed();
            (*st)[QStringLiteral("decoder")] = engine.cwDecoderEnabled();
            engine.setMode(QStringLiteral("CW"));
            engine.setCwSpeed(22);
            engine.setCwDecoderEnabled(true);
            // Scan: 2.5 s on each candidate, keep the loudest S-meter reading.
            (*st)[QStringLiteral("step")] = 0;
            (*st)[QStringLiteral("best")] = 0;
            (*st)[QStringLiteral("bestDbm")] = 0.0;
            (*st)[QStringLiteral("peak")] = -200.0;
            engine.setFrequencyMhz(candidates[0]);
            QObject::connect(timer, &QTimer::timeout, &engine, [&engine, st, timer, call] {
                const int step = (*st)[QStringLiteral("step")].toInt();
                (*st)[QStringLiteral("peak")] = std::max((*st)[QStringLiteral("peak")].toDouble(), engine.sMeterDbm());
                const int ticks = (*st)[QStringLiteral("ticks")].toInt() + 1;
                (*st)[QStringLiteral("ticks")] = ticks;
                if (step < 5 && ticks >= 10) {   // 2.5 s per candidate
                    const double peak = (*st)[QStringLiteral("peak")].toDouble();
                    qInfo().noquote() << QStringLiteral("cq-test: %1 MHz peak %2 dBm").arg(candidates[step], 0, 'f', 3).arg(peak, 0, 'f', 1);
                    if (step == 0 || peak < (*st)[QStringLiteral("bestDbm")].toDouble()) {
                        (*st)[QStringLiteral("best")] = step;
                        (*st)[QStringLiteral("bestDbm")] = peak;
                    }
                    (*st)[QStringLiteral("step")] = step + 1;
                    (*st)[QStringLiteral("ticks")] = 0;
                    (*st)[QStringLiteral("peak")] = -200.0;
                    if (step + 1 < 5) {
                        engine.setFrequencyMhz(candidates[step + 1]);
                    } else {
                        const double f = candidates[(*st)[QStringLiteral("best")].toInt()];
                        engine.setFrequencyMhz(f);
                        engine.setRfPower(25);
                        qInfo() << "cq-test: quietest" << f << "MHz; QRL?";
                        QTimer::singleShot(1500, &engine, [&engine] { engine.sendCwx(QStringLiteral("QRL?")); });
                        (*st)[QStringLiteral("phase")] = QStringLiteral("qrl");
                    }
                    return;
                }
                if (step < 5) {
                    return;
                }
                // Stop on a bad match whenever we are on the air.
                if (engine.radioTransmitting() && engine.swrValid() && engine.swr() > 2.5) {
                    qWarning() << "cq-test: SWR" << engine.swr() << "- stopping";
                    engine.stopCwx();
                }
                const QString phase = (*st)[QStringLiteral("phase")].toString();
                const int t = ticks;
                if (phase == QLatin1String("qrl") && t == 30) {   // ~4 s listening after QRL?
                    if (engine.sMeterDbm() > -95.0) {
                        qWarning() << "cq-test: frequency in use after QRL? (" << engine.sMeterDbm() << "dBm) - no CQ";
                        (*st)[QStringLiteral("phase")] = QStringLiteral("done");
                        (*st)[QStringLiteral("ticks")] = 0;
                        return;
                    }
                    const QString cq = QStringLiteral("CQ CQ CQ DE %1 %1 K").arg(call);
                    engine.sendCwx(cq + QLatin1Char(' ') + cq);
                    qInfo() << "cq-test: sending" << cq << "x2";
                    (*st)[QStringLiteral("phase")] = QStringLiteral("cq");
                    (*st)[QStringLiteral("ticks")] = 0;
                } else if (phase == QLatin1String("cq") && !engine.radioTransmitting() && t > 20) {
                    qInfo() << "cq-test: CQ done, listening 60 s";
                    (*st)[QStringLiteral("phase")] = QStringLiteral("listen");
                    (*st)[QStringLiteral("ticks")] = 0;
                } else if (phase == QLatin1String("listen") && t % 20 == 0) {
                    qInfo().noquote() << QStringLiteral("cq-test: %1 s, S %2 dBm, decoder %3 | %4")
                                             .arg(t / 4).arg(engine.sMeterDbm(), 0, 'f', 0).arg(engine.cwDecoderStats(),
                                                  engine.cwDecodedText().right(200).simplified());
                    if (t >= 240) {
                        (*st)[QStringLiteral("phase")] = QStringLiteral("done");
                        (*st)[QStringLiteral("ticks")] = 0;
                    }
                } else if (phase == QLatin1String("done") && t == 1) {
                    timer->stop();
                    engine.setRfPower((*st)[QStringLiteral("power")].toInt());
                    engine.setCwSpeed((*st)[QStringLiteral("speed")].toInt());
                    engine.setCwDecoderEnabled((*st)[QStringLiteral("decoder")].toBool());
                    engine.setMode((*st)[QStringLiteral("mode")].toString());
                    engine.setFrequencyMhz((*st)[QStringLiteral("freq")].toDouble());
                    qInfo() << "cq-test: restored power" << (*st)[QStringLiteral("power")] << "speed" << (*st)[QStringLiteral("speed")];
                    QTimer::singleShot(1500, qApp, &QCoreApplication::quit);
                }
            });
            timer->start(250);
        });
    }
    if (parser.isSet(keyTestOption)) {
        auto* saved = new QVariantMap;
        QTimer::singleShot(8000, &app, [&engine, saved] {
            (*saved)[QStringLiteral("freq")] = engine.frequencyMhz();
            (*saved)[QStringLiteral("mode")] = engine.mode();
            engine.setMode(QStringLiteral("CW"));
            engine.setFrequencyMhz(14.045);
        });
        QTimer::singleShot(11000, &app, [&engine, saved] {
            bool ownTx = false;
            for (const QVariant& v : engine.slices()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == engine.activeSliceId()) {
                    ownTx = m.value(QStringLiteral("tx")).toBool();
                }
            }
            if (!ownTx || engine.mode() != QLatin1String("CW") || std::abs(engine.frequencyMhz() - 14.045) > 0.0005) {
                qWarning() << "key-test: not keying";
                return;
            }
            (*saved)[QStringLiteral("power")] = engine.rfPower();
            engine.setRfPower(10);
            QTimer::singleShot(700, &engine, [&engine] { engine.setCwKey(true); qInfo() << "key-test: key down" << engine.cwKeyDown(); });
            QTimer::singleShot(1200, &engine, [&engine] {
                qInfo() << "key-test: held, tx" << engine.radioTransmitting() << "power" << engine.forwardPowerW();
            });
            QTimer::singleShot(1700, &engine, [&engine] { engine.setCwKey(false, QStringLiteral("key-test")); qInfo() << "key-test: key up"; });
            QTimer::singleShot(3500, &engine, [&engine, saved] {
                engine.setRfPower(saved->value(QStringLiteral("power")).toInt());
                engine.setMode(saved->value(QStringLiteral("mode")).toString());
                engine.setFrequencyMhz(saved->value(QStringLiteral("freq")).toDouble());
                qInfo() << "key-test: restored" << *saved;
            });
        });
    }
    if (parser.isSet(pttTestOption)) {
        const int ms = std::clamp(parser.value(pttTestOption).toInt(), 500, 5000);
        QTimer::singleShot(8000, &app, [&engine] { engine.setFrequencyMhz(14.250); });
        QTimer::singleShot(10000, &app, [&engine, ms] {
            bool ownTx = false;
            for (const QVariant& v : engine.slices()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == engine.activeSliceId()) {
                    ownTx = m.value(QStringLiteral("tx")).toBool();
                }
            }
            if (!ownTx || std::abs(engine.frequencyMhz() - 14.250) > 0.0005) {
                qWarning() << "ptt-test: not keying (own slice transmits:" << ownTx
                           << "frequency:" << engine.frequencyMhz() << ")";
                return;
            }
            const int power = engine.rfPower();
            engine.setRfPower(10);
            auto* sampler = new QTimer(&engine);
            QObject::connect(sampler, &QTimer::timeout, &engine, [&engine] {
                qInfo().noquote() << QStringLiteral("ptt-test: tx=%1 micLive=%2 mic=%3 dB peak=%4 dB power=%5 W comp=%6 dB")
                                         .arg(engine.radioTransmitting()).arg(engine.micMeterLive())
                                         .arg(engine.micLevelDbfs(), 0, 'f', 1).arg(engine.micPeakDbfs(), 0, 'f', 1)
                                         .arg(engine.forwardPowerW(), 0, 'f', 1)
                                         .arg(engine.compReductionDb(), 0, 'f', 1);
            });
            QTimer::singleShot(1000, &engine, [&engine, sampler, ms, power] {
                qInfo() << "ptt-test: PTT on for" << ms << "ms";
                sampler->start(250);
                engine.setPtt(true, QStringLiteral("ptt-test"));
                QTimer::singleShot(ms, &engine, [&engine, sampler, power] {
                    engine.setPtt(false, QStringLiteral("ptt-test"));
                    qInfo() << "ptt-test: PTT off";
                    QTimer::singleShot(1500, &engine, [&engine, sampler, power] {
                        sampler->stop();
                        engine.setRfPower(power);
                        qInfo() << "ptt-test: power back to" << power;
                    });
                });
            });
        });
    }
    if (parser.isSet(previewNoSliceOption)) {
        QTimer::singleShot(6000, &app, [&qml] {
            for (QObject* root : qml.rootObjects()) {
                for (QObject* notice : root->findChildren<QObject*>(QStringLiteral("noSliceNotice"))) {
                    notice->setProperty("preview", true);
                }
            }
        });
    }
    if (parser.isSet(meterChooserOption)) {
        const QString name = parser.value(meterChooserOption);
        // After --controls drawer (6 s) has shown its tab.
        QTimer::singleShot(7000, &app, [&qml, name] {
            for (QObject* root : qml.rootObjects()) {
                // Every match: a page can hold a hidden twin (e.g. the info
                // panel's one-line copy); only the visible one shows.
                for (QObject* chooser : findNamedObjects(root, name)) {
                    const auto* host = qvariant_cast<QQuickItem*>(chooser->property("parent"));
                    if (!host || host->isVisible()) {
                        // A dialog that needs content offers preview() with sample data.
                        const bool hasPreview = chooser->metaObject()->indexOfMethod("preview()") >= 0;
                        QMetaObject::invokeMethod(chooser, hasPreview ? "preview" : "open");
                    }
                }
            }
        });
    }
    if (parser.isSet(controlsScrollOption)) {
        const qreal px = parser.value(controlsScrollOption).toDouble();
        QTimer::singleShot(7000, &app, [&qml, px] {
            for (QObject* root : qml.rootObjects()) {
                for (QObject* scroll : findNamedObjects(root, QStringLiteral("controlsTabScroll"))) {
                    auto* item = qobject_cast<QQuickItem*>(scroll);
                    auto* flick = qvariant_cast<QObject*>(scroll->property("contentItem"));
                    if (item && item->isVisible() && flick) {
                        const qreal room = flick->property("contentHeight").toReal() - item->height();
                        flick->setProperty("contentY", std::clamp(px, 0.0, std::max(0.0, room)));
                    }
                }
            }
        });
    }
    if (parser.value(controlsOption) == QLatin1String("drawer")) {
        const QString tab = parser.value(controlsTabOption);
        QTimer::singleShot(6000, &app, [&qml, tab] {
            for (QObject* root : qml.rootObjects()) {
                if (QObject* drawer = root->findChild<QObject*>(QStringLiteral("controlsDrawer"))) {
                    QMetaObject::invokeMethod(drawer, "open");
                }
                if (!tab.isEmpty()) {
                    for (QObject* tabs : root->findChildren<QObject*>(QStringLiteral("controlsTabs")))
                        tabs->setProperty("currentIndex", tab.toInt());
                }
            }
        });
    }

    // The tablet's side panel shows pinned sections, not tabs: pin only that
    // one for this run and put the user's choice back on exit.
    if (parser.value(controlsOption) != QLatin1String("drawer") && parser.isSet(controlsTabOption)) {
        static const QStringList names{QStringLiteral("DSP"), QStringLiteral("Audio"), QStringLiteral("TX"),
            QStringLiteral("CW"), QStringLiteral("View"), QStringLiteral("Profiles"), QStringLiteral("Memory")};
        const QString name = names.value(parser.value(controlsTabOption).toInt());
        if (!name.isEmpty()) {
            const QStringList saved = engine.pinnedSections();
            engine.setPinnedSections({name});
            QObject::connect(&app, &QCoreApplication::aboutToQuit, &engine,
                             [&engine, saved] { engine.setPinnedSections(saved); });
        }
    }

    if (parser.isSet(screenshotOption)) {
        const QString path = parser.value(screenshotOption);
        // Screenshots run against a real radio: change nothing on it.
        engine.setMicDefaultEnabled(false);
        engine.setRedactIdentity(true);
        QObject::connect(&engine, &AetherSDR::mobile::MobileEngine::connectionStateChanged, &engine,
                         [&engine] { engine.setMuted(true); });
        QTimer::singleShot(parser.value(screenshotDelayOption).toInt(), &app, [&qml, path] {
            auto* window = qml.rootObjects().isEmpty()
                ? nullptr : qobject_cast<QQuickWindow*>(qml.rootObjects().constFirst());
            const bool saved = window && window->grabWindow().save(path);
            QCoreApplication::exit(saved ? 0 : 2);
        });
    }

    return app.exec();
}
