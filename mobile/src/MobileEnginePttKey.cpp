// A learned PTT key (roadmap 121, 41 (a)): any key a button sends becomes
// PTT, held as long as the key. Rob Metallo VA3MET's UNIWA F400 sends its
// side PTT key as F2 from its keypad ("mtk-kpd"), press and release both,
// and F2 played the voice keyer's slot 2; a Bluetooth button in keyboard
// mode sends what its maker chose. Controls > TX > PTT key: Learn takes the
// next key pressed (Esc cancels), Clear forgets it; off until learned.
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
#include <QWindow>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kPttKeyKey = QStringLiteral("MobileTxPttKey");
const QString kPttKeyScanKey = QStringLiteral("MobileTxPttKeyScan");

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
    if (m_pttKey != 0) {
        qCInfo(lcMobile).noquote() << "PTT key:" << pttKeyName();
    }
}

int MobileEngine::pttKey() const
{
    return m_pttKey;
}

QString MobileEngine::pttKeyName() const
{
    if (m_pttKey == 0) {
        return {};
    }
    if (m_pttKey == Qt::Key_unknown) {
        return tr("key %1").arg(m_pttKeyScan);
    }
    const QString name = QKeySequence(m_pttKey).toString(QKeySequence::NativeText);
    return name.isEmpty() ? tr("key %1").arg(m_pttKey) : name;
}

bool MobileEngine::pttKeyLearning() const
{
    return m_pttKeyLearning;
}

void MobileEngine::learnPttKey()
{
    releasePttKey(QStringLiteral("learning a new key"));
    m_pttKeyLearning = true;
    qCInfo(lcMobile) << "PTT key: learning, the next key pressed";
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
    AppSettings::instance().remove(kPttKeyKey);
    AppSettings::instance().remove(kPttKeyScanKey);
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
    if (m_pttKeyLearning) {
        if (!press || event->isAutoRepeat()) {
            return true;
        }
        m_pttKeyLearning = false;
        if (event->key() == Qt::Key_Escape) {
            qCInfo(lcMobile) << "PTT key: learning cancelled (Esc)";
            emit pttKeyChanged();
            return true;
        }
        m_pttKey = event->key() == 0 ? int(Qt::Key_unknown) : event->key();
        m_pttKeyScan = event->nativeScanCode();
        AppSettings::instance().setValue(kPttKeyKey, m_pttKey);
        AppSettings::instance().setValue(kPttKeyScanKey, m_pttKeyScan);
        AppSettings::instance().save();
        qCInfo(lcMobile).noquote() << "PTT key: learned" << pttKeyName() << "(Qt key" << m_pttKey << ", scan code"
                                   << m_pttKeyScan << ")";
        emit pttKeyChanged();
        return true;
    }
    if (m_pttKey == 0) {
        return false;
    }
    const bool match = m_pttKey == int(Qt::Key_unknown)
                           ? (event->key() == 0 || event->key() == Qt::Key_unknown) && event->nativeScanCode() == m_pttKeyScan
                           : event->key() == m_pttKey;
    if (!match) {
        return false;
    }
    // The key's auto-repeat, and the key while a layout is edited: taken,
    // nothing done.
    if (event->isAutoRepeat() || m_layoutEditing) {
        return true;
    }
    if (press) {
        if (m_pttKeyHeld) {
            return true;
        }
        m_pttKeyHeld = true;
        if (pttToggle() && !cwMode()) {
            togglePtt();
        } else {
            setPtt(true, QStringLiteral("PTT key"));
        }
    } else if (m_pttKeyHeld) {
        m_pttKeyHeld = false;
        if (!pttToggle() || cwMode()) {
            setPtt(false, QStringLiteral("PTT key released"));
        }
    }
    return true;
}

} // namespace AetherSDR::mobile
