#include "AudioSession.h"
#include "DevLink.h"
#include "EchoAudit.h"
#include "LogBuffer.h"
#include "MobileEngine.h"
#include "Ft8Engine.h"
#include "TrackpadScrollPhases.h"
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
#include <QThread>
#include <qpa/qwindowsysteminterface.h>
#include <QDateTime>
#include <QGuiApplication>
#include <QSysInfo>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>

// Objects named `name` under `root`, including inside Loader-made items
// (those have no QObject parent, so findChildren() alone misses them):
// the visual tree is walked too.
// Development (--wheel-test): which objects a wheel event visits.
class WheelTrace : public QObject {
public:
    bool on = false;
    bool eventFilter(QObject* receiver, QEvent* event) override
    {
        if (on && event->type() == QEvent::Wheel) {
            qInfo().noquote() << "wheel-test: trace" << receiver->metaObject()->className() << receiver->objectName()
                              << "accepted" << event->isAccepted();
        }
        return false;
    }
};

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
    // iPadOS reports every cancelled pointer hover (trackpad, Pencil) as an
    // "Unknown hover state" line: noise in the operator's log.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.input.mouse=false"));
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
                       "tablet, the sections pinned for this run (e.g. 0,2,3)."), QStringLiteral("index"));
    parser.addOption(controlsTabOption);
    const QCommandLineOption controlsScrollOption(QStringLiteral("controls-scroll"),
        QStringLiteral("With --controls drawer: scroll the shown tab down by <px> (screenshots), "
                       "<ms> after start (default 7000)."), QStringLiteral("px[,ms]"));
    parser.addOption(controlsScrollOption);
    const QCommandLineOption wheelTestOption(QStringLiteral("wheel-test"),
        QStringLiteral("Development: send trackpad scrolls across the shown controls and log where they do not "
                       "scroll the panel, and which item took them."));
    parser.addOption(wheelTestOption);
    const QCommandLineOption zoomTestOption(QStringLiteral("zoom-test"),
        QStringLiteral("Development: zoom the pan in <n> times by half, then out <n> times by two, 9 s after "
                       "start, logging the span after each (a real radio; the span ends where it began)."),
        QStringLiteral("n"));
    parser.addOption(zoomTestOption);
    const QCommandLineOption ft8TestOption(QStringLiteral("ft8-test"),
        QStringLiteral("Development: 8 s after start tune to 20 m FT8 (or FT4 with ft4), decode for <s> seconds "
                       "and log the decodes; with cq also call CQ twice at 10 W. Frequency, mode and power are "
                       "restored, then the app quits. E.g. 120 or 120,cq or 60,ft4."),
        QStringLiteral("spec"));
    parser.addOption(ft8TestOption);
    const QCommandLineOption tuneToOption(QStringLiteral("tune-to"),
        QStringLiteral("Development: 7 s after start tune the slice to <mhz,mode> (the pan follows), e.g. 1.9,USB."),
        QStringLiteral("mhz,mode"));
    parser.addOption(tuneToOption);
    const QCommandLineOption digiFilterOption(QStringLiteral("digi-filter"),
        QStringLiteral("Development: the FT8 / FT4 decode list filter for this run (all, cq, new, dxcc; it is saved)."),
        QStringLiteral("filter"));
    parser.addOption(digiFilterOption);
    const QCommandLineOption onlineLogTestOption(QStringLiteral("online-log-test"),
        QStringLiteral("Development: set the Wavelog / Cloudlog logbook to <url,key,station>, log a test QSO "
                       "(call TEST1) 4 s later and log the logbook's state; an empty url at the end clears it."),
        QStringLiteral("url,key,station"));
    parser.addOption(onlineLogTestOption);
    const QCommandLineOption pskReporterOption(QStringLiteral("psk-reporter"),
        QStringLiteral("Development: turn PSK Reporter on with <grid> as My grid (both saved; AETHER_PSKREPORTER_TEST=1 "
                       "sends to the checking port, every minute)."), QStringLiteral("grid"));
    parser.addOption(pskReporterOption);
    const QCommandLineOption previewTxOption(QStringLiteral("preview-tx"),
        QStringLiteral("Development: show the meters as while transmitting, nothing keyed (screenshots)."));
    parser.addOption(previewTxOption);
    const QCommandLineOption digiTxLevelOption(QStringLiteral("digi-tx-level"),
        QStringLiteral("Development: the FT8 / FT4 TX level in dB (-20 to 0; it is saved)."), QStringLiteral("db"));
    parser.addOption(digiTxLevelOption);
    const QCommandLineOption tapAtOption(QStringLiteral("tap-at"),
        QStringLiteral("Development: a mouse click at <x,y,ms>: window fractions 0-1, ms after start (e.g. 0.6,0.5,20000)."),
        QStringLiteral("x,y,ms"));
    parser.addOption(tapAtOption);
    const QCommandLineOption dragAtOption(QStringLiteral("drag-at"),
        QStringLiteral("Development: a mouse drag <x1,y,x2,ms>: window fractions, ms after start."),
        QStringLiteral("x1,y,x2,ms"));
    parser.addOption(dragAtOption);
    const QCommandLineOption adifImportOption(QStringLiteral("adif-import"),
        QStringLiteral("Development: import the ADIF <file> into the log at start, log the result and quit."),
        QStringLiteral("file"));
    parser.addOption(adifImportOption);
    const QCommandLineOption radeTestOption(QStringLiteral("rade-test"),
        QStringLiteral("Development: 8 s after start tune to <mhz> (default 14.236), turn RADE on for <s> seconds "
                       "and log its sync, SNR and callsigns; then RADE off and the slice restored. E.g. 180,14.236."),
        QStringLiteral("s,mhz"));
    parser.addOption(radeTestOption);
    const QCommandLineOption wheelIosTestOption(QStringLiteral("wheel-test-ios"),
        QStringLiteral("Development: scroll the shown controls from the top as iOS's trackpad does (wheel events "
                       "with pixel and angle deltas, no scroll phase) and log how far they get."));
    parser.addOption(wheelIosTestOption);
    const QCommandLineOption trackpadPhasesOption(QStringLiteral("trackpad-phases"),
        QStringLiteral("Development: give phase-less pixel wheel events trackpad phases, as on iOS (always on there)."));
    parser.addOption(trackpadPhasesOption);
    const QCommandLineOption reconnectTestOption(QStringLiteral("reconnect-test"),
        QStringLiteral("With --connect: disconnect after 8 s and reconnect at 11 s."));
    parser.addOption(reconnectTestOption);
    const QCommandLineOption frequencyDialogOption(QStringLiteral("frequency-dialog"),
        QStringLiteral("Open the frequency entry dialog (screenshots)."));
    parser.addOption(frequencyDialogOption);
    const QCommandLineOption meterChooserOption(QStringLiteral("popup"),
        QStringLiteral("Open a chooser popup by object name, e.g. meterChooser or modeChooser (screenshots); "
                       "name:N also sets its section, e.g. radioSetup:5."),
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
    const QCommandLineOption setStepOption(QStringLiteral("set-step"),
        QStringLiteral("Development: set the tuning step to <hz> 8 s after start, as the STEP chooser does."),
        QStringLiteral("hz"));
    parser.addOption(setStepOption);
    const QCommandLineOption selectBandOption(QStringLiteral("select-band"),
        QStringLiteral("Development: press this band button 9 s after start (a real radio), e.g. 20m or a "
                       "transverter's name; the band list goes to the log."), QStringLiteral("band"));
    parser.addOption(selectBandOption);
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
    const QCommandLineOption repeaterTestOption(QStringLiteral("repeater-test"),
        QStringLiteral("Development: FM on <MHz>, repeater <down|up> <offset MHz> from the TX tab's setters, "
                       "then TUNE for <ms> (0: no TUNE) so the transmitted carrier shows where TX went, e.g. "
                       "29.650:down:0.05:1500. Transmits: only with the operator's consent."),
        QStringLiteral("spec"));
    parser.addOption(repeaterTestOption);
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
        QStringLiteral("Development: save a memory \"OksaSDR test\", edit it into an FM repeater memory "
                       "(29.620, -0.1 MHz, CTCSS 88.5), recall it and log the slice's TX offset, put the slice "
                       "back, remove the memory, then quit (other memories untouched; nothing transmits)."));
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
    const QCommandLineOption meterStyleOption(QStringLiteral("meter-style"),
        QStringLiteral("Meter styles <left>,<right>: analog, bar or two (two bars), e.g. two,analog (remembered)."),
        QStringLiteral("styles"));
    parser.addOption(meterStyleOption);
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
    // iOS trackpad scrolling (Magic Keyboard, an iPad app on a Mac) gets the
    // phases Flickable needs to follow the fingers (TrackpadScrollPhases.h).
#ifdef Q_OS_IOS
    const bool trackpadPhases = true;
#else
    const bool trackpadPhases = parser.isSet(trackpadPhasesOption);
#endif
    if (trackpadPhases && !qml.rootObjects().isEmpty()) {
        if (auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().constFirst())) {
            new AetherSDR::mobile::TrackpadScrollPhases(window);
        }
    }
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
    if (parser.isSet(meterStyleOption)) {
        const QStringList styles = parser.value(meterStyleOption).split(QLatin1Char(','));
        if (styles.size() == 2) {
            engine.setMeterStyle(QStringLiteral("left"), styles.at(0));
            engine.setMeterStyle(QStringLiteral("right"), styles.at(1));
        }
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
        // The slice as it was, put back before the memory is removed.
        auto* before = new QVariantMap;
        QTimer::singleShot(7500, &app, [&engine, before] {
            *before = {{QStringLiteral("mode"), engine.mode()}, {QStringLiteral("mhz"), engine.frequencyMhz()}};
        });
        QTimer::singleShot(8000, &app, [&engine] { engine.saveMemory(QStringLiteral("OksaSDR test")); });
        // An FM repeater memory: 29.620 MHz, shift down 0.1 MHz, CTCSS 88.5 Hz.
        QTimer::singleShot(11000, &app, [&engine, findTest] {
            const QVariantMap m = findTest(QStringLiteral("OksaSDR test"));
            qInfo() << "memory-edit-test: saved" << m;
            if (!m.isEmpty()) {
                engine.editMemory(m.value(QStringLiteral("index")).toInt(), QStringLiteral("OksaSDR edited"), 29.62,
                                  QStringLiteral("FM"), QStringLiteral("down"), 0.1, 1, QStringLiteral("88.5"));
            }
        });
        QTimer::singleShot(14000, &app, [&engine, findTest] {
            const QVariantMap m = findTest(QStringLiteral("OksaSDR edited"));
            qInfo() << "memory-edit-test: edited" << m;
            if (!m.isEmpty()) {
                engine.recallMemory(m.value(QStringLiteral("index")).toInt());
            }
        });
        QTimer::singleShot(17000, &app, [&engine] {
            qInfo() << "memory-edit-test: recalled: RX" << engine.frequencyMhz() << engine.mode() << "dir"
                    << engine.repeaterDir() << "offset" << engine.repeaterOffsetMhz() << "TX offset"
                    << engine.txOffsetMhz() << "CTCSS" << engine.ctcssOn() << engine.ctcssTone();
            // Straight to the radio: the slice model may not have the recalled
            // offset yet, and a setter that sees no change sends nothing.
            engine.devCommand(QStringLiteral("slice set %1 repeater_offset_dir=simplex fm_repeater_offset_freq=0 "
                                             "tx_offset_freq=0 fm_tone_mode=off").arg(engine.activeSliceId()));
        });
        QTimer::singleShot(18500, &app, [&engine, findTest, before] {
            engine.setMode(before->value(QStringLiteral("mode")).toString());
            engine.setFrequencyMhz(before->value(QStringLiteral("mhz")).toDouble());
            QVariantMap m = findTest(QStringLiteral("OksaSDR edited"));
            if (m.isEmpty()) {
                m = findTest(QStringLiteral("OksaSDR test"));
            }
            if (!m.isEmpty()) {
                engine.removeMemory(m.value(QStringLiteral("index")).toInt());
            }
            qInfo() << "memory-edit-test: restored" << *before << "TX offset" << engine.txOffsetMhz();
            delete before;
        });
        QTimer::singleShot(21000, &app, [&engine] {
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
    if (parser.isSet(setStepOption)) {
        const int hz = parser.value(setStepOption).toInt();
        QTimer::singleShot(8000, &app, [&engine, hz] { engine.setStepHz(hz); });
    }
    if (parser.isSet(selectBandOption)) {
        const QString band = parser.value(selectBandOption);
        QTimer::singleShot(9000, &app, [&engine, band] {
            qInfo().noquote() << "select-band: bands" << engine.bands().join(QLatin1Char(' ')) << "-> pressing" << band;
            engine.selectBand(band);
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
    if (parser.isSet(repeaterTestOption)) {
        const QStringList spec = parser.value(repeaterTestOption).split(QLatin1Char(':'));
        const double rxMhz = spec.value(0).toDouble();
        const QString dir = spec.value(1, QStringLiteral("down"));
        const double offsetMhz = spec.value(2).toDouble();
        const int tuneMs = std::clamp(spec.value(3).toInt(), 0, 3000);
        // What the slice had, put back at the end: a TX offset left behind
        // would move the next SSB transmission too.
        auto* before = new QVariantMap;
        QTimer::singleShot(7500, &app, [&engine, before] {
            *before = {{QStringLiteral("mode"), engine.mode()}, {QStringLiteral("mhz"), engine.frequencyMhz()},
                       {QStringLiteral("dir"), engine.repeaterDir()},
                       {QStringLiteral("offset"), engine.repeaterOffsetMhz()}};
            qInfo() << "repeater-test: before" << *before;
        });
        QTimer::singleShot(12000 + tuneMs + 2000, &app, [&engine, before] {
            engine.setRepeaterOffsetMhz(before->value(QStringLiteral("offset")).toDouble());
            engine.setRepeaterDir(before->value(QStringLiteral("dir")).toString());
            engine.setMode(before->value(QStringLiteral("mode")).toString());
            engine.setFrequencyMhz(before->value(QStringLiteral("mhz")).toDouble());
            qInfo() << "repeater-test: restored" << *before << "TX offset" << engine.txOffsetMhz();
            delete before;
        });
        QTimer::singleShot(8000, &app, [&engine, rxMhz] {
            engine.setMode(QStringLiteral("FM"));
            engine.setFrequencyMhz(rxMhz);
        });
        QTimer::singleShot(10000, &app, [&engine, dir, offsetMhz] {
            engine.setRepeaterDir(dir);
            engine.setRepeaterOffsetMhz(offsetMhz);
        });
        QTimer::singleShot(12000, &app, [&engine, rxMhz, tuneMs] {
            qInfo() << "repeater-test: RX" << engine.frequencyMhz() << "mode" << engine.mode() << "dir"
                    << engine.repeaterDir() << "offset" << engine.repeaterOffsetMhz() << "TX offset"
                    << engine.txOffsetMhz() << "-> TX" << engine.frequencyMhz() + engine.txOffsetMhz();
            bool ownTx = false;
            for (const QVariant& v : engine.slices()) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("id")).toInt() == engine.activeSliceId()) {
                    ownTx = m.value(QStringLiteral("tx")).toBool();
                }
            }
            if (tuneMs <= 0 || !ownTx || std::abs(engine.frequencyMhz() - rxMhz) > 0.0005) {
                qWarning() << "repeater-test: not keying (own slice transmits:" << ownTx << ")";
                return;
            }
            qInfo() << "repeater-test: TUNE on for" << tuneMs << "ms";
            engine.toggleTune();
            QTimer::singleShot(tuneMs, &engine, [&engine] {
                if (engine.tuning()) {
                    engine.toggleTune();
                }
                qInfo() << "repeater-test: TUNE off";
            });
        });
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
        const QStringList popupArgs = parser.value(meterChooserOption).split(QLatin1Char(':'));
        const QString name = popupArgs.value(0);
        const int section = popupArgs.value(1, QStringLiteral("-1")).toInt();
        // After --controls drawer (6 s) has shown its tab.
        QTimer::singleShot(7000, &app, [&qml, name, section] {
            for (QObject* root : qml.rootObjects()) {
                // Every match: a page can hold a hidden twin (e.g. the info
                // panel's one-line copy); only the visible one shows.
                for (QObject* chooser : findNamedObjects(root, name)) {
                    const auto* host = qvariant_cast<QQuickItem*>(chooser->property("parent"));
                    if (!host || host->isVisible()) {
                        // A dialog that needs content offers preview() with sample data.
                        const bool hasPreview = chooser->metaObject()->indexOfMethod("preview()") >= 0;
                        QMetaObject::invokeMethod(chooser, hasPreview ? "preview" : "open");
                        if (section >= 0) {
                            chooser->setProperty("section", section);
                        }
                    }
                }
            }
        });
    }
    if (parser.isSet(wheelIosTestOption)) {
        QTimer::singleShot(9000, &app, [&qml] {
            for (QObject* root : qml.rootObjects()) {
                auto* window = qobject_cast<QQuickWindow*>(root);
                if (!window) {
                    continue;
                }
                for (QObject* scroll : findNamedObjects(root, QStringLiteral("controlsTabScroll"))
                                           + findNamedObjects(root, QStringLiteral("pinnedScroll"))) {
                    auto* item = qobject_cast<QQuickItem*>(scroll);
                    auto* flick = qvariant_cast<QQuickItem*>(scroll->property("contentItem"));
                    if (!item || !item->isVisible() || !flick) {
                        continue;
                    }
                    flick->setProperty("contentY", 0.0);
                    const qreal room = flick->property("contentHeight").toReal() - item->height();
                    // quiview.mm handleScroll: pixelDelta = the pan's step,
                    // angleDelta = 2 x it, no phase, the cursor where the
                    // gesture began (here the panel's centre).
                    const QPointF scene = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
                    const QPointF global = window->mapToGlobal(scene);
                    ulong stamp = 5000;
                    const int kEvents = qEnvironmentVariableIntValue("WHEEL_EVENTS") > 0
                        ? qEnvironmentVariableIntValue("WHEEL_EVENTS") : 240;
                    QStringList trace;
                    for (int i = 1; i <= kEvents; ++i) {
                        QWheelEvent ev(scene, global, QPoint(0, -12), QPoint(0, -24), Qt::NoButton, Qt::NoModifier,
                                       Qt::NoScrollPhase, false);
                        ev.setTimestamp(stamp += 16);
                        QCoreApplication::sendEvent(window, &ev);
                        QEventLoop loop;
                        QTimer::singleShot(16, &loop, &QEventLoop::quit);
                        loop.exec();
                        if (i % (kEvents / 8) == 0) {
                            trace << QString::number(flick->property("contentY").toReal(), 'f', 0);
                        }
                    }
                    QEventLoop settle;
                    QTimer::singleShot(1500, &settle, &QEventLoop::quit);
                    settle.exec();
                    qInfo().noquote() << "wheel-test-ios:" << scroll->objectName() << "room" << room
                                      << "contentY every 30 events" << trace.join(QLatin1Char(' '))
                                      << "final" << flick->property("contentY").toReal()
                                      << "(" << kEvents * 12 << "px scrolled)";
                }
            }
        });
    }
    if (parser.isSet(zoomTestOption)) {
        const int n = std::clamp(parser.value(zoomTestOption).toInt(), 1, 20);
        for (int i = 0; i < 2 * n; ++i) {
            QTimer::singleShot(9000 + i * 700, &app, [&engine, i, n] {
                engine.zoom(i < n ? 0.5 : 2.0);
            });
            QTimer::singleShot(9000 + i * 700 + 600, &app, [&engine, i] {
                qInfo() << "zoom-test: step" << i << "span" << engine.panBandwidthMhz() * 1e3 << "kHz";
            });
        }
    }
    if (parser.isSet(ft8TestOption) && parser.value(ft8TestOption).contains(QStringLiteral("selftest"))) {
        // Our own transmit audio through our own decoder, no radio.
        for (const bool ft4 : {false, true}) {
            Ft8Engine ft8;
            ft8.setProtocol(ft4 ? Ft8Engine::Protocol::Ft4 : Ft8Engine::Protocol::Ft8);
            const QStringList messages{QStringLiteral("CQ OH6OKSA"), QStringLiteral("<K1ABC> OH6OKSA"),
                                       QStringLiteral("K1ABC <OH6OKSA> -12"), QStringLiteral("K1ABC <OH6OKSA> R-09"),
                                       QStringLiteral("<K1ABC> OH6OKSA RR73"), QStringLiteral("<K1ABC> OH6OKSA 73"),
                                       QStringLiteral("CQ OH2XO KP20"), QStringLiteral("K1ABC OH2XO KP20"),
                                       QStringLiteral("<SN9Q/QRP> OH6OKSA RR73"), QStringLiteral("<SN9Q/QRP> OH6OKSA")};
            for (const QString& m : messages) {
                const QByteArray audio = ft8.encode(m, 1500.0);
                const auto* f = reinterpret_cast<const float*>(audio.constData());
                const int n = int(audio.size() / sizeof(float));
                std::vector<float> s12(size_t((ft4 ? 6.5 : 14.0) * 12000), 0.0f);
                for (int i = 0; i + 1 < n && size_t(6000 + i / 2) < s12.size(); i += 2) {
                    s12[size_t(6000 + i / 2)] = f[i];
                }
                const auto decodes = ft8.decodeSamples(s12, 0);
                qInfo().noquote() << "ft8-selftest" << (ft4 ? "FT4" : "FT8") << m << "->"
                                  << (decodes.isEmpty() ? QStringLiteral("(nothing)") : decodes.first().text);
            }
        }
        {
            // More hashed calls than the table holds (it once hung when full),
            // then one of the newest still resolves.
            Ft8Engine ft8;
            QElapsedTimer timer;
            timer.start();
            for (int i = 0; i < 3000; ++i) {
                const QString call = QStringLiteral("OH%1%2").arg(i % 10).arg(QString::number(10000 + i, 36).toUpper());
                ft8.encode(QStringLiteral("<%1> OH6OKSA").arg(call), 1500.0);
            }
            const QByteArray audio = ft8.encode(QStringLiteral("K1ABC <OH2%1> -05").arg(QString::number(12999, 36).toUpper()), 1500.0);
            const auto* f = reinterpret_cast<const float*>(audio.constData());
            std::vector<float> s12(size_t(14.0 * 12000), 0.0f);
            for (int i = 0; i + 1 < int(audio.size() / sizeof(float)) && size_t(6000 + i / 2) < s12.size(); i += 2) {
                s12[size_t(6000 + i / 2)] = f[i];
            }
            const auto decodes = ft8.decodeSamples(s12, 0);
            qInfo().noquote() << "ft8-selftest hash table: 3000 calls in" << timer.elapsed() << "ms, then"
                              << (decodes.isEmpty() ? QStringLiteral("(nothing)") : decodes.first().text);
        }
        return 0;
    }
    if (parser.isSet(ft8TestOption) && parser.value(ft8TestOption).contains(QStringLiteral("keepcq"))) {
        // Keep CQ and the CQ slot without RF: dry transmissions, a station
        // answering our CQ fed in by hand, then CQ must start again.
        engine.setDigiDryRun(true);
        engine.setDigiAutoCq(true);
        engine.setDigiCqParity(QStringLiteral("odd"));
        QTimer::singleShot(9000, &app, [&engine] {
            engine.setDigiRunning(true);
            engine.digiCallCq();
            qInfo() << "keepcq-test: CQ called, slot" << engine.digi().value(QStringLiteral("parity")).toString();
        });
        const auto feed = [&engine](const QString& text) {
            engine.digiInjectDecodes({QVariantMap{{QStringLiteral("utc"), QStringLiteral("000000")},
                                                  {QStringLiteral("snr"), -7}, {QStringLiteral("dt"), QStringLiteral("0.1")},
                                                  {QStringLiteral("freq"), 1234}, {QStringLiteral("text"), text},
                                                  {QStringLiteral("hl"), QString()}}});
            qInfo().noquote() << "keepcq-test: fed" << text << "->" << engine.digi().value(QStringLiteral("status")).toString()
                              << "| next" << engine.digi().value(QStringLiteral("txMsg")).toString()
                              << "| rx" << engine.digi().value(QStringLiteral("rxHz")).toInt();
        };
        // nonstd: a nonstandard call answers (no reports between two of them).
        const bool nonstd = parser.value(ft8TestOption).contains(QStringLiteral("nonstd"));
        QTimer::singleShot(40000, &app, [feed, nonstd] {
            feed(nonstd ? QStringLiteral("<OH6OKSA> SN9Q/QRP") : QStringLiteral("OH6OKSA K1ABC FN42"));
        });
        if (!nonstd) {
            QTimer::singleShot(70000, &app, [feed] { feed(QStringLiteral("OH6OKSA K1ABC R-10")); });
        }
        QTimer::singleShot(110000, &app, [&app, &engine] {
            const QVariantMap d = engine.digi();
            qInfo().noquote() << "keepcq-test: end state" << d.value(QStringLiteral("state")).toString() << "|"
                              << d.value(QStringLiteral("status")).toString() << "| next" << d.value(QStringLiteral("txMsg")).toString();
            engine.digiHalt();
            engine.setDigiAutoCq(false);
            engine.setDigiCqParity(QStringLiteral("auto"));
            engine.setDigiRunning(false);
            QTimer::singleShot(500, &app, &QCoreApplication::quit);
        });
    } else if (parser.isSet(ft8TestOption)) {
        const QStringList spec = parser.value(ft8TestOption).split(QLatin1Char(','));
        const int seconds = std::clamp(spec.value(0).toInt(), 20, 1800);
        const bool answer = spec.contains(QStringLiteral("answer"));
        const bool qso = spec.contains(QStringLiteral("qso"));
        const bool cq = qso || spec.contains(QStringLiteral("cq"));
        const bool ft4 = spec.contains(QStringLiteral("ft4"));
        if (spec.contains(QStringLiteral("free"))) {  // the free TX finder on real decodes
            QTimer::singleShot(52000, &app, [&engine] {
                const int before = engine.digiTxHz();
                engine.digiFindFreeTx();
                qInfo().noquote() << "free-test:" << before << "->" << engine.digiTxHz() << "|"
                                  << engine.digi().value(QStringLiteral("status")).toString();
            });
        }
        auto saved = std::make_shared<std::tuple<double, QString, int, QString>>();
        // Slices a cut-short run left behind: with clean, all but the first
        // go (only then: they can be another client's or the user's own).
        QTimer::singleShot(6000, &app, [&engine, spec] {
            if (!spec.contains(QStringLiteral("clean"))) {
                return;
            }
            int first = 1 << 30;
            for (const QVariant& v : engine.slices()) {
                first = std::min(first, v.toMap().value(QStringLiteral("id")).toInt());
            }
            for (const QVariant& v : engine.slices()) {
                const int id = v.toMap().value(QStringLiteral("id")).toInt();
                if (id != first) {
                    engine.removeSlice(id);
                    qInfo() << "ft8-test: removed leftover slice" << id;
                }
            }
            engine.selectSlice(first);
        });
        QTimer::singleShot(8000, &app, [&engine, saved, cq, ft4, spec] {
            *saved = {engine.frequencyMhz(), engine.mode(), engine.rfPower(), engine.digiProtocol()};
            qInfo() << "ft8-test: saved" << std::get<0>(*saved) << std::get<1>(*saved) << std::get<2>(*saved) << "W";
            engine.setDigiProtocol(ft4 ? QStringLiteral("FT4") : QStringLiteral("FT8"));
            engine.digiTune(spec.contains(QStringLiteral("40")) ? (ft4 ? 7.0475 : 7.074) : (ft4 ? 14.080 : 14.074));
            engine.setDigiRunning(true);
            if (cq) {
                engine.setRfPower(10);
                engine.digiCallCq();
            }
        });
        if (answer) {
            // Answer the strongest plain CQ, once the app is idle again.
            QObject::connect(&engine, &AetherSDR::mobile::MobileEngine::digiDecodesChanged, &app, [&engine] {
                if (engine.digi().value(QStringLiteral("state")).toString() != QLatin1String("idle")) {
                    return;
                }
                const QVariantList list = engine.digiDecodes();
                int best = -1;
                int bestSnr = -99;
                for (int i = 0; i < list.size() && i < 40; ++i) {
                    const QVariantMap d = list.at(i).toMap();
                    const QStringList t = d.value(QStringLiteral("text")).toString().split(QLatin1Char(' '));
                    if (!d.value(QStringLiteral("cq")).toBool() || t.size() != 3 || d.value(QStringLiteral("snr")).toInt() <= bestSnr
                        || d.value(QStringLiteral("utc")) != list.first().toMap().value(QStringLiteral("utc"))) {
                        continue;
                    }
                    best = i;
                    bestSnr = d.value(QStringLiteral("snr")).toInt();
                }
                if (best >= 0) {
                    qInfo().noquote() << "ft8-test: answering" << list.at(best).toMap().value(QStringLiteral("text")).toString();
                    engine.setRfPower(10);
                    engine.digiAnswer(best);
                }
            });
        }
        if (spec.contains(QStringLiteral("slices"))) {
            // A second slice: FT8 must stay on its slice while the active
            // one changes, and come back intact when the slice goes.
            auto firstId = std::make_shared<int>(-1);
            auto activeId = [&engine] {
                for (const QVariant& v : engine.slices()) {
                    const QVariantMap m = v.toMap();
                    if (m.value(QStringLiteral("active")).toBool()) {
                        return m.value(QStringLiteral("id")).toInt();
                    }
                }
                return -1;
            };
            QTimer::singleShot(40000, &app, [&engine, firstId, activeId] {
                *firstId = activeId();
                qInfo() << "slices-test: digi on" << engine.digi().value(QStringLiteral("sliceLetter")) << "active" << *firstId << "adding a slice";
                engine.addSlice();
            });
            for (int i = 0; i < 6; ++i) {
                QTimer::singleShot(48000 + i * 3000, &app, [&engine, firstId, activeId, i] {
                    const QVariantList list = engine.slices();
                    int other = -1;
                    for (const QVariant& v : list) {
                        const int id = v.toMap().value(QStringLiteral("id")).toInt();
                        if (id != *firstId) {
                            other = id;
                        }
                    }
                    engine.selectSlice(i % 2 == 0 ? *firstId : other);
                    qInfo() << "slices-test: active" << activeId() << "of" << list.size() << "digi on"
                            << engine.digi().value(QStringLiteral("sliceLetter")) << engine.digi().value(QStringLiteral("sliceMhz"));
                });
            }
            QTimer::singleShot(105000, &app, [&engine, firstId] {
                for (const QVariant& v : engine.slices()) {
                    const int id = v.toMap().value(QStringLiteral("id")).toInt();
                    if (id != *firstId) {
                        engine.removeSlice(id);
                        qInfo() << "slices-test: removed slice" << id;
                    }
                }
                engine.selectSlice(*firstId);
            });
        }
        if (spec.contains(QStringLiteral("multi"))) {
            // Slice B in DIGU 1.5 kHz up: both slices decode, rows marked
            // A / B; then a CQ heard on B is answered: TX, the active slice
            // and the display move to B and one call goes out (then halt).
            auto ids = std::make_shared<std::pair<int, int>>(-1, -1);
            const auto activeId = [&engine] {
                for (const QVariant& v : engine.slices()) {
                    if (v.toMap().value(QStringLiteral("active")).toBool()) {
                        return v.toMap().value(QStringLiteral("id")).toInt();
                    }
                }
                return -1;
            };
            QTimer::singleShot(25000, &app, [&engine, ids, activeId] {
                ids->first = activeId();
                engine.addSlice();
            });
            QTimer::singleShot(30000, &app, [&engine, ids, activeId] {
                ids->second = activeId();
                engine.setMode(QStringLiteral("DIGU"));
                engine.setFrequencyMhz(engine.digi().value(QStringLiteral("sliceMhz")).toDouble() + 0.0015);
                engine.selectSlice(ids->first);
                qInfo() << "multi-test: A" << ids->first << "B" << ids->second;
            });
            QTimer::singleShot(80000, &app, [&engine, ids, activeId] {
                const QVariantList list = engine.digiDecodes();
                QMap<QString, int> perSlice;
                QMap<QString, int> perHl;
                int pick = -1;
                for (int i = 0; i < list.size(); ++i) {
                    const QVariantMap d = list.at(i).toMap();
                    perSlice[d.value(QStringLiteral("slice")).toString()]++;
                    perHl[d.value(QStringLiteral("hl")).toString()]++;
                    const QStringList t = d.value(QStringLiteral("text")).toString().split(QLatin1Char(' '));
                    if (pick < 0 && d.value(QStringLiteral("sliceId")).toInt() == ids->second && d.value(QStringLiteral("cq")).toBool()
                        && t.size() == 3) {
                        pick = i;
                    }
                }
                qInfo() << "multi-test: decodes per slice" << perSlice << "highlights" << perHl;
                if (pick >= 0) {
                    qInfo() << "multi-test: answering" << list.at(pick).toMap().value(QStringLiteral("text")).toString() << "from slice B";
                    engine.setRfPower(10);
                    engine.digiAnswer(pick);
                    QTimer::singleShot(1500, &engine, [&engine, activeId] {
                        qInfo() << "multi-test: after answer, TX slice" << engine.digi().value(QStringLiteral("sliceLetter"))
                                << "active" << activeId() << "pan centre" << engine.panCenterMhz() << engine.panBandwidthMhz();
                    });
                } else {
                    qInfo() << "multi-test: no plain CQ heard on B";
                }
            });
            QTimer::singleShot(125000, &app, [&engine, ids] {
                engine.digiHalt();
                engine.selectSlice(ids->first);
                engine.removeSlice(ids->second);
                qInfo() << "multi-test: halted, B removed";
            });
        }
        if (spec.contains(QStringLiteral("rebind"))) {
            // Slice B added and made TX; a tap in B's passband moves FT8 to
            // B; then a CQ must go out on B (the FT8 slice), not elsewhere.
            auto ids = std::make_shared<std::pair<int, int>>(-1, -1);
            const auto activeId = [&engine] {
                for (const QVariant& v : engine.slices()) {
                    if (v.toMap().value(QStringLiteral("active")).toBool()) {
                        return v.toMap().value(QStringLiteral("id")).toInt();
                    }
                }
                return -1;
            };
            QTimer::singleShot(30000, &app, [&engine, ids, activeId] {
                ids->first = activeId();
                engine.addSlice();
            });
            QTimer::singleShot(36000, &app, [&engine, ids, activeId] {
                ids->second = activeId();
                // TX to A, so the CQ later has to move it to B.
                engine.makeTxSlice(ids->first);
                qInfo() << "rebind-test: A" << ids->first << "B" << ids->second << "digi on"
                        << engine.digi().value(QStringLiteral("sliceLetter")) << "B at" << engine.frequencyMhz() << engine.mode();
            });
            QTimer::singleShot(40000, &app, [&qml, &engine] {
                for (QObject* root : qml.rootObjects()) {
                    auto* window = qobject_cast<QQuickWindow*>(root);
                    if (!window) {
                        continue;
                    }
                    // B's passband: 600 Hz above B's dial, on the display.
                    const double centre = engine.panCenterMhz();
                    const double span = engine.panBandwidthMhz();
                    const double mhz = engine.frequencyMhz() + 0.0006;
                    const double fx = 0.5 + (mhz - centre) / span;
                    const QPointF p(fx * window->width(), 0.62 * window->height());
                    QWindowSystemInterface::handleMouseEvent(window, p, window->mapToGlobal(p), Qt::LeftButton,
                                                             Qt::LeftButton, QEvent::MouseButtonPress);
                    QWindowSystemInterface::handleMouseEvent(window, p, window->mapToGlobal(p), Qt::NoButton,
                                                             Qt::LeftButton, QEvent::MouseButtonRelease);
                    QWindowSystemInterface::flushWindowSystemEvents();
                    qInfo() << "rebind-test: tapped at" << fx << "digi on" << engine.digi().value(QStringLiteral("sliceLetter"))
                            << "tx Hz" << engine.digiTxHz();
                }
            });
            QTimer::singleShot(46000, &app, [&engine] {
                engine.setRfPower(10);
                engine.digiCallCq();
            });
            QTimer::singleShot(46000 + 34000, &app, [&engine] {
                engine.digiHalt();
                qInfo() << "rebind-test: CQ done, digi on" << engine.digi().value(QStringLiteral("sliceLetter"))
                        << engine.digi().value(QStringLiteral("sliceMhz"));
            });
            QTimer::singleShot(84000, &app, [&engine, ids] {
                engine.selectSlice(ids->first);
                engine.removeSlice(ids->second);
            });
        }
        if (cq && !qso) {
            // Two CQs, then stop calling (one slot pair is 30 s in FT8, 15 s in FT4).
            QTimer::singleShot(8000 + (ft4 ? 33000 : 63000), &app, [&engine] { engine.digiHalt(); });
        }
        QTimer::singleShot(8000 + seconds * 1000, &app, [&app, &engine, saved] {
            engine.digiHalt();
            engine.setDigiRunning(false);
            engine.setDigiProtocol(std::get<3>(*saved));
            engine.digiTune(std::get<0>(*saved));
            engine.setDigiRunning(false);
            engine.setMode(std::get<1>(*saved));
            engine.setRfPower(std::get<2>(*saved));
            qInfo() << "ft8-test: restored" << std::get<0>(*saved) << std::get<1>(*saved) << std::get<2>(*saved) << "W";
            QTimer::singleShot(1500, &app, &QCoreApplication::quit);
        });
    }
    if (parser.isSet(radeTestOption)) {
        const QStringList spec = parser.value(radeTestOption).split(QLatin1Char(','));
        const int seconds = std::clamp(spec.value(0).toInt(), 10, 3600);
        const double mhz = spec.value(1, QStringLiteral("14.236")).toDouble();
        auto saved = std::make_shared<std::pair<double, QString>>();
        QTimer::singleShot(8000, &app, [&engine, saved, mhz] {
            *saved = {engine.frequencyMhz(), engine.mode()};
            engine.digiTune(mhz);
            QTimer::singleShot(2000, &engine, [&engine] {
                engine.setRadeActive(true);
                qInfo() << "rade-test: on" << engine.radeStatus();
            });
        });
        QObject::connect(&engine, &AetherSDR::mobile::MobileEngine::radeChanged, &app, [&engine] {
            static QVariantMap last;
            const QVariantMap now = engine.radeStatus();
            if (now.value(QStringLiteral("synced")) != last.value(QStringLiteral("synced"))
                || now.value(QStringLiteral("callsign")) != last.value(QStringLiteral("callsign"))) {
                qInfo() << "rade-test:" << now;
            }
            last = now;
        });
        QTimer::singleShot(10000 + seconds * 1000, &app, [&app, &engine, saved] {
            engine.setRadeActive(false);
            engine.digiTune(saved->first);
            engine.setMode(saved->second);
            qInfo() << "rade-test: restored" << saved->first << saved->second;
            QTimer::singleShot(3000, &app, &QCoreApplication::quit);
        });
    }
    if (parser.isSet(adifImportOption)) {
        QTimer::singleShot(1000, &app, [&app, &engine, file = parser.value(adifImportOption)] {
            qInfo() << "adif-import:" << engine.importAdif(QUrl::fromLocalFile(file));
            app.quit();
        });
    }
    if (parser.isSet(previewTxOption)) {
        engine.setProperty("previewTx", true);
    }
    if (parser.isSet(pskReporterOption)) {
        if (parser.value(pskReporterOption) == QLatin1String("off")) {
            engine.setPskReporter(false);
        } else {
            engine.setMyGrid(parser.value(pskReporterOption));
            engine.setPskReporter(true);
        }
    }
    if (parser.isSet(onlineLogTestOption)) {
        const QStringList a = parser.value(onlineLogTestOption).split(QLatin1Char(','));
        QTimer::singleShot(2000, &app, [&engine, a] { engine.setOnlineLog(a.value(0), a.value(1), a.value(2)); });
        QTimer::singleShot(6000, &app, [&engine] {
            qInfo() << "online-log-test: logging TEST1 ->" << engine.logQso({{QStringLiteral("call"), QStringLiteral("TEST1")},
                                                                            {QStringLiteral("rstSent"), QStringLiteral("59")}});
        });
        QTimer::singleShot(12000, &app, [&engine] { qInfo() << "online-log-test: state" << engine.onlineLog(); });
    }
    if (parser.isSet(digiFilterOption)) {
        engine.setDigiFilter(parser.value(digiFilterOption));
    }
    if (parser.isSet(digiTxLevelOption)) {
        engine.setDigiTxLevelDb(parser.value(digiTxLevelOption).toInt());
    }
    for (const QString& tapSpec : parser.values(tapAtOption)) {  // each --tap-at in turn
        const QStringList t = tapSpec.split(QLatin1Char(','));
        QTimer::singleShot(t.value(2, QStringLiteral("20000")).toInt(), &app, [&qml, &engine, t] {
            for (QObject* root : qml.rootObjects()) {
                auto* window = qobject_cast<QQuickWindow*>(root);
                if (!window) {
                    continue;
                }
                const QPointF p(t.value(0).toDouble() * window->width(), t.value(1).toDouble() * window->height());
                const QPointF g = window->mapToGlobal(p);
                QMouseEvent press(QEvent::MouseButtonPress, p, g, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, p, g, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &release);
                qInfo() << "tap-at:" << p << "digiTxHz" << engine.digiTxHz() << "freq" << engine.frequencyMhz();
                QTimer::singleShot(500, &engine, [&engine] {
                    qInfo() << "tap-at: after, digiTxHz" << engine.digiTxHz() << "freq" << engine.frequencyMhz();
                });
            }
        });
    }
    if (parser.isSet(dragAtOption)) {
        const QStringList t = parser.value(dragAtOption).split(QLatin1Char(','));
        QTimer::singleShot(t.value(3, QStringLiteral("20000")).toInt(), &app, [&qml, &engine, t] {
            for (QObject* root : qml.rootObjects()) {
                auto* window = qobject_cast<QQuickWindow*>(root);
                if (!window) {
                    continue;
                }
                const double y = t.value(1).toDouble() * window->height();
                const double x1 = t.value(0).toDouble() * window->width();
                const double x2 = t.value(2).toDouble() * window->width();
                qInfo() << "drag-at: before, digiTxHz" << engine.digiTxHz() << "freq" << engine.frequencyMhz()
                        << "pan" << engine.panCenterMhz();
                // Through the window system queue, as a real mouse: the
                // handlers' drag tracking needs it.
                const auto send = [window](QPointF p, Qt::MouseButtons buttons, Qt::MouseButton button, QEvent::Type type) {
                    QWindowSystemInterface::handleMouseEvent(window, p, window->mapToGlobal(p), buttons, button, type);
                    QWindowSystemInterface::flushWindowSystemEvents();
                    QCoreApplication::processEvents();
                };
                send(QPointF(x1, y), Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
                for (int i = 1; i <= 30; ++i) {
                    QThread::msleep(10);
                    send(QPointF(x1 + (x2 - x1) * i / 30.0, y), Qt::LeftButton, Qt::NoButton, QEvent::MouseMove);
                }
                send(QPointF(x2, y), Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
                qInfo() << "drag-at: after, digiTxHz" << engine.digiTxHz() << "freq" << engine.frequencyMhz()
                        << "pan" << engine.panCenterMhz();
                // A pan move shows once the radio has answered.
                QTimer::singleShot(1500, &engine, [&engine] {
                    qInfo() << "drag-at: 1.5 s later, freq" << engine.frequencyMhz() << "pan" << engine.panCenterMhz();
                });
            }
        });
    }
    if (parser.isSet(tuneToOption)) {
        const QStringList spec = parser.value(tuneToOption).split(QLatin1Char(','));
        QTimer::singleShot(7000, &app, [&engine, spec] {
            engine.digiTune(spec.value(0).toDouble());
            engine.setMode(spec.value(1, QStringLiteral("USB")));
            qInfo() << "tune-to:" << spec;
        });
    }
    if (parser.isSet(wheelTestOption)) {
        QTimer::singleShot(9000, &app, [&qml] {
            const auto settle = [] {
                QEventLoop loop;
                QTimer::singleShot(60, &loop, &QEventLoop::quit);
                loop.exec();
            };
            for (QObject* root : qml.rootObjects()) {
                auto* window = qobject_cast<QQuickWindow*>(root);
                if (!window) {
                    continue;
                }
                for (QObject* scroll : findNamedObjects(root, QStringLiteral("controlsTabScroll"))
                                           + findNamedObjects(root, QStringLiteral("pinnedScroll"))) {
                    auto* item = qobject_cast<QQuickItem*>(scroll);
                    auto* flick = qvariant_cast<QQuickItem*>(scroll->property("contentItem"));
                    if (!item || !item->isVisible() || !flick) {
                        continue;
                    }
                    const qreal room = flick->property("contentHeight").toReal() - item->height();
                    int total = 0;
                    QMap<QString, int> blockers;
                    for (qreal top = 0; top < room && top < 2 * item->height(); top += item->height()) {
                        for (qreal y = 10; y < item->height() - 10; y += 30) {
                            for (qreal x = 10; x < item->width() - 10; x += 40) {
                                flick->setProperty("contentY", std::max(0.0, std::min(top, room - 50)));
                                const qreal before = flick->property("contentY").toReal();
                                const QPointF scene = item->mapToScene(QPointF(x, y));
                                const QPointF global = window->mapToGlobal(scene);
                                static WheelTrace trace;
                                if (total == 0) {
                                    qApp->installEventFilter(&trace);
                                    trace.on = true;
                                }
                                // As a trackpad sends it: begin, a few updates 16 ms apart, end.
                                static ulong stamp = 1000;
                                const QList<Qt::ScrollPhase> phases{Qt::ScrollBegin, Qt::ScrollUpdate, Qt::ScrollUpdate,
                                                                    Qt::ScrollUpdate, Qt::ScrollUpdate, Qt::ScrollEnd};
                                for (Qt::ScrollPhase phase : phases) {
                                    const QPoint pixels = phase == Qt::ScrollUpdate ? QPoint(0, -20) : QPoint();
                                    const QPoint angle = phase == Qt::ScrollUpdate ? QPoint(0, -60) : QPoint();
                                    QWheelEvent ev(scene, global, pixels, angle, Qt::NoButton, Qt::NoModifier, phase, false);
                                    ev.setTimestamp(stamp += 16);
                                    QCoreApplication::sendEvent(window, &ev);
                                    QEventLoop loop;
                                    QTimer::singleShot(16, &loop, &QEventLoop::quit);
                                    loop.exec();
                                }
                                trace.on = false;
                                settle();
                                QMetaObject::invokeMethod(flick, "cancelFlick");
                                if (total == 0) {
                                    qInfo().noquote() << "wheel-test: first point" << scene << "flick" << flick->metaObject()->className()
                                                      << "contentY" << before << "->" << flick->property("contentY").toReal()
                                                      << "interactive" << flick->property("interactive");
                                }
                                ++total;
                                if (flick->property("contentY").toReal() <= before) {
                                    QQuickItem* hit = window->contentItem();
                                    while (QQuickItem* child = hit->childAt(hit->mapFromScene(scene).x(),
                                                                            hit->mapFromScene(scene).y())) {
                                        hit = child;
                                    }
                                    QString chain;
                                    for (QQuickItem* i = hit; i && i != flick && chain.count(QLatin1Char('<')) < 4; i = i->parentItem()) {
                                        chain += QString::fromLatin1(i->metaObject()->className()).section(QLatin1Char('_'), 0, 0)
                                                 + QLatin1Char('<');
                                    }
                                    ++blockers[chain];
                                }
                            }
                        }
                    }
                    int blocked = 0;
                    for (int n : blockers) {
                        blocked += n;
                    }
                    qInfo().noquote() << "wheel-test:" << scroll->objectName() << total << "points," << blocked
                                      << "did not scroll";
                    for (auto it = blockers.cbegin(); it != blockers.cend(); ++it) {
                        qInfo().noquote() << "wheel-test:   " << it.value() << "x" << it.key();
                    }
                }
            }
        });
    }
    if (parser.isSet(controlsScrollOption)) {
        const QStringList scrollArgs = parser.value(controlsScrollOption).split(QLatin1Char(','));
        const qreal px = scrollArgs.value(0).toDouble();
        QTimer::singleShot(scrollArgs.value(1, QStringLiteral("7000")).toInt(), &app, [&qml, px] {
            for (QObject* root : qml.rootObjects()) {
                for (QObject* scroll : findNamedObjects(root, QStringLiteral("controlsTabScroll"))
                                           + findNamedObjects(root, QStringLiteral("pinnedScroll"))) {
                    auto* item = qobject_cast<QQuickItem*>(scroll);
                    auto* flick = qvariant_cast<QObject*>(scroll->property("contentItem"));
                    if (item && item->isVisible() && flick) {
                        const qreal room = flick->property("contentHeight").toReal() - item->height();
                        flick->setProperty("contentY", std::clamp(px, 0.0, std::max(0.0, room)));
                        qInfo().noquote() << "controls-scroll:" << scroll->objectName() << "height" << item->height()
                                          << "contentHeight" << flick->property("contentHeight").toReal()
                                          << "contentY" << flick->property("contentY").toReal();
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
            QStringLiteral("CW"), QStringLiteral("View"), QStringLiteral("Profiles"), QStringLiteral("Memory"),
            QStringLiteral("Log"), QStringLiteral("Digi"), QStringLiteral("FreeDV")};
        QStringList pinned;
        for (const QString& index : parser.value(controlsTabOption).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            if (const QString name = names.value(index.toInt()); !name.isEmpty()) {
                pinned << name;
            }
        }
        if (!pinned.isEmpty()) {
            const QStringList saved = engine.pinnedSections();
            engine.setPinnedSections(pinned);
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
