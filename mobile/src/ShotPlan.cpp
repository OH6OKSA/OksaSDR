#include "ShotPlan.h"

#include "MobileEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <algorithm>
#include <functional>

namespace AetherSDR::mobile {

namespace {

// Objects named so anywhere under the window: QObject children and the
// visual items (a popup's content is not a QObject child of its page).
QList<QObject*> findNamed(QObject* root, const QString& name)
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

// The tablet panel's sections, by the drawer's tab order.
const QStringList kSections{QStringLiteral("DSP"),      QStringLiteral("Audio"),  QStringLiteral("TX"),
                            QStringLiteral("CW"),       QStringLiteral("View"),   QStringLiteral("Profiles"),
                            QStringLiteral("Memory"),   QStringLiteral("Log"),    QStringLiteral("Digi"),
                            QStringLiteral("FreeDV")};

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
        for (QObject* o : root->findChildren<QObject*>()) {
            const QMetaObject* mo = o->metaObject();
            if (mo->indexOfProperty("opened") >= 0 && mo->indexOfMethod("close()") >= 0 && o->property("opened").toBool()) {
                QMetaObject::invokeMethod(o, "close");
            }
        }
        for (QObject* notice : findNamed(root, QStringLiteral("noSliceNotice"))) {
            notice->setProperty("preview", false);
        }
    }
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
                if (host && !host->isVisible()) {
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
        const QStringList xy = arg.split(QLatin1Char(','));
        if (window && xy.size() == 2) {
            const QPointF p(xy.at(0).toDouble() * window->width(), xy.at(1).toDouble() * window->height());
            const QPointF g = window->mapToGlobal(p);
            QMouseEvent press(QEvent::MouseButtonPress, p, g, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, p, g, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &release);
        }
    } else {
        qWarning() << "shot-plan: unknown command" << command;
        m_failed = true;
    }
}

} // namespace AetherSDR::mobile
