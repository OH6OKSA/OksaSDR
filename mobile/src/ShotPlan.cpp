#include "ShotPlan.h"

#include "MobileEngine.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>
#include <functional>

namespace AetherSDR::mobile {

namespace {

// Every object under the window: the QObject children and the visual items
// with their QObject children (a popup's content is not a QObject child of its
// page, and a Repeater's delegates, such as the phone header's info panel
// pieces with their popups, are no QObject children of anything).
QList<QObject*> allObjects(QObject* root)
{
    QList<QObject*> found = root->findChildren<QObject*>();
    QSet<QObject*> seen(found.cbegin(), found.cend());
    auto add = [&](QObject* o) {
        if (!seen.contains(o)) {
            seen.insert(o);
            found << o;
        }
    };
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* item) {
        add(item);
        for (QObject* child : item->children()) {
            add(child);
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

// On screen: visible and not under an item at opacity 0 (the tablet page's
// probes are shown so, to be measured, never seen).
bool seen(const QQuickItem* item)
{
    if (!item->isVisible()) {
        return false;
    }
    for (const QQuickItem* i = item; i; i = i->parentItem()) {
        if (i->opacity() <= 0) {
            return false;
        }
    }
    return true;
}

// Objects named so anywhere under the window.
QList<QObject*> findNamed(QObject* root, const QString& name)
{
    QList<QObject*> found;
    for (QObject* o : allObjects(root)) {
        if (o->objectName() == name) {
            found << o;
        }
    }
    return found;
}

// The tablet panel's sections, by the drawer's tab order.
const QStringList kSections{QStringLiteral("DSP"),      QStringLiteral("Audio"),  QStringLiteral("TX"),
                            QStringLiteral("ESSB"),     QStringLiteral("CW"),     QStringLiteral("View"),
                            QStringLiteral("Profiles"), QStringLiteral("Memory"), QStringLiteral("Log"),
                            QStringLiteral("Digi"),     QStringLiteral("FreeDV")};

// Time a command's effect needs before the next one (open animations).
constexpr int kSettleMs = 900;

} // namespace

ShotPlan::ShotPlan(QQmlApplicationEngine& qml, MobileEngine& engine, const QString& planFile, const QString& outDir,
                   QObject* parent)
    : QObject(parent)
    , m_qml(qml)
    , m_engine(engine)
    , m_outDir(outDir)
{
    QFile file(planFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QString& raw : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
            const QString line = raw.trimmed();
            if (!line.isEmpty() && !line.startsWith(QLatin1Char('#'))) {
                m_lines << line;
            }
        }
    } else {
        qWarning() << "shot-plan: cannot read" << planFile;
        m_failed = true;
    }
    QDir().mkpath(m_outDir);
}

void ShotPlan::start()
{
    QTimer::singleShot(0, this, &ShotPlan::step);
}

void ShotPlan::step()
{
    if (m_next >= m_lines.size()) {
        if (m_pinned) {
            m_engine.setPinnedSections(m_savedPinned);
        }
        qInfo() << "shot-plan: done" << (m_failed ? "(with failures)" : "");
        QCoreApplication::exit(m_failed ? 2 : 0);
        return;
    }
    const QString line = m_lines.at(m_next++);
    QStringList words = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString command = words.takeFirst();
    int delayMs = kSettleMs;
    run(command, words, &delayMs);
    QTimer::singleShot(delayMs, this, &ShotPlan::step);
}

void ShotPlan::closePopups()
{
    for (QObject* root : m_qml.rootObjects()) {
        // Guarded: a close can destroy others in the list (the TNF popup's
        // sample notch takes its spectrum handle with it).
        QList<QPointer<QObject>> objects;
        for (QObject* o : allObjects(root)) {
            objects << o;
        }
        for (const QPointer<QObject>& o : std::as_const(objects)) {
            if (!o) {
                continue;
            }
            const QMetaObject* mo = o->metaObject();
            if (mo->indexOfProperty("opened") >= 0 && mo->indexOfMethod("close()") >= 0 && o->property("opened").toBool()) {
                QMetaObject::invokeMethod(o.data(), "close");
            }
        }
        for (QObject* notice : findNamed(root, QStringLiteral("noSliceNotice"))) {
            notice->setProperty("preview", false);
        }
    }
}

// One line of the page's layout decisions and the info panels' geometry, to
// compare a run with an earlier one (mobile/tools/layout-diff.py). A changed
// decision shows there even where a picture looks fine at a glance: the
// iPhone Duo's inner screen dropped the info panel under the frequency in
// build 137's first audit (a black hole beside the frequency) and the
// pictures were passed.
void ShotPlan::logLayout()
{
    QStringList parts;
    for (QObject* root : m_qml.rootObjects()) {
        if (auto* window = qobject_cast<QQuickWindow*>(root)) {
            parts << QStringLiteral("window=%1x%2").arg(window->width()).arg(window->height());
        }
        for (QObject* o : allObjects(root)) {
            const auto* item = qobject_cast<QQuickItem*>(o);
            if (!item) {
                continue;
            }
            const QString type = QString::fromLatin1(o->metaObject()->className()).section(QLatin1Char('_'), 0, 0);
            // The tablet page's probes (shown at opacity 0, never seen): the
            // width each measures at its type size; the page fits the panel
            // from them.
            if (type == QLatin1String("InfoPanel") && o->objectName().startsWith(QLatin1String("infoProbe"))) {
                parts << QStringLiteral("Probe(%1 scale=%2 needs=%3)")
                             .arg(o->objectName())
                             .arg(QString::number(o->property("fontScale").toDouble(), 'f', 2))
                             .arg(qRound(item->implicitWidth()));
                continue;
            }
            if (!seen(item)) {
                continue;
            }
            if (type == QLatin1String("OperatePage") || type == QLatin1String("PhoneOperatePage")) {
                QStringList props;
                for (const char* name : {"landscape", "flowHeader", "infoUnder", "infoScale", "controlsFull"}) {
                    const QVariant v = o->property(name);
                    if (v.isValid()) {
                        props << QStringLiteral("%1=%2").arg(QLatin1String(name),
                            v.typeId() == QMetaType::Double ? QString::number(v.toDouble(), 'f', 2) : v.toString());
                    }
                }
                parts << type + QLatin1Char('(') + props.join(QLatin1Char(' ')) + QLatin1Char(')');
            } else if (type == QLatin1String("InfoPanel")) {
                const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
                parts << QStringLiteral("InfoPanel(x=%1 y=%2 w=%3 h=%4 needs=%5)")
                             .arg(qRound(r.x())).arg(qRound(r.y())).arg(qRound(r.width())).arg(qRound(r.height()))
                             .arg(qRound(item->implicitWidth()));
            }
        }
    }
    qInfo().noquote() << "shot-plan: layout" << parts.join(QStringLiteral("; "));
}

void ShotPlan::run(const QString& command, const QStringList& args, int* delayMs)
{
    const QString arg = args.value(0);
    auto* window = m_qml.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(m_qml.rootObjects().constFirst());
    if (command == QLatin1String("wait")) {
        *delayMs = std::max(0, arg.toInt());
    } else if (command == QLatin1String("shot")) {
        const QString path = QDir(m_outDir).filePath(arg + QStringLiteral(".png"));
        const bool saved = window && window->grabWindow().save(path);
        qInfo() << "shot-plan: shot" << arg << (saved ? "saved" : "FAILED");
        m_failed = m_failed || !saved;
        *delayMs = 200;
    } else if (command == QLatin1String("popup")) {
        const QStringList parts = arg.split(QLatin1Char(':'));
        const QString name = parts.value(0);
        const int section = parts.value(1, QStringLiteral("-1")).toInt();
        bool opened = false;
        for (QObject* root : m_qml.rootObjects()) {
            // Every visible match: a page can hold a hidden twin.
            for (QObject* chooser : findNamed(root, name)) {
                const auto* host = qvariant_cast<QQuickItem*>(chooser->property("parent"));
                if (host && !seen(host)) {
                    continue;
                }
                const bool hasPreview = chooser->metaObject()->indexOfMethod("preview()") >= 0;
                QMetaObject::invokeMethod(chooser, hasPreview ? "preview" : "open");
                if (section >= 0) {
                    chooser->setProperty("section", section);
                }
                opened = true;
            }
        }
        if (!opened) {
            qWarning() << "shot-plan: no visible" << name;
        }
    } else if (command == QLatin1String("close")) {
        closePopups();
        *delayMs = 600;
    } else if (command == QLatin1String("drawer")) {
        for (QObject* root : m_qml.rootObjects()) {
            if (QObject* drawer = root->findChild<QObject*>(QStringLiteral("controlsDrawer"))) {
                QMetaObject::invokeMethod(drawer, "open");
            }
            for (QObject* tabs : root->findChildren<QObject*>(QStringLiteral("controlsTabs"))) {
                tabs->setProperty("currentIndex", arg.toInt());
            }
        }
        *delayMs = 1200;
    } else if (command == QLatin1String("scroll")) {
        const qreal px = arg.toDouble();
        for (QObject* root : m_qml.rootObjects()) {
            for (QObject* scroll : findNamed(root, QStringLiteral("controlsTabScroll"))
                                       + findNamed(root, QStringLiteral("pinnedScroll"))) {
                auto* item = qobject_cast<QQuickItem*>(scroll);
                auto* flick = qvariant_cast<QObject*>(scroll->property("contentItem"));
                if (item && item->isVisible() && flick) {
                    const qreal room = flick->property("contentHeight").toReal() - item->height();
                    flick->setProperty("contentY", std::clamp(px, 0.0, std::max(0.0, room)));
                }
            }
        }
    } else if (command == QLatin1String("pin")) {
        QStringList pinned;
        for (const QString& index : arg.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            if (const QString name = kSections.value(index.toInt()); !name.isEmpty()) {
                pinned << name;
            }
        }
        if (!m_pinned) {
            m_savedPinned = m_engine.pinnedSections();
            m_pinned = true;
        }
        m_engine.setPinnedSections(pinned);
        *delayMs = 1200;
    } else if (command == QLatin1String("controls")) {
        m_engine.setControlsVisible(arg == QLatin1String("visible"));
        *delayMs = 1200;
    } else if (command == QLatin1String("frequency-dialog")) {
        for (QObject* root : m_qml.rootObjects()) {
            if (QObject* dialog = root->findChild<QObject*>(QStringLiteral("frequencyDialog"))) {
                QMetaObject::invokeMethod(dialog, "open");
            }
        }
    } else if (command == QLatin1String("no-slice")) {
        for (QObject* root : m_qml.rootObjects()) {
            for (QObject* notice : findNamed(root, QStringLiteral("noSliceNotice"))) {
                notice->setProperty("preview", arg == QLatin1String("on"));
            }
        }
    } else if (command == QLatin1String("preview-tx")) {
        m_engine.setProperty("previewTx", arg == QLatin1String("on"));
        *delayMs = 1200;
    } else if (command == QLatin1String("meter-style")) {
        const QStringList styles = arg.split(QLatin1Char(','));
        if (styles.size() == 2) {
            m_engine.setMeterStyle(QStringLiteral("left"), styles.at(0));
            m_engine.setMeterStyle(QStringLiteral("right"), styles.at(1));
        }
    } else if (command == QLatin1String("tap")) {
        // Through the window system, as a real press: an event sent straight
        // to the window keeps a press position of (0,0), so the panadapter's
        // DragHandler saw a drag and took the RF gain label's tap.
        const QStringList xy = arg.split(QLatin1Char(','));
        if (window && xy.size() == 2) {
            const QPointF p(xy.at(0).toDouble() * window->width(), xy.at(1).toDouble() * window->height());
            const QPointF g = window->mapToGlobal(p);
            QWindowSystemInterface::handleMouseEvent(window, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
            QWindowSystemInterface::handleMouseEvent(window, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
            QWindowSystemInterface::flushWindowSystemEvents();
        }
    } else if (command == QLatin1String("tapmhz") || command == QLatin1String("holdmhz")) {
        // On the spectrum at that frequency, a quarter down its height: a tap
        // at a known distance from a signal (CW autotune), wherever the view
        // is; holdmhz MHZ,MS keeps it pressed MS (a new notch).
        const QStringList parts = arg.split(QLatin1Char(','));
        const double mhz = parts.at(0).toDouble();
        const int holdMs = command == QLatin1String("holdmhz") && parts.size() == 2 ? parts.at(1).toInt() : 0;
        const double low = m_engine.panCenterMhz() - m_engine.panBandwidthMhz() / 2.0;
        QQuickItem* spectrum = nullptr;
        for (QObject* root : m_qml.rootObjects()) {
            for (QObject* o : allObjects(root)) {
                auto* item = qobject_cast<QQuickItem*>(o);
                if (item && seen(item) && QString::fromLatin1(o->metaObject()->className()).startsWith(QLatin1String("AetherSDR::mobile::SpectrumItem"))) {
                    spectrum = item;
                }
            }
        }
        if (window && spectrum && m_engine.panBandwidthMhz() > 0.0) {
            const QPointF local((mhz - low) / m_engine.panBandwidthMhz() * spectrum->width(), spectrum->height() * 0.12);
            const QPointF p = spectrum->mapToScene(local);
            const QPointF g = window->mapToGlobal(p);
            qInfo().noquote() << QStringLiteral("shot-plan: %1 at %2 MHz (x %3)").arg(holdMs > 0 ? QStringLiteral("hold") : QStringLiteral("tap"))
                                     .arg(mhz, 0, 'f', 6).arg(qRound(p.x()));
            QWindowSystemInterface::handleMouseEvent(window, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
            QWindowSystemInterface::flushWindowSystemEvents();
            if (holdMs > 0) {
                QPointer<QQuickWindow> w(window);
                QTimer::singleShot(holdMs, this, [w, p, g] {
                    if (w) {
                        QWindowSystemInterface::handleMouseEvent(w, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
                        QWindowSystemInterface::flushWindowSystemEvents();
                    }
                });
                *delayMs = holdMs + 1500;
            } else {
                QWindowSystemInterface::handleMouseEvent(window, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
                QWindowSystemInterface::flushWindowSystemEvents();
            }
        } else {
            qWarning() << "shot-plan: no spectrum to tap";
            m_failed = true;
        }
    } else if (command == QLatin1String("hold")) {
        // Press, keep it down MS, release: a held button's auto-repeat. The
        // frequency is logged before, at the release and when settled.
        const QStringList xym = arg.split(QLatin1Char(','));
        if (window && xym.size() == 3) {
            const QPointF p(xym.at(0).toDouble() * window->width(), xym.at(1).toDouble() * window->height());
            const QPointF g = window->mapToGlobal(p);
            const int ms = xym.at(2).toInt();
            qInfo() << "shot-plan: hold" << ms << "ms, frequency" << QString::number(m_engine.frequencyMhz(), 'f', 6);
            QWindowSystemInterface::handleMouseEvent(window, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
            QWindowSystemInterface::flushWindowSystemEvents();
            QPointer<QQuickWindow> w(window);
            QTimer::singleShot(ms, this, [this, w, p, g] {
                if (!w) {
                    return;
                }
                QWindowSystemInterface::handleMouseEvent(w, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
                QWindowSystemInterface::flushWindowSystemEvents();
                qInfo() << "shot-plan: released, frequency" << QString::number(m_engine.frequencyMhz(), 'f', 6);
                QTimer::singleShot(1500, this, [this] {
                    qInfo() << "shot-plan: settled, frequency" << QString::number(m_engine.frequencyMhz(), 'f', 6);
                });
            });
            *delayMs = ms + 2000;
        }
    } else if (command == QLatin1String("freq")) {
        // As the frequency entry: the view follows (a plain set left the
        // slice outside the pan, which the radio refuses).
        m_engine.tuneTo(arg.toDouble());
        *delayMs = 1500;
    } else if (command == QLatin1String("mode")) {
        m_engine.setMode(arg);
        *delayMs = 1500;
    } else if (command == QLatin1String("ready")) {
        // Until connected with a slice (at most 30 s): a slow connect left a
        // fixed wait short and the plan's first commands went to no slice.
        if (m_engine.connectionState() != QLatin1String("connected") || m_engine.activeSliceId() < 0
            || m_engine.frequencyMhz() <= 0.0) {
            if (++m_readyTries <= 60) {
                --m_next;  // this line again
                *delayMs = 500;
                return;
            }
            qWarning() << "shot-plan: not ready after 30 s";
            m_failed = true;
        }
        m_readyTries = 0;
        *delayMs = 4000;  // the display fills
    } else if (command == QLatin1String("setup")) {
        const int eq = arg.indexOf(QLatin1Char('='));
        if (eq > 0) {
            m_engine.setRadioSetup(arg.left(eq), arg.mid(eq + 1));
        }
        *delayMs = 1000;
    } else if (command == QLatin1String("tapname")) {
        // A finger on the named control's middle, through the window system
        // (click only emits its clicked): whatever covers it, a popup's
        // press-outside close included, acts as for a user.
        QQuickItem* target = nullptr;
        for (QObject* root : m_qml.rootObjects()) {
            for (QObject* o : findNamed(root, arg)) {
                auto* item = qobject_cast<QQuickItem*>(o);
                if (item && item->isVisible()) {
                    target = item;
                }
            }
        }
        if (window && target) {
            const QPointF p = target->mapToScene(QPointF(target->width() / 2.0, target->height() / 2.0));
            const QPointF g = window->mapToGlobal(p);
            qInfo().noquote() << QStringLiteral("shot-plan: tap %1 at %2,%3").arg(arg).arg(qRound(p.x())).arg(qRound(p.y()));
            QWindowSystemInterface::handleMouseEvent(window, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
            QWindowSystemInterface::handleMouseEvent(window, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
            QWindowSystemInterface::flushWindowSystemEvents();
        } else {
            qWarning() << "shot-plan: no visible" << arg;
            m_failed = true;
        }
    } else if (command == QLatin1String("click")) {
        bool clicked = false;
        for (QObject* root : m_qml.rootObjects()) {
            for (QObject* button : findNamed(root, arg)) {
                const auto* item = qobject_cast<QQuickItem*>(button);
                if (item && item->isVisible()) {
                    QMetaObject::invokeMethod(button, "clicked");
                    clicked = true;
                }
            }
        }
        if (!clicked) {
            qWarning() << "shot-plan: no visible" << arg;
        }
    } else if (command == QLatin1String("span")) {
        // The pan this many kHz wide, centred on the slice: a plan's
        // frequencies then land where it expects (the radio keeps each
        // client's last zoom, 3 kHz one run, 200 the next).
        if (m_engine.panBandwidthMhz() > 0.0) {
            m_engine.zoom(arg.toDouble() / 1000.0 / m_engine.panBandwidthMhz());
        }
        *delayMs = 2000;
    } else if (command == QLatin1String("a11y") || command == QLatin1String("a11ypress")
               || command == QLatin1String("a11yinc") || command == QLatin1String("a11ydec")) {
        // What a screen reader is given: a11y on turns accessibility on (the
        // controls name themselves only then); a11y logs every element in
        // view with its role and name, and the buttons without a name;
        // a11ypress NAME does the named element's press action, as a
        // VoiceOver or TalkBack double tap.
        // A name can have spaces: the rest of the line.
        const QString name = args.join(QLatin1Char(' '));
        if (name == QLatin1String("on")) {
            QAccessible::setActive(true);
            *delayMs = 1000;
            return;
        }
        QList<QAccessibleInterface*> found;
        int named = 0;
        int unnamed = 0;
        const std::function<void(QAccessibleInterface*, int)> walk = [&](QAccessibleInterface* a, int depth) {
            if (!a || !a->isValid() || depth > 60 || a->state().invisible) {
                return;
            }
            // A password field gives no name (Qt's choice); its description
            // (the placeholder) says what it is.
            const QString text = a->state().passwordEdit && a->text(QAccessible::Name).isEmpty()
                ? a->text(QAccessible::Description) : a->text(QAccessible::Name);
            const QAccessible::Role role = a->role();
            const bool actionable = role == QAccessible::Button || role == QAccessible::CheckBox
                || role == QAccessible::RadioButton || role == QAccessible::Slider || role == QAccessible::ComboBox
                || role == QAccessible::EditableText || role == QAccessible::MenuItem;
            if (command != QLatin1String("a11y")) {
                if (a->text(QAccessible::Name) == name) {
                    found << a;
                }
            } else if (actionable || !text.isEmpty()) {
                const QRect r = a->rect();
                if (actionable && text.trimmed().isEmpty()) {
                    ++unnamed;
                } else if (actionable) {
                    ++named;
                }
                qInfo().noquote() << QStringLiteral("shot-plan: a11y %1 %2 \"%3\" at %4,%5 %6x%7")
                                         .arg(actionable && text.trimmed().isEmpty() ? QStringLiteral("UNNAMED") : QStringLiteral("-"))
                                         .arg(int(role), 0, 16).arg(text).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
            }
            for (int i = 0; i < a->childCount(); ++i) {
                walk(a->child(i), depth + 1);
            }
        };
        if (window) {
            walk(QAccessible::queryAccessibleInterface(window), 0);
        }
        if (command != QLatin1String("a11y")) {
            QAccessibleActionInterface* action = found.isEmpty() ? nullptr : found.first()->actionInterface();
            if (action) {
                qInfo().noquote() << "shot-plan:" << command << name << "of" << found.size();
                action->doAction(command == QLatin1String("a11yinc")   ? QAccessibleActionInterface::increaseAction()
                                 : command == QLatin1String("a11ydec") ? QAccessibleActionInterface::decreaseAction()
                                                                       : QAccessibleActionInterface::pressAction());
            } else {
                qWarning().noquote() << "shot-plan:" << command << "no element" << name << "with that action";
                m_failed = true;
            }
        } else {
            qInfo().noquote() << QStringLiteral("shot-plan: a11y %1 controls named, %2 without a name").arg(named).arg(unnamed);
        }
    } else if (command == QLatin1String("txchain")) {
        QStringList kv;
        const QVariantMap chain = m_engine.txChain();
        for (auto it = chain.cbegin(); it != chain.cend(); ++it) {
            kv << it.key() + QLatin1Char('=') + it.value().toString();
        }
        qInfo().noquote() << "shot-plan: txchain" << kv.join(QLatin1Char(' '));
    } else if (command == QLatin1String("tnfs")) {
        QStringList list;
        for (const QVariant& v : m_engine.tnfs()) {
            const QVariantMap t = v.toMap();
            list << QStringLiteral("%1@%2/%3Hz/d%4%5").arg(t.value(QStringLiteral("id")).toInt())
                        .arg(t.value(QStringLiteral("freq")).toDouble(), 0, 'f', 6).arg(t.value(QStringLiteral("width")).toInt())
                        .arg(t.value(QStringLiteral("depth")).toInt()).arg(t.value(QStringLiteral("permanent")).toBool() ? QStringLiteral("/p") : QString());
        }
        qInfo().noquote() << "shot-plan: tnfs" << list.size() << list.join(QLatin1Char(' ')) << "frequency"
                          << QString::number(m_engine.frequencyMhz(), 'f', 6) << "pan"
                          << QString::number(m_engine.panCenterMhz() - m_engine.panBandwidthMhz() / 2.0, 'f', 5)
                          << QString::number(m_engine.panCenterMhz() + m_engine.panBandwidthMhz() / 2.0, 'f', 5);
    } else if (command == QLatin1String("layout")) {
        logLayout();
    } else if (command == QLatin1String("size")) {
        const QStringList wh = arg.split(QLatin1Char('x'));
        if (window && wh.size() == 2) {
            window->resize(wh.at(0).toInt(), wh.at(1).toInt());
        }
    } else {
        qWarning() << "shot-plan: unknown command" << command;
        m_failed = true;
    }
}

} // namespace AetherSDR::mobile
