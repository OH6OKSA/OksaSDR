// The Ulanzi Dial (D100H) as a controller (roadmap 83, Christoph Steinert
// DM5HF). The dial is a plain Bluetooth keyboard: its knob sends Volume Up /
// Down, its press Mute, the three top keys Previous / Play-Pause / Next, the
// four side keys Ctrl+V, Ctrl+C, Ctrl+Y and Ctrl+Z (Mode Cycle Ctrl+Y with
// Previous). The platforms hand its keys here in the Linux key code space and
// src/core's UlanziChordDecoder (the desktop's) makes them a knob's steps and
// the controls' presses and releases.
//
// It is another controller on Radio Setup › MIDI, not a page of its own: with
// Ulanzi Dial on, Learn on a function takes the next dial control as it takes
// a MIDI one, and the same functions run (MobileEngineMidi.cpp), under the
// same Transmit from the controller. The knob goes to the VFO knob or a level
// (volume, AGC-T, RF power, CW speed), a step a detent; the keys to the
// buttons' functions, a held one (PTT, the paddles, the straight key) for as
// long as the key is down.
//
// Where the keys come from:
//  - Android: OksaActivity.dispatchKeyEvent takes the dial's keys (its
//    vendor / product, 0xFFF1 / 0x0082, or a name beginning "Ulanzi") while
//    this is on, so the phone's own volume buttons and a headset keep
//    theirs; the dial going (or the app leaving the screen) sends -1.
//  - iPad, iPhone and the iPad app on a Mac: the system keeps the knob and
//    the top keys (the volume and the media keys), so only the side keys
//    come, through GameController (SideKeys.mm): Control and V / C / Y / Z.
//    GCKeyboard is every keyboard as one, so a Control+V typed on another
//    keyboard counts too while this is on, and Control is no CW paddle then.
//  - Elsewhere: the render tool's "ulanzi KEY VALUE", for the tests.

#include "MobileEngine.h"

#include "SideKeys.h"
#include "core/AppSettings.h"
#include "core/MidiControlManager.h"
#include "models/RadioModel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>

#include <algorithm>
#include <utility>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#endif

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

#if defined(HAVE_MIDI)

namespace {
const QString kUlanziOnKey = QStringLiteral("MobileUlanziOn");
const QString kUlanziMapKey = QStringLiteral("MobileUlanziMap");
const QString kKnob = QStringLiteral("KNOB");
const QString kVfoParam = QStringLiteral("rx.tuneKnob");

// Until the operator learns otherwise: the knob tunes, its press steps
// larger. No transmit function by default.
QHash<QString, QString> defaultMap()
{
    return {{kKnob, kVfoParam}, {QStringLiteral("KEY_MUTE"), QStringLiteral("rx.stepUp")}};
}

#ifdef Q_OS_ANDROID
QPointer<MobileEngine> s_ulanziEngine;
// The Java side calls the native ulanziKey only when told the dial is on,
// and is told so only once that native is registered (an unregistered one
// would throw UnsatisfiedLinkError in dispatchKeyEvent and close the app).
bool s_ulanziBridge = false;

// OksaActivity's native ulanziKey(int, int), on Android's UI thread.
void JNICALL ulanziKeyFromJava(JNIEnv*, jclass, jint linuxKey, jint value)
{
    QPointer<MobileEngine> engine = s_ulanziEngine;
    if (!engine) {
        return;
    }
    QMetaObject::invokeMethod(engine, [engine, linuxKey, value] {
        if (engine) {
            engine->ulanziKey(int(linuxKey), int(value));
        }
    }, Qt::QueuedConnection);
}

void tellJava(bool on)
{
    if (!s_ulanziBridge) {
        return;
    }
    QJniObject::callStaticMethod<void>("fi/oh6oksa/oksasdr/OksaActivity", "setUlanziOn", "(Z)V", jboolean(on));
}
#endif
} // namespace

void MobileEngine::setupUlanzi()
{
    const QJsonObject saved = QJsonDocument::fromJson(AppSettings::instance().value(kUlanziMapKey).toString().toUtf8()).object();
    if (saved.isEmpty()) {
        m_ulanziMap = defaultMap();
    } else {
        for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
            const QString id = it.value().toString();
            if (m_midi && m_midi->findParam(id)) {
                m_ulanziMap.insert(it.key(), id);
            }
        }
    }
#ifdef Q_OS_ANDROID
    s_ulanziEngine = this;
    {
        QJniEnvironment env;
        const JNINativeMethod methods[] = {
            {"ulanziKey", "(II)V", reinterpret_cast<void*>(ulanziKeyFromJava)}};
        s_ulanziBridge = env.registerNativeMethods("fi/oh6oksa/oksasdr/OksaActivity", methods, 1);
        if (!s_ulanziBridge) {
            qCWarning(lcMobile) << "Ulanzi Dial: the key bridge to Android did not register; the dial is not read";
        } else if (ulanziOn()) {
            tellJava(true);
        }
    }
#endif
    if (sideKeysAvailable()) {
        startUlanziKeys([this](int linuxKey, bool pressed) { ulanziKey(linuxKey, pressed ? 1 : 0); });
        setUlanziKeysOn(ulanziOn());
    }
    // A transmission over: what the dial keyed has ended, so the dial going
    // later (on iOS any text field taking the keys, on Android the app
    // leaving the screen) ends nothing it did not start, such as a later
    // transmission from the screen's PTT or Space.
    connect(&m_session.radioModel(), &RadioModel::radioTransmittingChanged, this, [this](bool on) {
        if (!on && m_ulanziHeld.isEmpty()) {
            m_ulanziKeyed = false;
        }
    });
}

bool MobileEngine::ulanziOn() const
{
    return AppSettings::instance().value(kUlanziOnKey).toString() == QLatin1String("1");
}

void MobileEngine::setUlanziOn(bool on)
{
    if (on == ulanziOn()) {
        return;
    }
    if (!on) {
        ulanziKey(-1, 0);  // anything the dial holds is let go
    }
    AppSettings::instance().setValue(kUlanziOnKey, on ? QStringLiteral("1") : QStringLiteral("0"));
    AppSettings::instance().save();
#ifdef Q_OS_ANDROID
    tellJava(on);
#endif
    if (sideKeysAvailable()) {
        setUlanziKeysOn(on);
    }
    qCInfo(lcMobile) << "Ulanzi Dial:" << (on ? "on" : "off");
    m_ulanziNote.clear();
    emit midiChanged();
}

void MobileEngine::ulanziKey(int linuxKey, int value)
{
    if (linuxKey < 0) {
        // The dial went (Bluetooth off, out of range, the app off the
        // screen): every held function is released, and what it keyed ends.
        m_ulanziDecoder.reset();
        const QSet<QString> held = std::exchange(m_ulanziHeld, {});
        for (const QString& id : held) {
            midiAction(id, 0.0f);
        }
        // A transmission the dial started (PTT held, MOX, TUNE, a macro)
        // ends with it, as a MIDI controller's does.
        if (std::exchange(m_ulanziKeyed, false)) {
            qCInfo(lcMobile) << "Ulanzi Dial: gone, what it keyed ends";
            midiEndTransmit(QStringLiteral("Ulanzi Dial gone"));
        }
        return;
    }
    if (!ulanziOn() || !m_midi) {
        return;
    }
    for (const UlanziChordDecoder::Event& e : m_ulanziDecoder.feed(linuxKey, value)) {
        ulanziEvent(e);
    }
}

void MobileEngine::ulanziEvent(const UlanziChordDecoder::Event& event)
{
    const bool knob = event.kind == UlanziChordDecoder::Event::Kind::Tune;
    const QString signature = knob ? kKnob : event.signature;
    // The last control on Radio Setup › MIDI's Last message line.
    const QString last = knob ? tr("%1 %2").arg(ulanziName(kKnob), event.tuneSteps > 0 ? QStringLiteral("+") : QStringLiteral("−"))
                              : (event.action ? ulanziName(signature) : tr("%1 released").arg(ulanziName(signature)));
    if (last != m_midiActivity) {
        m_midiActivity = last;
        emit midiActivityChanged();
    }
    // A learn under way takes a control's press (or the knob's first step).
    if (m_midi->isLearning() && (knob || event.action == 1)) {
        ulanziLearn(signature);
        return;
    }
    const QString id = m_ulanziMap.value(signature);
    if (id.isEmpty()) {
        return;
    }
    const MidiParam* p = m_midi->findParam(id);
    if (!p) {
        return;
    }
    if (knob) {
        if (id == kVfoParam) {
            midiTune(event.tuneSteps);
            return;
        }
        // A level a detent at a time, from where it is now.
        double now = 0.0;
        if (id == QLatin1String("rx.afGain")) {
            now = afGain();
        } else if (id == QLatin1String("rx.agcThreshold")) {
            now = agcThreshold();
        } else if (id == QLatin1String("tx.rfPower")) {
            now = rfPower();
        } else if (id == QLatin1String("cw.speed")) {
            now = cwSpeed();
        } else {
            return;
        }
        const double next = std::clamp(now + event.tuneSteps, double(p->rangeMin), double(p->rangeMax));
        midiAction(id, float(next));
        return;
    }
    // The dial's own transmissions: a transmit function that keyed (its
    // MIDI action sets m_midiKeyed only when Transmit from the controller
    // lets it), not one refused.
    const auto run = [this, &id](float value) {
        const bool keyedBefore = std::exchange(m_midiKeyed, false);
        midiAction(id, value);
        if (m_midiKeyed && m_midiTxFunctions.contains(id)) {
            m_ulanziKeyed = true;
        }
        m_midiKeyed = m_midiKeyed || keyedBefore;
    };
    if (p->type == MidiParamType::Gate) {
        if (event.action) {
            m_ulanziHeld.insert(id);
        } else {
            m_ulanziHeld.remove(id);
        }
        run(event.action ? 1.0f : 0.0f);
        return;
    }
    // Triggers and toggles on the press.
    if (event.action == 1) {
        run(1.0f);
    }
}

// The learn's function takes this control: the knob only a knob's function
// (the VFO or a level), a key only a button's. A control another function
// had moves here, and the line under the switch says so (roadmap 73's rule:
// never silently).
bool MobileEngine::ulanziLearn(const QString& signature)
{
    const QString id = m_midi->learningParamId();
    const MidiParam* p = m_midi->findParam(id);
    if (!p) {
        return false;
    }
    const bool knob = signature == kKnob;
    const bool knobFunction = p->type == MidiParamType::Slider;
    if (knob != knobFunction) {
        m_ulanziNote = knob ? tr("The dial's knob turns the VFO or a level: press a key of the dial for %1.").arg(p->displayName)
                            : tr("%1 is a knob's function: turn the dial's knob for it.").arg(p->displayName);
        emit midiChanged();
        return false;
    }
    if (knob && id != kVfoParam && id != QLatin1String("rx.afGain") && id != QLatin1String("rx.agcThreshold")
        && id != QLatin1String("tx.rfPower") && id != QLatin1String("cw.speed")) {
        m_ulanziNote = tr("The dial's knob cannot run %1.").arg(p->displayName);
        emit midiChanged();
        return false;
    }
    const QString before = m_ulanziMap.value(signature);
    // One dial control a function.
    for (auto it = m_ulanziMap.begin(); it != m_ulanziMap.end();) {
        if (it.value() == id && it.key() != signature) {
            it = m_ulanziMap.erase(it);
        } else {
            ++it;
        }
    }
    m_ulanziMap.insert(signature, id);
    m_midi->cancelLearn();
    if (!before.isEmpty() && before != id) {
        const MidiParam* o = m_midi->findParam(before);
        m_ulanziNote = tr("%1 moved from %2 to %3.").arg(ulanziName(signature), o ? o->displayName : before, p->displayName);
    } else {
        m_ulanziNote = tr("%1 runs %2.").arg(ulanziName(signature), p->displayName);
    }
    qCInfo(lcMobile).noquote() << "Ulanzi Dial: learned" << signature << "for" << p->displayName
                               << (before.isEmpty() || before == id ? QString() : QStringLiteral("(taken from %1)").arg(before));
    ulanziSave();
    emit midiChanged();
    return true;
}

void MobileEngine::ulanziSave()
{
    QJsonObject o;
    for (auto it = m_ulanziMap.cbegin(); it != m_ulanziMap.cend(); ++it) {
        o.insert(it.key(), it.value());
    }
    // An empty map saved as {"": ""} would read back as the defaults; a
    // cleared dial keeps nothing.
    if (o.isEmpty()) {
        o.insert(QStringLiteral("none"), QString());
    }
    AppSettings::instance().setValue(kUlanziMapKey, QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
    AppSettings::instance().save();
}

// What Radio Setup shows for a control.
QString MobileEngine::ulanziName(const QString& signature) const
{
    if (signature == kKnob) {
        return tr("Dial knob");
    }
    if (signature == QLatin1String("KEY_MUTE")) {
        return tr("Dial press");
    }
    if (signature == QLatin1String("KEY_PREVIOUSSONG")) {
        return tr("Dial ⏮");
    }
    if (signature == QLatin1String("KEY_PLAYPAUSE")) {
        return tr("Dial ⏯");
    }
    if (signature == QLatin1String("KEY_NEXTSONG")) {
        return tr("Dial ⏭");
    }
    if (signature.endsWith(QLatin1String("+KEY_PREVIOUSSONG"))) {
        return tr("Dial Mode Cycle");
    }
    return tr("Dial %1").arg(signature);
}

QString MobileEngine::ulanziSource(const QString& paramId) const
{
    if (!ulanziOn()) {
        return {};
    }
    for (auto it = m_ulanziMap.cbegin(); it != m_ulanziMap.cend(); ++it) {
        if (it.value() == paramId) {
            return ulanziName(it.key());
        }
    }
    return {};
}

void MobileEngine::ulanziStatus(QVariantMap& m) const
{
    m.insert(QStringLiteral("dialOn"), ulanziOn());
    m.insert(QStringLiteral("dialNote"), m_ulanziNote);
    m.insert(QStringLiteral("dialBound"), static_cast<int>(m_ulanziMap.size()));
#if defined(Q_OS_ANDROID)
    m.insert(QStringLiteral("dialKeys"), QStringLiteral("all"));
    const QString device = QJniObject::callStaticObjectMethod("fi/oh6oksa/oksasdr/OksaActivity", "ulanziDevice",
                                                              "()Ljava/lang/String;").toString();
    m.insert(QStringLiteral("dialDevice"), device);
#elif defined(Q_OS_IOS)
    m.insert(QStringLiteral("dialKeys"), QStringLiteral("side"));
#else
    m.insert(QStringLiteral("dialKeys"), QStringLiteral("test"));
#endif
}

#else

void MobileEngine::setupUlanzi() {}
bool MobileEngine::ulanziOn() const { return false; }
void MobileEngine::setUlanziOn(bool) {}
void MobileEngine::ulanziKey(int, int) {}
void MobileEngine::ulanziEvent(const UlanziChordDecoder::Event&) {}
bool MobileEngine::ulanziLearn(const QString&) { return false; }
void MobileEngine::ulanziSave() {}
QString MobileEngine::ulanziName(const QString&) const { return {}; }
QString MobileEngine::ulanziSource(const QString&) const { return {}; }
void MobileEngine::ulanziStatus(QVariantMap&) const {}

#endif

} // namespace AetherSDR::mobile
