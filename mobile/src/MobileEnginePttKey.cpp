// A learned PTT key (roadmap 121, 41 (a)): any key a button sends becomes
// PTT, held as long as the key. Rob Metallo VA3MET's UNIWA F400 sends its
// side PTT key as F2 from its keypad ("mtk-kpd"), press and release both,
// and F2 played the voice keyer's slot 2; a Bluetooth button in keyboard
// mode sends what its maker chose. Controls > TX > PTT key: Learn takes the
// next key pressed (Esc cancels), Clear forgets it; off until learned.
//
// A combination is learned the same way: modifiers held and then a key
// (Option + Space) take both, and modifiers alone, let go without a key
// (Option, Control + Option), take the modifiers. A combination keys only
// when exactly its modifiers are held, so Space alone stays Space; with a
// key, PTT is let go when the key or one of its modifiers is released.
//
// An application-wide filter takes the learned key before anything else,
// wherever the keyboard is (the page, a drawer, a text field): the press
// keys PTT through setPtt, as Space does, so the transmit lock, the link's
// transmit block and the rest hold it the same way; Tap on / off toggles on
// the press; the release lets go; the key's auto-repeat is taken and does
// nothing, and the key no longer acts as itself (F2 no longer plays the
// slot). PTT is let go when the app leaves the front or its window loses
// the keyboard, so a release that never arrives cannot hold the
// transmitter. While an own layout is edited the key keys nothing, as Space.

#include "MobileEngine.h"

#include "core/AppSettings.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLoggingCategory>
#include <QStringList>
#include <QWindow>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kPttKeyKey = QStringLiteral("MobileTxPttKey");
const QString kPttKeyScanKey = QStringLiteral("MobileTxPttKeyScan");
const QString kPttKeyModsKey = QStringLiteral("MobileTxPttKeyMods");

const Qt::KeyboardModifiers kPttModMask =
    Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;

// The modifier a modifier key is, or none for any other key.
Qt::KeyboardModifier modifierOfKey(int key)
{
    switch (key) {
    case Qt::Key_Shift:
        return Qt::ShiftModifier;
    case Qt::Key_Control:
        return Qt::ControlModifier;
    case Qt::Key_Alt:
        return Qt::AltModifier;
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
        return Qt::MetaModifier;
    default:
        return Qt::NoModifier;
    }
}

// The modifiers held once this event has happened: a platform may or may
// not count a modifier key in its own press and release.
Qt::KeyboardModifiers modifiersAfter(const QKeyEvent* event)
{
    Qt::KeyboardModifiers mods = event->modifiers() & kPttModMask;
    const Qt::KeyboardModifier own = modifierOfKey(event->key());
    if (own != Qt::NoModifier) {
        if (event->type() == QEvent::KeyPress) {
            mods |= own;
        } else {
            mods &= ~Qt::KeyboardModifiers(own);
        }
    }
    return mods;
}

// The modifiers in words, as a screen reader should say them. On Apple
// platforms Qt's Control is Command and its Meta is Control.
QString modifierNames(Qt::KeyboardModifiers mods)
{
    QStringList parts;
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    if (mods & Qt::MetaModifier) {
        parts << MobileEngine::tr("Control");
    }
    if (mods & Qt::AltModifier) {
        parts << MobileEngine::tr("Option");
    }
    if (mods & Qt::ShiftModifier) {
        parts << MobileEngine::tr("Shift");
    }
    if (mods & Qt::ControlModifier) {
        parts << MobileEngine::tr("Command");
    }
#else
    if (mods & Qt::ControlModifier) {
        parts << MobileEngine::tr("Ctrl");
    }
    if (mods & Qt::AltModifier) {
        parts << MobileEngine::tr("Alt");
    }
    if (mods & Qt::ShiftModifier) {
        parts << MobileEngine::tr("Shift");
    }
    if (mods & Qt::MetaModifier) {
        parts << MobileEngine::tr("Meta");
    }
#endif
    return parts.join(QStringLiteral(" + "));
}

// The key events a window gets, before its items: once per key, whatever
// item has the keyboard.
class PttKeyFilter : public QObject {
public:
    explicit PttKeyFilter(MobileEngine* engine) : QObject(engine), m_engine(engine) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) && watched->isWindowType()) {
            return m_engine->pttKeyEvent(static_cast<QKeyEvent*>(event));
        }
        return false;
    }

private:
    MobileEngine* m_engine;
};
} // namespace

void MobileEngine::setupPttKey()
{
    m_pttKey = AppSettings::instance().value(kPttKeyKey, 0).toInt();
    m_pttKeyScan = AppSettings::instance().value(kPttKeyScanKey, 0).toUInt();
    m_pttKeyMods = Qt::KeyboardModifiers(AppSettings::instance().value(kPttKeyModsKey, 0).toInt()) & kPttModMask;
    // A modifier learned as a key before combinations (Option): the same
    // modifier as a combination of one.
    if (const Qt::KeyboardModifier own = modifierOfKey(m_pttKey); own != Qt::NoModifier) {
        m_pttKeyMods |= own;
        m_pttKey = 0;
        m_pttKeyScan = 0;
    }
    qApp->installEventFilter(new PttKeyFilter(this));
    // A held key let go when the app leaves the front or the keyboard goes
    // elsewhere: its release would never come here.
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive) {
            releasePttKey(QStringLiteral("app not in front"));
        }
    });
    connect(qGuiApp, &QGuiApplication::focusWindowChanged, this, [this](QWindow* window) {
        if (!window) {
            releasePttKey(QStringLiteral("window lost the keyboard"));
        }
    });
    if (pttKey() != 0) {
        qCInfo(lcMobile).noquote() << "PTT key:" << pttKeyName();
    }
}

// Not 0 when a key or a combination is learned.
int MobileEngine::pttKey() const
{
    return m_pttKey != 0 ? m_pttKey : m_pttKeyMods.toInt();
}

QString MobileEngine::pttKeyName() const
{
    QString key;
    if (m_pttKey == int(Qt::Key_unknown)) {
        key = tr("key %1").arg(m_pttKeyScan);
    } else if (m_pttKey != 0) {
        key = QKeySequence(m_pttKey).toString(QKeySequence::NativeText);
        if (key.isEmpty()) {
            key = tr("key %1").arg(m_pttKey);
        }
    }
    const QString mods = modifierNames(m_pttKeyMods);
    if (mods.isEmpty() || key.isEmpty()) {
        return mods + key;
    }
    return mods + QStringLiteral(" + ") + key;
}

bool MobileEngine::pttKeyLearning() const
{
    return m_pttKeyLearning;
}

void MobileEngine::learnPttKey()
{
    releasePttKey(QStringLiteral("learning a new key"));
    m_pttKeyLearning = true;
    m_pttKeyLearnMods = {};
    qCInfo(lcMobile) << "PTT key: learning, the next key or combination pressed";
    emit pttKeyChanged();
}

void MobileEngine::cancelPttKeyLearn()
{
    if (m_pttKeyLearning) {
        m_pttKeyLearning = false;
        qCInfo(lcMobile) << "PTT key: learning cancelled";
        emit pttKeyChanged();
    }
}

void MobileEngine::clearPttKey()
{
    releasePttKey(QStringLiteral("key cleared"));
    m_pttKeyLearning = false;
    m_pttKey = 0;
    m_pttKeyScan = 0;
    m_pttKeyMods = {};
    AppSettings::instance().remove(kPttKeyKey);
    AppSettings::instance().remove(kPttKeyScanKey);
    AppSettings::instance().remove(kPttKeyModsKey);
    AppSettings::instance().save();
    qCInfo(lcMobile) << "PTT key: cleared";
    emit pttKeyChanged();
}

void MobileEngine::setLayoutEditing(bool editing)
{
    if (editing == m_layoutEditing) {
        return;
    }
    m_layoutEditing = editing;
    if (editing) {
        releasePttKey(QStringLiteral("layout editing"));
    }
    emit layoutEditingChanged();
}

void MobileEngine::releasePttKey(const QString& why)
{
    // A key whose release will not come here: its next press is a press.
    m_pttKeyMainDown = false;
    if (!m_pttKeyHeld) {
        return;
    }
    m_pttKeyHeld = false;
    if (!pttToggle() || cwMode()) {
        setPtt(false, QStringLiteral("PTT key: ") + why);
    }
}

bool MobileEngine::pttKeyEvent(QKeyEvent* event)
{
    const bool press = event->type() == QEvent::KeyPress;
    const Qt::KeyboardModifier own = modifierOfKey(event->key());
    if (m_pttKeyLearning) {
        if (event->isAutoRepeat()) {
            return true;
        }
        if (press && own != Qt::NoModifier) {
            // A modifier: perhaps the start of a combination, perhaps the
            // whole of it. Its release, or the next key, decides.
            m_pttKeyLearnMods |= modifiersAfter(event);
            return true;
        }
        if (!press) {
            // A modifier let go with no key pressed: the modifiers held are
            // the combination. A release of a modifier pressed before Learn
            // (the keys that pressed Learn) learns nothing.
            if (own == Qt::NoModifier || m_pttKeyLearnMods == Qt::NoModifier) {
                return true;
            }
            m_pttKey = 0;
            m_pttKeyScan = 0;
            m_pttKeyMods = m_pttKeyLearnMods;
        } else {
            const Qt::KeyboardModifiers mods = event->modifiers() & kPttModMask;
            if (event->key() == Qt::Key_Escape && mods == Qt::NoModifier) {
                m_pttKeyLearning = false;
                qCInfo(lcMobile) << "PTT key: learning cancelled (Esc)";
                emit pttKeyChanged();
                return true;
            }
            m_pttKey = event->key() == 0 ? int(Qt::Key_unknown) : event->key();
            m_pttKeyScan = event->nativeScanCode();
            m_pttKeyMods = mods;
        }
        m_pttKeyLearning = false;
        AppSettings::instance().setValue(kPttKeyKey, m_pttKey);
        AppSettings::instance().setValue(kPttKeyScanKey, m_pttKeyScan);
        AppSettings::instance().setValue(kPttKeyModsKey, m_pttKeyMods.toInt());
        AppSettings::instance().save();
        qCInfo(lcMobile).noquote() << "PTT key: learned" << pttKeyName() << "(Qt key" << m_pttKey << ", scan code"
                                   << m_pttKeyScan << ", modifiers" << Qt::hex << m_pttKeyMods.toInt() << ")";
        emit pttKeyChanged();
        return true;
    }
    if (pttKey() == 0) {
        return false;
    }

    const auto keyDown = [this] {
        qCInfo(lcMobile).noquote() << "PTT key: down," << pttKeyName();
        m_pttKeyHeld = true;
        if (pttToggle() && !cwMode()) {
            togglePtt();
        } else {
            setPtt(true, QStringLiteral("PTT key"));
        }
    };
    // Let go, but leave m_pttKeyMainDown: the key's own release is still
    // to come and is taken.
    const auto letGo = [this](const QString& why) {
        if (!m_pttKeyHeld) {
            return;
        }
        qCInfo(lcMobile).noquote() << "PTT key: up," << why;
        m_pttKeyHeld = false;
        if (!pttToggle() || cwMode()) {
            setPtt(false, why);
        }
    };

    if (m_pttKey == 0) {
        // Modifiers alone (Option, Control + Option). Other keys pass, so a
        // key typed while they are held acts as itself.
        if (own == Qt::NoModifier) {
            return false;
        }
        if (event->isAutoRepeat()) {
            return m_pttKeyHeld;
        }
        if (press) {
            if (m_pttKeyHeld) {
                return true;
            }
            if (modifiersAfter(event) != m_pttKeyMods) {
                return false;
            }
            if (!m_layoutEditing) {
                keyDown();
            }
            return true;
        }
        if (m_pttKeyHeld && (m_pttKeyMods & own)) {
            letGo(QStringLiteral("PTT key released"));
            return true;
        }
        return false;
    }

    // A key, with or without modifiers.
    const bool isKey = m_pttKey == int(Qt::Key_unknown)
                           ? (event->key() == 0 || event->key() == Qt::Key_unknown) && event->nativeScanCode() == m_pttKeyScan
                           : event->key() == m_pttKey;
    if (!isKey) {
        // One of the combination's modifiers let go while it keys: PTT lets
        // go, and the modifier's release goes on as usual.
        if (!press && own != Qt::NoModifier && (m_pttKeyMods & own)) {
            letGo(QStringLiteral("PTT key modifier released"));
        }
        return false;
    }
    if (press) {
        // The key's auto-repeat, or a press after one whose release never
        // came: taken, nothing done.
        if (m_pttKeyMainDown) {
            return true;
        }
        // The key with other modifiers (Space without Option): itself.
        if (event->isAutoRepeat() || (event->modifiers() & kPttModMask) != m_pttKeyMods) {
            return false;
        }
        // While a layout is edited: taken, nothing done.
        if (m_layoutEditing) {
            return true;
        }
        m_pttKeyMainDown = true;
        keyDown();
        return true;
    }
    if (!m_pttKeyMainDown) {
        return false;
    }
    if (!event->isAutoRepeat()) {
        m_pttKeyMainDown = false;
        letGo(QStringLiteral("PTT key released"));
    }
    return true;
}

} // namespace AetherSDR::mobile
