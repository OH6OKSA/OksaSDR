#include "ShotPlan.h"

#include "MobileEngine.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QRegularExpression>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>
#include <functional>
#include <memory>

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
    QString command = words.takeFirst();
    if ((command == QLatin1String("?") || command == QLatin1String("!")) && !words.isEmpty()) {
        if ((command == QLatin1String("?")) != m_condition) {
            qInfo().noquote() << "shot-plan: skipped" << line;
            QTimer::singleShot(0, this, &ShotPlan::step);
            return;
        }
        command = words.takeFirst();
    }
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
    } else if (command == QLatin1String("rttypairs") || command == QLatin1String("tappair") || command == QLatin1String("spotpair")) {
        // RTTY signals in view: SpectrumItem::rttyMarkNear every 20 px across
        // the spectrum, marks within 30 Hz as one. tappair N then taps the
        // N-th (from 1) between its two traces, as a user would; spotpair N
        // puts an RTTY test spot there (a spot giving the signal's centre).
        QQuickItem* spectrum = nullptr;
        for (QObject* root : m_qml.rootObjects()) {
            for (QObject* o : allObjects(root)) {
                auto* item = qobject_cast<QQuickItem*>(o);
                if (item && seen(item) && QString::fromLatin1(o->metaObject()->className()).startsWith(QLatin1String("AetherSDR::mobile::SpectrumItem"))) {
                    spectrum = item;
                }
            }
        }
        if (!spectrum || m_engine.panBandwidthMhz() <= 0.0) {
            qWarning() << "shot-plan: no spectrum for rttypairs";
            m_failed = true;
            return;
        }
        const double low = m_engine.panCenterMhz() - m_engine.panBandwidthMhz() / 2.0;
        const double shift = m_engine.rtty().value(QStringLiteral("shiftHz")).toDouble();
        QList<double> marks;
        for (double x = 10.0; x < spectrum->width() - 10.0; x += 20.0) {
            double mark = -1.0;
            QMetaObject::invokeMethod(spectrum, "rttyMarkNear", Q_RETURN_ARG(double, mark),
                                      Q_ARG(double, low + x / spectrum->width() * m_engine.panBandwidthMhz()),
                                      Q_ARG(double, 0.0), Q_ARG(double, shift));
            if (mark > 0.0 && std::none_of(marks.cbegin(), marks.cend(), [mark](double m) { return std::abs(m - mark) < 30e-6; })) {
                marks << mark;
            }
        }
        QStringList list;
        for (double m : marks) {
            list << QString::number(m, 'f', 5);
        }
        qInfo().noquote() << "shot-plan: rtty pairs (marks)" << list.join(QLatin1Char(' '));
        if (command != QLatin1String("rttypairs")) {
            const int n = arg.toInt();
            if (n < 1 || n > marks.size()) {
                qWarning() << "shot-plan: no rtty pair" << n;
                m_failed = true;
                return;
            }
            const QString centre = QString::number(marks.at(n - 1) - shift / 2e6, 'f', 6);
            if (command == QLatin1String("tappair")) {
                run(QStringLiteral("tapmhz"), {centre}, delayMs);
            } else {
                run(QStringLiteral("spottest"), {QStringLiteral("rtty=") + centre}, delayMs);
            }
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
    } else if (command == QLatin1String("slice")) {
        // That slice (by letter) active only; TX stays where it is (txslice
        // makes it both: a restore after it went to that slice, not to the
        // one tested).
        for (const QVariant& v : m_engine.slices()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("letter")).toString() == arg) {
                m_engine.selectSlice(m.value(QStringLiteral("id")).toInt());
                qInfo().noquote() << "shot-plan: slice" << arg << "active";
            }
        }
        *delayMs = 1500;
    } else if (command == QLatin1String("txslice")) {
        // That slice (by letter) active and the TX slice, as the operator
        // had it before a test moved TX.
        for (const QVariant& v : m_engine.slices()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("letter")).toString() == arg) {
                const int id = m.value(QStringLiteral("id")).toInt();
                m_engine.selectSlice(id);
                m_engine.makeTxSlice(id);
                qInfo().noquote() << "shot-plan: slice" << arg << "active and TX";
            }
        }
        *delayMs = 1500;
    } else if (command == QLatin1String("rfpower")) {
        // The TX power (W), to put it back after a run cut short.
        m_engine.setRfPower(arg.toInt());
        qInfo() << "shot-plan: RF power" << arg.toInt() << "W";
        *delayMs = 1500;
    } else if (command == QLatin1String("status")) {
        // OBJECT | K=V K=V: as the radio's status, so a page can be tried
        // with hardware this radio does not have; commands it sends still
        // go to the radio.
        const QString line = args.join(QLatin1Char(' '));
        const qsizetype bar = line.indexOf(QLatin1String(" | "));
        const QString object = (bar >= 0 ? line.left(bar) : line).trimmed();
        QMap<QString, QString> kvs;
        for (const QString& kv : (bar >= 0 ? line.mid(bar + 3) : QString()).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const qsizetype eq = kv.indexOf(QLatin1Char('='));
            if (eq > 0) {
                kvs.insert(kv.left(eq), kv.mid(eq + 1));
            }
        }
        m_engine.injectStatusForTest(object, kvs);
        qInfo().noquote() << "shot-plan: status" << object << kvs.keys().join(QLatin1Char(','));
        *delayMs = 300;
    } else if (command == QLatin1String("ptt")) {
        // The radio keyed as PTT held, for what must not happen while it
        // transmits (the amplifier's commands); a DIGU TX slice with no
        // audio sends no carrier.
        m_engine.setPtt(arg == QLatin1String("on"), QStringLiteral("shot plan"));
        qInfo().noquote() << "shot-plan: PTT" << arg;
        *delayMs = 1500;
    } else if (command == QLatin1String("amp")) {
        // The amplifier's address (Radio Setup › Amp); "off" forgets it,
        // "pause" / "resume" are its Read the amplifier switch.
        if (arg == QLatin1String("pause") || arg == QLatin1String("resume")) {
            m_engine.setAmpOn(arg == QLatin1String("resume"));
        } else {
            m_engine.setAmpAddress(arg == QLatin1String("off") ? QString() : arg);
        }
        qInfo().noquote() << "shot-plan: amp" << arg;
        *delayMs = 500;
    } else if (command == QLatin1String("appstate")) {
        // The application state as the system reports it (background and
        // back): active, inactive, hidden or suspended.
        const Qt::ApplicationState state = arg == QLatin1String("suspended") ? Qt::ApplicationSuspended
            : arg == QLatin1String("hidden")   ? Qt::ApplicationHidden
            : arg == QLatin1String("inactive") ? Qt::ApplicationInactive
                                               : Qt::ApplicationActive;
        QWindowSystemInterface::handleApplicationStateChanged<QWindowSystemInterface::SynchronousDelivery>(state, true);
        qInfo().noquote() << "shot-plan: app state" << arg << QGuiApplication::applicationState();
        *delayMs = 500;
    } else if (command == QLatin1String("ampstate")) {
        // What the Amp tab shows: the link and every reading.
        const QVariantMap a = m_engine.amp();
        QStringList parts;
        for (auto it = a.cbegin(); it != a.cend(); ++it) {
            parts << it.key() + QLatin1Char('=') + it.value().toString();
        }
        qInfo().noquote() << "shot-plan: amp" << parts.join(QLatin1Char(' '));
    } else if (command == QLatin1String("slices")) {
        // Every slice on the radio, the TX slice and the power: what a test
        // left behind.
        for (const QVariant& v : m_engine.slices()) {
            const QVariantMap m = v.toMap();
            qInfo().noquote() << "shot-plan: slice" << m.value(QStringLiteral("letter")).toString()
                              << QString::number(m.value(QStringLiteral("frequency")).toDouble(), 'f', 6)
                              << m.value(QStringLiteral("mode")).toString()
                              << (m.value(QStringLiteral("tx")).toBool() ? "TX" : "")
                              << (m.value(QStringLiteral("active")).toBool() ? "active" : "");
        }
        qInfo() << "shot-plan: RF power" << m_engine.rfPower() << "W";
    } else if (command == QLatin1String("waittx") || command == QLatin1String("waitcall")
               || command == QLatin1String("waitover")) {
        // At most MS: waittx until this app's text has gone out; waitcall
        // until the received text holds a call (after DE, or twice) and has
        // stopped coming, the call becoming the one worked; waitover until the other station's
        // over ends (K, KN, SK or BK after our last [TX] line, then 1.5 s
        // with nothing new). Sets the condition "?" and "!" lines read.
        if (!m_waiting) {
            m_waiting = true;
            m_waitClock.start();
            m_growthClock.start();
            m_lastTextLen = m_engine.rttyDecodedText().size();
        }
        const QString text = m_engine.rttyDecodedText();
        if (text.size() != m_lastTextLen) {
            m_lastTextLen = text.size();
            m_growthClock.restart();
        }
        bool done = false;
        QString what;
        if (command == QLatin1String("waittx")) {
            done = !m_engine.textTransmitting();
        } else if (command == QLatin1String("waitcall")) {
            // Once the caller's over has ended (1.5 s with nothing new): a
            // call read half way ("OH2XY" of OH2XYZ) is not taken.
            // "waitcall MS cq": only a station calling CQ (its over holds
            // CQ), not one already in a QSO.
            const qsizetype tx = text.lastIndexOf(QLatin1String("[TX]"));
            const QString since = tx < 0 ? text : text.mid(tx);
            const bool cqOk = args.value(1) != QLatin1String("cq")
                || since.contains(QRegularExpression(QStringLiteral("\\bCQ\\b"), QRegularExpression::CaseInsensitiveOption));
            const QStringList calls = m_engine.textCalls();
            if (!calls.isEmpty() && cqOk && m_growthClock.elapsed() >= 1500) {
                done = true;
                what = calls.first();
                m_engine.setWorkedCall(what);
            }
        } else {
            const qsizetype tx = text.lastIndexOf(QLatin1String("[TX]"));
            const qsizetype lineEnd = tx < 0 ? 0 : text.indexOf(QLatin1Char('\n'), tx);
            const QString heard = lineEnd < 0 ? QString() : text.mid(lineEnd + 1).trimmed();
            // PSK operators type in lower case ("pse k").
            static const QRegularExpression over(QStringLiteral("(^|\\s)(K|KN|SK|BK|PSE K)$"), QRegularExpression::CaseInsensitiveOption);
            done = heard.size() >= 6 && over.match(heard).hasMatch() && m_growthClock.elapsed() >= 1500;
            what = heard.right(80);
        }
        if (done) {
            m_waiting = false;
            // waittx leaves the condition as the last call / over wait set it.
            if (command != QLatin1String("waittx")) {
                m_condition = true;
            }
            qInfo().noquote() << QStringLiteral("shot-plan: %1 done after %2 s %3").arg(command).arg(m_waitClock.elapsed() / 1000.0, 0, 'f', 1).arg(what);
            *delayMs = 300;
            return;
        }
        if (m_waitClock.elapsed() >= arg.toInt()) {
            m_waiting = false;
            if (command != QLatin1String("waittx")) {
                m_condition = false;
            }
            qInfo().noquote() << QStringLiteral("shot-plan: %1 timed out after %2 s").arg(command).arg(m_waitClock.elapsed() / 1000.0, 0, 'f', 1);
            *delayMs = 100;
            return;
        }
        --m_next;  // this line again
        *delayMs = 250;
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
    } else if (command == QLatin1String("tapname") || command == QLatin1String("showname")
               || command == QLatin1String("holdname")) {
        // A finger on the named control's middle, through the window system
        // (click only emits its clicked): whatever covers it, a popup's
        // press-outside close included, acts as for a user. showname only
        // scrolls it into view; holdname keeps the finger down 1.2 s (a
        // press and hold).
        QQuickItem* target = nullptr;
        for (QObject* root : m_qml.rootObjects()) {
            for (QObject* o : findNamed(root, arg)) {
                auto* item = qobject_cast<QQuickItem*>(o);
                if (item && item->isVisible()) {
                    target = item;
                }
            }
        }
        // Below or above its scrolling panel's view (or the window), as a
        // user would: scrolled into view first. A point still off the window
        // is not tapped (it would land on whatever is there).
        for (QQuickItem* up = target ? target->parentItem() : nullptr; up; up = up->parentItem()) {
            if (up->inherits("QQuickFlickable")) {
                auto* content = qvariant_cast<QQuickItem*>(up->property("contentItem"));
                if (content) {
                    const qreal y = target->mapToItem(content, QPointF(0, 0)).y();
                    const qreal view = up->height();
                    const qreal top = up->property("contentY").toReal();
                    if (y < top || y + target->height() > top + view) {
                        const qreal maxY = std::max<qreal>(0, up->property("contentHeight").toReal() - view);
                        up->setProperty("contentY", std::clamp<qreal>(y - (view - target->height()) / 2.0, 0, maxY));
                        QCoreApplication::processEvents();
                    }
                }
                break;
            }
        }
        if (window && target && command == QLatin1String("showname")) {
            const QPointF p = target->mapToScene(QPointF(0, 0));
            qInfo().noquote() << QStringLiteral("shot-plan: show %1 at %2,%3").arg(arg).arg(qRound(p.x())).arg(qRound(p.y()));
            return;
        }
        if (window && target) {
            const QPointF p = target->mapToScene(QPointF(target->width() / 2.0, target->height() / 2.0));
            if (p.x() < 0 || p.y() < 0 || p.x() >= window->width() || p.y() >= window->height()) {
                qWarning().noquote() << QStringLiteral("shot-plan: %1 is off the window at %2,%3: not tapped")
                                            .arg(arg).arg(qRound(p.x())).arg(qRound(p.y()));
                m_failed = true;
                return;
            }
            const QPointF g = window->mapToGlobal(p);
            qInfo().noquote() << QStringLiteral("shot-plan: %1 %2 at %3,%4").arg(command == QLatin1String("holdname") ? QStringLiteral("hold") : QStringLiteral("tap"), arg).arg(qRound(p.x())).arg(qRound(p.y()));
            QWindowSystemInterface::handleMouseEvent(window, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
            QWindowSystemInterface::flushWindowSystemEvents();
            if (command == QLatin1String("holdname")) {
                QPointer<QQuickWindow> w(window);
                QTimer::singleShot(1200, [w, p, g] {
                    if (w) {
                        QWindowSystemInterface::handleMouseEvent(w, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
                        QWindowSystemInterface::flushWindowSystemEvents();
                    }
                });
                *delayMs = std::max(*delayMs, 1600);
                return;
            }
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
    } else if (command == QLatin1String("focussweep")) {
        // Every control in view that takes the keyboard on a press (a
        // slider, a switch, a ComboBox: Qt's ClickFocus), text entry left
        // out: given the focus as a tap would give it, then looked at 0.8 s
        // later, the page should have the keyboard back (Space PTT
        // and the arrows on an iPad). No press, so nothing changes. One line
        // per control that kept it, then the count.
        QList<QPointer<QQuickItem>> controls;
        for (QObject* root : m_qml.rootObjects()) {
            for (QObject* o : allObjects(root)) {
                auto* item = qobject_cast<QQuickItem*>(o);
                if (!item || !o->inherits("QQuickControl") || o->inherits("QQuickTextField")
                    || o->inherits("QQuickTextArea") || !item->isVisible() || !seen(item)) {
                    continue;
                }
                if (o->property("focusPolicy").toInt() & Qt::ClickFocus) {
                    controls << item;
                }
            }
        }
        QPointer<QQuickWindow> w(window);
        auto kept = std::make_shared<int>(0);
        // One at a time, each looked at 0.8 s after it got the focus (the
        // page's own check runs every 0.3 s): a schedule laid out up front
        // looked before the hand-back while a frame of the pinned panel's
        // 5000 px held the event loop for a second.
        auto step = std::make_shared<std::function<void(int)>>();
        *step = [this, w, controls, kept, step](int i) {
            if (i >= controls.size()) {
                qInfo().noquote() << "shot-plan: focussweep" << controls.size() << "controls," << *kept << "kept the keyboard";
                return;
            }
            const QPointer<QQuickItem> control = controls.at(i);
            if (!w || !control) {
                (*step)(i + 1);
                return;
            }
            control->forceActiveFocus(Qt::MouseFocusReason);
            QTimer::singleShot(800, this, [w, control, kept, step, i] {
                if (w && control) {
                    QQuickItem* now = w->activeFocusItem();
                    if (now == control || (now && control->isAncestorOf(now))) {
                        ++*kept;
                        // Where it is: the nearest named ancestors, and on screen.
                        QStringList path;
                        for (QQuickItem* p = control->parentItem(); p && path.size() < 3; p = p->parentItem()) {
                            if (!p->objectName().isEmpty()) {
                                path << p->objectName();
                            }
                        }
                        const QPointF at = control->mapToScene(QPointF(0, 0));
                        QAccessibleInterface* a11y = QAccessible::queryAccessibleInterface(control);
                        qInfo().noquote() << "shot-plan: focussweep kept:" << control->metaObject()->className()
                                          << control->objectName() << control->property("text").toString()
                                          << (a11y ? a11y->text(QAccessible::Name) : QString())
                                          << "in" << path.join(QLatin1Char('/')) << "at" << int(at.x()) << int(at.y());
                    }
                }
                (*step)(i + 1);
            });
        };
        (*step)(0);
        // The plan waits for the sweep (a control in 0.8 s and a busy
        // frame or so).
        *delayMs = int(controls.size()) * 1300 + 1500;
    } else if (command == QLatin1String("focusaudit")) {
        // Every text field in view, as a finger would: scrolled into view,
        // tapped (it should take the keyboard), then a tap at FX,FY outside
        // it (it should let go, the page keeping the keyboard: Space PTT on
        // an iPad). One line per field.
        const QStringList xy = arg.split(QLatin1Char(','));
        if (!window || xy.size() != 2) {
            m_failed = true;
            return;
        }
        const QPointF neutral(xy.at(0).toDouble() * window->width(), xy.at(1).toDouble() * window->height());
        QList<QPointer<QQuickItem>> fields;
        for (QObject* root : m_qml.rootObjects()) {
            for (QObject* o : allObjects(root)) {
                auto* item = qobject_cast<QQuickItem*>(o);
                if (item && (o->inherits("QQuickTextInput") || o->inherits("QQuickTextEdit")) && seen(item)
                    && !o->property("readOnly").toBool() && item->width() > 4 && item->height() > 4) {
                    fields << item;
                }
            }
        }
        const auto press = [](QQuickWindow* w, const QPointF& p) {
            const QPointF g = w->mapToGlobal(p);
            QWindowSystemInterface::handleMouseEvent(w, p, g, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress);
            QWindowSystemInterface::handleMouseEvent(w, p, g, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease);
            QWindowSystemInterface::flushWindowSystemEvents();
        };
        QPointer<QQuickWindow> w(window);
        int at = 0;
        for (const QPointer<QQuickItem>& field : fields) {
            // Into view: the nearest scrolling ancestor brought to it.
            QTimer::singleShot(at, this, [field] {
                if (!field) {
                    return;
                }
                for (QQuickItem* p = field->parentItem(); p; p = p->parentItem()) {
                    if (p->inherits("QQuickFlickable")) {
                        auto* content = p->property("contentItem").value<QQuickItem*>();
                        if (content) {
                            const QPointF c = field->mapToItem(content, QPointF(0, field->height() / 2));
                            const qreal maxY = std::max(0.0, p->property("contentHeight").toReal() - p->height());
                            p->setProperty("contentY", std::clamp(c.y() - p->height() / 2, 0.0, maxY));
                        }
                        break;
                    }
                }
            });
            QTimer::singleShot(at + 300, this, [this, w, field, press] {
                if (!w || !field) {
                    return;
                }
                const QPointF c = field->mapToScene(QPointF(field->width() / 2, field->height() / 2));
                if (c.x() < 0 || c.y() < 0 || c.x() > w->width() || c.y() > w->height()) {
                    qInfo().noquote() << "shot-plan: focusaudit" << field->property("placeholderText").toString() << "out of view";
                    return;
                }
                press(w, c);
            });
            QTimer::singleShot(at + 650, this, [this, w, field, neutral, press] {
                if (!w || !field) {
                    return;
                }
                const bool took = QGuiApplication::focusObject() == field;
                field->setProperty("auditTook", took);
                press(w, neutral);
            });
            QTimer::singleShot(at + 1000, this, [this, field] {
                if (!field) {
                    return;
                }
                QObject* now = QGuiApplication::focusObject();
                const bool released = !(now && (now->inherits("QQuickTextInput") || now->inherits("QQuickTextEdit")));
                QString name = field->property("placeholderText").toString();
                if (name.isEmpty()) {
                    name = field->objectName().isEmpty() ? QString::fromLatin1(field->metaObject()->className()) : field->objectName();
                }
                const bool took = field->property("auditTook").toBool();
                qInfo().noquote() << QStringLiteral("shot-plan: focusaudit %1 \"%2\" took %3 released %4 page %5")
                                         .arg(took && released && m_engine.keyboardOnPage() ? QStringLiteral("ok  ") : QStringLiteral("FAIL"))
                                         .arg(name).arg(took ? "yes" : "no").arg(released ? "yes" : "no")
                                         .arg(m_engine.keyboardOnPage() ? "yes" : "no");
            });
            at += 1200;
        }
        qInfo().noquote() << "shot-plan: focusaudit" << fields.size() << "text fields";
        *delayMs = at + 500;
    } else if (command == QLatin1String("focus")) {
        // Where the keyboard is: Space PTT on an iPad needs the page to have
        // it (Engine.keyboardOnPage); and the overlay's visible children,
        // which keep it from the page (OperateKeys.popupOpen).
        QStringList overlay;
        QString active = QStringLiteral("none");
        if (window) {
            if (QQuickItem* item = window->activeFocusItem()) {
                active = QString::fromLatin1(item->metaObject()->className()) + QLatin1Char(' ') + item->objectName();
            }
            for (QQuickItem* child : window->contentItem()->childItems()) {
                if (QString::fromLatin1(child->metaObject()->className()).contains(QLatin1String("Overlay"))) {
                    for (QQuickItem* c : child->childItems()) {
                        if (c->isVisible()) {
                            overlay << QString::fromLatin1(c->metaObject()->className()) + QLatin1Char(' ') + c->objectName();
                        }
                    }
                }
            }
        }
        qInfo().noquote() << "shot-plan: focus keyboardOnPage" << m_engine.keyboardOnPage() << "active" << active
                          << "overlay visible:" << overlay.join(QStringLiteral(", "));
    } else if (command == QLatin1String("worked")) {
        // CALL[,BAND,MODE]: the worked-before view (MobileEngine::workedAudit).
        const QStringList parts = arg.split(QLatin1Char(','));
        qInfo().noquote() << "shot-plan: worked"
                          << m_engine.workedAudit(parts.value(0), parts.value(1, QStringLiteral("20m")),
                                                  parts.value(2, QStringLiteral("FT8")));
        *delayMs = 100;
    } else if (command == QLatin1String("qso")) {
        // What a QSO logged now gets: the Log tab's frequency, mode and band.
        const QVariantMap d = m_engine.qsoDefaults();
        qInfo().noquote() << "shot-plan: qso" << QString::number(d.value(QStringLiteral("freq")).toDouble(), 'f', 6)
                          << d.value(QStringLiteral("mode")).toString() << d.value(QStringLiteral("band")).toString()
                          << "rst" << d.value(QStringLiteral("rst")).toString() << "| slice"
                          << QString::number(m_engine.frequencyMhz(), 'f', 6) << m_engine.mode();
        *delayMs = 100;
    } else if (command == QLatin1String("logqso")) {
        // KEY=VALUE | KEY=VALUE ...: MobileEngine::logQso with those fields
        // (call, rstSent, rstRcvd, timeOn HHMM, comment...), as the Log tab
        // sends them: a QSO a run worked but could not log itself.
        QVariantMap fields;
        for (const QString& part : args.join(QLatin1Char(' ')).split(QLatin1Char('|'))) {
            const QString key = part.section(QLatin1Char('='), 0, 0).trimmed();
            if (!key.isEmpty()) {
                fields.insert(key, part.section(QLatin1Char('='), 1).trimmed());
            }
        }
        qInfo().noquote() << "shot-plan: logqso" << fields.value(QStringLiteral("call")).toString() << "->" << m_engine.logQso(fields);
        *delayMs = 3000;
    } else if (command == QLatin1String("preview")) {
        // TEXT as a Digi macro would send it now (MobileEngine::previewText).
        qInfo().noquote() << "shot-plan: preview" << m_engine.workedCall() << "->" << m_engine.previewText(args.join(QLatin1Char(' ')));
        *delayMs = 100;
    } else if (command == QLatin1String("logread")) {
        // "on" / "off" sets Worked before from the logbook, "now" reads what
        // is new, nothing logs its state.
        if (arg == QLatin1String("on") || arg == QLatin1String("off")) {
            m_engine.setOnlineLogRead(arg == QLatin1String("on"));
        } else if (arg == QLatin1String("now")) {
            m_engine.onlineLogRead();
        }
        const QVariantMap log = m_engine.onlineLog();
        qInfo().noquote() << "shot-plan: logread" << arg << "read" << log.value(QStringLiteral("read")).toBool()
                          << "reading" << log.value(QStringLiteral("reading")).toBool() << "known"
                          << log.value(QStringLiteral("known")).toInt() << "status"
                          << log.value(QStringLiteral("readStatusKind")).toString()
                          << log.value(QStringLiteral("readStatus")).toString();
        *delayMs = 100;
    } else if (command == QLatin1String("qrzlog")) {
        // QRZ.com Logbook: "key=K" saves a key, "on" / "off" sets Worked
        // before from the logbook, "now" reads what is new, "insert=CALL"
        // queues a test record, "remove" clears it all; then its state.
        if (arg.startsWith(QLatin1String("key="))) {
            m_engine.setQrzLogKey(arg.mid(4));
        } else if (arg == QLatin1String("on") || arg == QLatin1String("off")) {
            m_engine.setQrzLogRead(arg == QLatin1String("on"));
        } else if (arg == QLatin1String("now")) {
            m_engine.qrzLogRead();
        } else if (arg.startsWith(QLatin1String("insert="))) {
            m_engine.devQrzInsert(arg.mid(7));
        } else if (arg == QLatin1String("remove")) {
            m_engine.clearQrzLog();
        }
        const QVariantMap q = m_engine.qrzLog();
        qInfo().noquote() << "shot-plan: qrzlog" << arg.section(QLatin1Char('='), 0, 0) << "| key" << q.value(QStringLiteral("hasKey")).toBool()
                          << "pending" << q.value(QStringLiteral("pending")).toInt() << "status"
                          << q.value(QStringLiteral("statusKind")).toString() << q.value(QStringLiteral("status")).toString()
                          << "| read" << q.value(QStringLiteral("read")).toBool() << "reading" << q.value(QStringLiteral("reading")).toBool()
                          << "known" << q.value(QStringLiteral("known")).toInt() << q.value(QStringLiteral("readStatusKind")).toString()
                          << q.value(QStringLiteral("readStatus")).toString();
        *delayMs = 100;
    } else if (command == QLatin1String("shared")) {
        // The shared ADIF file: "file=PATH" chooses it, "now" reads it,
        // "insert=CALL" adds a test record, "remove" forgets it; then its state.
        if (arg.startsWith(QLatin1String("file="))) {
            m_engine.setSharedAdif(QUrl::fromLocalFile(arg.mid(5)));
        } else if (arg == QLatin1String("now")) {
            m_engine.sharedAdifRead();
        } else if (arg.startsWith(QLatin1String("insert="))) {
            m_engine.devSharedInsert(arg.mid(7));
        } else if (arg == QLatin1String("remove")) {
            m_engine.clearSharedAdif();
        }
        const QVariantMap f = m_engine.sharedAdif();
        qInfo().noquote() << "shot-plan: shared" << arg.section(QLatin1Char('='), 0, 0) << "| set" << f.value(QStringLiteral("set")).toBool()
                          << f.value(QStringLiteral("name")).toString() << "known" << f.value(QStringLiteral("known")).toInt()
                          << "pending" << f.value(QStringLiteral("pending")).toInt() << "reading" << f.value(QStringLiteral("reading")).toBool()
                          << f.value(QStringLiteral("statusKind")).toString() << f.value(QStringLiteral("status")).toString();
        *delayMs = 100;
    } else if (command == QLatin1String("keys")) {
        // Keys through the window as a hardware keyboard sends them; "\\b"
        // is Backspace as an iPad app on a Mac gets it from UIKit (key 8,
        // text "\\b"). Then the focused field's text.
        if (window) {
            QString keys = args.join(QLatin1Char(' '));  // words with spaces between
            // <F1> ... <F12>, <Esc>: that key alone, no text. <Cmd-1> ...
            // <Cmd-0>: the digit with Control, as Qt hands an iPad's ⌘ on
            // (Ctrl on Android and a PC).
            static const QRegularExpression named(QStringLiteral("^<(F([1-9]|1[0-2])|Esc|Cmd-([0-9]))>$"));
            if (const QRegularExpressionMatch m = named.match(keys); m.hasMatch()) {
                const bool cmd = !m.captured(3).isEmpty();
                const int key = cmd ? Qt::Key_0 + m.captured(3).toInt()
                                    : m.captured(1) == QLatin1String("Esc") ? Qt::Key_Escape : Qt::Key_F1 + m.captured(2).toInt() - 1;
                const Qt::KeyboardModifiers mods = cmd ? Qt::ControlModifier : Qt::NoModifier;
                QWindowSystemInterface::handleKeyEvent(window, QEvent::KeyPress, key, mods);
                QWindowSystemInterface::handleKeyEvent(window, QEvent::KeyRelease, key, mods);
                QWindowSystemInterface::flushWindowSystemEvents();
                QQuickItem* item = window->activeFocusItem();
                qInfo().noquote() << "shot-plan: key" << keys << "->" << (item ? QString::fromLatin1(item->metaObject()->className()) : QStringLiteral("none"));
                *delayMs = 500;
                return;
            }
            keys.replace(QLatin1String("\\b"), QString(QChar(0x08)));
            for (const QChar ch : std::as_const(keys)) {
                const int key = ch.unicode() == 0x08 ? 0x08 : ch.toUpper().unicode();
                QWindowSystemInterface::handleKeyEvent(window, QEvent::KeyPress, key, Qt::NoModifier, QString(ch));
                QWindowSystemInterface::handleKeyEvent(window, QEvent::KeyRelease, key, Qt::NoModifier, QString(ch));
            }
            QWindowSystemInterface::flushWindowSystemEvents();
            QQuickItem* item = window->activeFocusItem();
            qInfo().noquote() << "shot-plan: keys" << arg << "->"
                              << (item ? QString::fromLatin1(item->metaObject()->className()) : QStringLiteral("none"))
                              << (item ? item->property("text").toString() : QString());
        }
        *delayMs = 500;
    } else if (command == QLatin1String("spottest")) {
        // Test spots on the radio (MobileEngine::spotTest): add, clear, psk=MHZ,
        // rtty=MHZ.
        m_engine.spotTest(arg);
        *delayMs = 1500;
    } else if (command == QLatin1String("rttyset")) {
        // KEY=VALUE straight to MobileEngine::setRtty (a shift the tab does
        // not offer, e.g. a weather station's 450 Hz).
        const QString key = arg.section(QLatin1Char('='), 0, 0);
        const QString value = arg.section(QLatin1Char('='), 1);
        m_engine.setRtty(key, key == QLatin1String("reverse") || key == QLatin1String("squelch")
                                  ? QVariant(value == QLatin1String("true")) : QVariant(value.toDouble()));
        *delayMs = 300;
    } else if (command == QLatin1String("rttyfeed")) {
        m_engine.testRttyFeed(args.join(QLatin1Char(' ')));
        *delayMs = 1000;
    } else if (command == QLatin1String("qsodry")) {
        // QSOs printed to the log, not written to the logbook or sent (a test).
        m_engine.setQsoLogDryRun(arg != QLatin1String("off"));
        *delayMs = 100;
    } else if (command == QLatin1String("textclear")) {
        // The Digi tab's text (RTTY's and PSK's alike), as its Clear.
        m_engine.clearRttyText();
        *delayMs = 300;
    } else if (command == QLatin1String("textmode")) {
        // RTTY, PSK31 or PSK63, as the Digi tab's mode row.
        m_engine.setTextMode(arg);
        *delayMs = 1500;
    } else if (command == QLatin1String("pskfeed")) {
        // [+-HZ] TEXT: a PSK signal at the decoder's carrier (plus HZ off it).
        double offset = 0.0;
        QStringList words = args;
        if (!words.isEmpty() && (words.first().startsWith(QLatin1Char('+')) || words.first().startsWith(QLatin1Char('-')))) {
            offset = words.takeFirst().toDouble();
        }
        m_engine.testPskFeed(words.join(QLatin1Char(' ')), offset);
        *delayMs = 1000;
    } else if (command == QLatin1String("psk")) {
        // KEY=VALUE to MobileEngine::setPsk (carrier=1500, afc=false), or
        // bare: log the state and the text so far.
        if (arg.contains(QLatin1Char('='))) {
            const QString key = arg.section(QLatin1Char('='), 0, 0);
            const QString value = arg.section(QLatin1Char('='), 1);
            m_engine.setPsk(key, key == QLatin1String("afc") ? QVariant(value == QLatin1String("true")) : QVariant(value.toDouble()));
            *delayMs = 300;
        } else {
            const QVariantMap k = m_engine.psk();
            QString text = m_engine.rttyDecodedText().right(160);
            text.replace(QLatin1Char('\n'), QStringLiteral(" | ")).replace(QLatin1Char('\r'), QString());
            qInfo().noquote() << "shot-plan: psk active" << m_engine.pskActive() << "mode" << k.value(QStringLiteral("mode")).toString()
                              << "via" << k.value(QStringLiteral("via")).toString() << "carrier" << k.value(QStringLiteral("carrierHz")).toDouble()
                              << "tracked" << k.value(QStringLiteral("trackedHz")).toDouble()
                              << "at" << QString::number(k.value(QStringLiteral("carrierMhz")).toDouble(), 'f', 6)
                              << "quality" << k.value(QStringLiteral("quality")).toInt() << "locked" << k.value(QStringLiteral("locked")).toBool()
                              << "text:" << text;
        }
    } else if (command == QLatin1String("rtty")) {
        const QVariantMap r = m_engine.rtty();
        QString text = m_engine.rttyDecodedText().right(160);
        text.replace(QLatin1Char('\n'), QStringLiteral(" | ")).replace(QLatin1Char('\r'), QString());
        qInfo().noquote() << "shot-plan: rtty at" << QString::number(m_engine.frequencyMhz(), 'f', 5) << "mode" << m_engine.mode()
                          << "via" << r.value(QStringLiteral("via")).toString() << r.value(QStringLiteral("sideband")).toString()
                          << "decoding" << m_engine.rttyDecoderEnabled()
                          << "mark" << r.value(QStringLiteral("markHz")).toInt()
                          << "shift" << r.value(QStringLiteral("shiftHz")).toInt() << "snr" << r.value(QStringLiteral("snr")).toInt()
                          << "locked" << r.value(QStringLiteral("locked")).toBool() << "squelch" << r.value(QStringLiteral("squelch")).toBool()
                          << "text" << m_engine.rttyDecodedText().size()
                          << QStringLiteral("\"%1\"").arg(text);
        if (arg == QLatin1String("all")) {
            QString all = m_engine.rttyDecodedText();
            all.replace(QLatin1Char('\r'), QString());
            for (const QString& l : all.split(QLatin1Char('\n'))) {
                qInfo().noquote() << "shot-plan: rtty text |" << l;
            }
        }
        if (arg == QLatin1String("spots")) {
            // RTTY stations the spots know of, to tune onto one.
            for (const QVariant& v : m_engine.spots()) {
                const QVariantMap sp = v.toMap();
                const QString what = sp.value(QStringLiteral("mode")).toString() + QLatin1Char(' ') + sp.value(QStringLiteral("comment")).toString();
                if (what.contains(QLatin1String("RTTY"), Qt::CaseInsensitive)) {
                    qInfo().noquote() << "shot-plan: rtty spot" << sp.value(QStringLiteral("call")).toString()
                                      << QString::number(sp.value(QStringLiteral("freq")).toDouble(), 'f', 4) << what.left(60);
                }
            }
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
