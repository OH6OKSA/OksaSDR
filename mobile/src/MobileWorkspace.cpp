#include "MobileWorkspace.h"

#include "core/AppSettings.h"
#include "gui/workspace/CanvasInteraction.h"

#include <QJsonDocument>
#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

const QString MobileWorkspace::kSettingsKey = QStringLiteral("MobileWorkspace");
namespace {
const QString kFillKey = QStringLiteral("MobileWorkspaceFill");
}

namespace {
// The desktop's debounce: a drag coalesces into one write.
constexpr int kSaveDebounceMs = 750;
// Enough for a session's worth of gestures; each is the whole document.
constexpr int kUndoDepth = 50;

HitZone zoneFrom(const QString& z)
{
    static const QHash<QString, HitZone> zones{
        {QStringLiteral("move"), HitZone::Move}, {QStringLiteral("n"), HitZone::N},
        {QStringLiteral("s"), HitZone::S},       {QStringLiteral("e"), HitZone::E},
        {QStringLiteral("w"), HitZone::W},       {QStringLiteral("ne"), HitZone::NE},
        {QStringLiteral("nw"), HitZone::NW},     {QStringLiteral("se"), HitZone::SE},
        {QStringLiteral("sw"), HitZone::SW},
    };
    return zones.value(z.toLower(), HitZone::None);
}

NormRect rectFrom(const QVariantMap& m)
{
    NormRect r;
    r.x = m.value(QStringLiteral("x")).toDouble();
    r.y = m.value(QStringLiteral("y")).toDouble();
    r.w = m.value(QStringLiteral("w")).toDouble();
    r.h = m.value(QStringLiteral("h")).toDouble();
    return r;
}

QVariantMap rectMap(const NormRect& r)
{
    return {{QStringLiteral("x"), r.x}, {QStringLiteral("y"), r.y}, {QStringLiteral("w"), r.w},
            {QStringLiteral("h"), r.h}};
}

QVariantList doubles(const QList<double>& v)
{
    QVariantList out;
    for (double d : v) {
        out << d;
    }
    return out;
}
} // namespace

MobileWorkspace::MobileWorkspace(QObject* parent)
    : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDebounceMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &MobileWorkspace::flush);
    load();
}

MobileWorkspace::~MobileWorkspace()
{
    if (m_saveTimer.isActive()) {
        flush();
    }
}

void MobileWorkspace::load()
{
    m_fill = QJsonDocument::fromJson(AppSettings::instance().value(kFillKey, QString()).toString().toUtf8()).object();
    const QString stored = AppSettings::instance().value(kSettingsKey, QString()).toString();
    if (stored.isEmpty()) {
        return;
    }
    WorkspaceDocument doc;
    QString error;
    QStringList warnings;
    if (!WorkspaceDocument::fromStoredJson(stored.toUtf8(), &doc, &error, &warnings)) {
        // Kept as it is (a newer build's, or damaged): Classic until the
        // operator makes a layout, which then writes over it.
        qCWarning(lcMobile).noquote() << "layouts: the stored layouts not read -" << error;
        return;
    }
    for (const QString& w : warnings) {
        qCInfo(lcMobile).noquote() << "layouts:" << w;
    }
    m_doc = doc;
    qCInfo(lcMobile).noquote() << "layouts:" << m_doc.workspaces.size() << "own,"
                               << (canvas() ? QStringLiteral("using ") + activeLabel() : QStringLiteral("Classic"));
}

void MobileWorkspace::flush()
{
    m_saveTimer.stop();
    if (!m_dirty) {
        return;
    }
    m_dirty = false;
    AppSettings::instance().setValue(kSettingsKey, QString::fromUtf8(m_doc.toStoredJson()));
    AppSettings::instance().setValue(kFillKey, QString::fromUtf8(QJsonDocument(m_fill).toJson(QJsonDocument::Compact)));
    AppSettings::instance().save();
}

void MobileWorkspace::reload()
{
    m_saveTimer.stop();
    m_dirty = false;
    m_undo.clear();
    m_doc = WorkspaceDocument();
    m_fill = QJsonObject();
    load();
    ++m_revision;
    emit changed();
}

void MobileWorkspace::touch()
{
    m_dirty = true;
    ++m_revision;
    m_saveTimer.start();
    emit changed();
}

QString MobileWorkspace::surfaceId(const QString& orientation)
{
    return orientation == QLatin1String("port") ? QStringLiteral("portrait") : WorkspaceSurface::kMainId;
}

QString MobileWorkspace::itemId(const QString& orientation, const QString& block)
{
    return (orientation == QLatin1String("port") ? QStringLiteral("port:") : QStringLiteral("land:")) + block;
}

Workspace* MobileWorkspace::activeWorkspace()
{
    for (Workspace& w : m_doc.workspaces) {
        if (w.id == m_doc.activeWorkspace) {
            return &w;
        }
    }
    return nullptr;
}

const Workspace* MobileWorkspace::activeWorkspaceConst() const
{
    return m_doc.workspace(m_doc.activeWorkspace);
}

WorkspaceSurface* MobileWorkspace::surface(const QString& orientation, bool create)
{
    Workspace* w = activeWorkspace();
    if (!w) {
        return nullptr;
    }
    const QString id = surfaceId(orientation);
    for (WorkspaceSurface& s : w->surfaces) {
        if (s.id == id) {
            return &s;
        }
    }
    if (!create) {
        return nullptr;
    }
    WorkspaceSurface s;
    s.id = id;
    s.label = orientation == QLatin1String("port") ? QStringLiteral("Portrait") : QStringLiteral("Landscape");
    w->surfaces.append(s);
    return &w->surfaces.last();
}

CanvasItem* MobileWorkspace::item(const QString& orientation, const QString& block)
{
    WorkspaceSurface* s = surface(orientation, false);
    if (!s) {
        return nullptr;
    }
    const QString id = itemId(orientation, block);
    for (CanvasItem& it : s->items) {
        if (it.id == id) {
            return &it;
        }
    }
    return nullptr;
}

void MobileWorkspace::setCanvas(bool on)
{
    if (on == m_doc.canvasEnabled) {
        return;
    }
    m_doc.canvasEnabled = on;
    touch();
}

void MobileWorkspace::setActive(const QString& id)
{
    if (!m_doc.contains(id) || id == m_doc.activeWorkspace) {
        return;
    }
    m_doc.activeWorkspace = id;
    m_undo.clear();
    touch();
}

QString MobileWorkspace::activeLabel() const
{
    const Workspace* w = activeWorkspaceConst();
    return w ? w->label : QString();
}

QVariantList MobileWorkspace::layouts() const
{
    QVariantList out;
    for (const Workspace& w : m_doc.workspaces) {
        out << QVariantMap{{QStringLiteral("id"), w.id}, {QStringLiteral("label"), w.label}};
    }
    return out;
}

QVariantList MobileWorkspace::items(const QString& orientation) const
{
    QVariantList out;
    const Workspace* w = activeWorkspaceConst();
    const WorkspaceSurface* s = w ? w->surface(surfaceId(orientation)) : nullptr;
    if (!s) {
        return out;
    }
    const int prefix = itemId(orientation, QString()).size();
    for (const CanvasItem& it : s->items) {
        QVariantMap m = rectMap(it.rect);
        m.insert(QStringLiteral("block"), it.id.mid(prefix));
        m.insert(QStringLiteral("z"), it.z);
        m.insert(QStringLiteral("closed"), it.closed);
        out << m;
    }
    return out;
}

bool MobileWorkspace::hasArrangement(const QString& orientation) const
{
    const Workspace* w = activeWorkspaceConst();
    const WorkspaceSurface* s = w ? w->surface(surfaceId(orientation)) : nullptr;
    return s && !s->items.isEmpty();
}

QString MobileWorkspace::createLayout(const QString& orientation, const QVariantList& blocks)
{
    // A new layout's Undo starts from it: undoing its making would leave
    // the editor open over Classic. Delete removes it.
    m_undo.clear();
    const QString id = m_doc.addBlank(m_doc.uniqueLabel(tr("Layout %1").arg(m_doc.workspaces.size() + 1)));
    m_doc.activeWorkspace = id;
    m_doc.canvasEnabled = true;
    seed(orientation, blocks);
    qCInfo(lcMobile).noquote() << "layouts: new" << activeLabel() << "from Classic," << orientation;
    return id;
}

void MobileWorkspace::seed(const QString& orientation, const QVariantList& blocks)
{
    WorkspaceSurface* s = surface(orientation, true);
    if (!s) {
        return;
    }
    s->items.clear();
    int z = 0;
    for (const QVariant& v : blocks) {
        const QVariantMap m = v.toMap();
        CanvasItem it;
        it.id = itemId(orientation, m.value(QStringLiteral("block")).toString());
        it.contentType = m.value(QStringLiteral("block")).toString();
        it.rect = clampToBounds(rectFrom(m));
        it.z = z++;
        it.closed = m.value(QStringLiteral("closed")).toBool();
        if (!it.rect.isValid()) {
            // A block Classic does not show (the controls as a drawer):
            // somewhere to start from when it is added.
            it.rect = NormRect{0.35, 0.35, 0.3, 0.3};
            it.closed = true;
        }
        s->items.append(it);
    }
    touch();
}

void MobileWorkspace::rename(const QString& id, const QString& label)
{
    const QString trimmed = label.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    checkpoint();
    if (m_doc.renameWorkspace(id, m_doc.uniqueLabel(trimmed))) {
        touch();
    }
}

QString MobileWorkspace::duplicate(const QString& id)
{
    const Workspace* w = m_doc.workspace(id);
    if (!w) {
        return {};
    }
    checkpoint();
    const QString copy = m_doc.addDuplicateOf(id, m_doc.uniqueLabel(w->label));
    if (!copy.isEmpty()) {
        m_doc.activeWorkspace = copy;
        touch();
    }
    return copy;
}

void MobileWorkspace::remove(const QString& id)
{
    if (!m_doc.contains(id)) {
        return;
    }
    checkpoint();
    for (const QString& k : m_fill.keys()) {
        if (k.startsWith(id + QLatin1Char('|'))) {
            m_fill.remove(k);
        }
    }
    if (m_doc.workspaces.size() == 1) {
        // The document's last workspace cannot go (it would describe no
        // shell): an empty document is Classic.
        m_doc = WorkspaceDocument();
    } else {
        m_doc.removeWorkspace(id);
    }
    touch();
}

QVariantMap MobileWorkspace::drag(const QString& orientation, const QString& block, const QString& zone,
                                  const QVariantMap& start, double dx, double dy, double minW, double minH,
                                  double tolX, double tolY) const
{
    const HitZone z = zoneFrom(zone);
    const QSizeF minNorm(minW, minH);
    const NormRect moved = applyDrag(rectFrom(start), z, dx, dy, minNorm);
    // Peers: the other blocks shown in this orientation.
    QList<NormRect> peers;
    const Workspace* w = activeWorkspaceConst();
    const WorkspaceSurface* s = w ? w->surface(surfaceId(orientation)) : nullptr;
    const QString self = itemId(orientation, block);
    if (s) {
        for (const CanvasItem& it : s->items) {
            if (it.id != self && !it.closed) {
                peers << it.rect;
            }
        }
    }
    const SnapResult snapped = snapRect(moved, z, peers, tolX, tolY, minNorm);
    QVariantMap out = rectMap(snapped.rect);
    out.insert(QStringLiteral("vguides"), doubles(snapped.verticalGuides));
    out.insert(QStringLiteral("hguides"), doubles(snapped.horizontalGuides));
    return out;
}

void MobileWorkspace::checkpoint()
{
    m_undo.append(m_doc.toStoredJson());
    while (m_undo.size() > kUndoDepth) {
        m_undo.removeFirst();
    }
    emit changed();
}

void MobileWorkspace::undo()
{
    if (m_undo.isEmpty()) {
        return;
    }
    WorkspaceDocument doc;
    if (WorkspaceDocument::fromStoredJson(m_undo.takeLast(), &doc)) {
        m_doc = doc;
    }
    touch();
}

void MobileWorkspace::setRect(const QString& orientation, const QString& block, const QVariantMap& rect)
{
    if (CanvasItem* it = item(orientation, block)) {
        const NormRect r = clampToBounds(rectFrom(rect));
        if (r.isValid() && r != it->rect) {
            it->rect = r;
            touch();
        }
    }
}

void MobileWorkspace::setClosed(const QString& orientation, const QString& block, bool closed)
{
    if (CanvasItem* it = item(orientation, block)) {
        if (it->closed != closed) {
            it->closed = closed;
            if (!closed) {
                raise(orientation, block);
            }
            touch();
        }
    }
}

void MobileWorkspace::raise(const QString& orientation, const QString& block)
{
    WorkspaceSurface* s = surface(orientation, false);
    CanvasItem* top = item(orientation, block);
    if (!s || !top) {
        return;
    }
    // Dense z over [0, n-1], the raised one last (as CanvasLayout keeps it).
    const int was = top->z;
    for (CanvasItem& it : s->items) {
        if (it.z > was) {
            --it.z;
        }
    }
    top->z = static_cast<int>(s->items.size()) - 1;
    touch();
}

QString MobileWorkspace::fillKey(const QString& orientation, const QString& block) const
{
    return m_doc.activeWorkspace + QLatin1Char('|') + surfaceId(orientation) + QLatin1Char('|') + block;
}

namespace {
QJsonObject rectJson(const NormRect& r)
{
    return {{QStringLiteral("x"), r.x}, {QStringLiteral("y"), r.y}, {QStringLiteral("w"), r.w}, {QStringLiteral("h"), r.h}};
}
NormRect rectOf(const QJsonObject& o)
{
    NormRect r;
    r.x = o.value(QStringLiteral("x")).toDouble();
    r.y = o.value(QStringLiteral("y")).toDouble();
    r.w = o.value(QStringLiteral("w")).toDouble();
    r.h = o.value(QStringLiteral("h")).toDouble();
    return r;
}
bool sameRect(const NormRect& a, const NormRect& b)
{
    return std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6 && std::abs(a.w - b.w) < 1e-6
        && std::abs(a.h - b.h) < 1e-6;
}
} // namespace

QStringList MobileWorkspace::hideAndFill(const QString& orientation, const QString& block, double gapX, double gapY)
{
    WorkspaceSurface* s = surface(orientation, false);
    CanvasItem* hidden = item(orientation, block);
    if (!s || !hidden || hidden->closed) {
        return {};
    }
    const NormRect c = hidden->rect;
    const QString displayId = itemId(orientation, QStringLiteral("display"));
    // The four ways the room can be taken: by the blocks on one side of
    // the hidden one, growing toward it (Right: the blocks left of it grow
    // right; Down: the blocks above grow down...).
    enum Dir { Right, Left, Down, Up };
    const auto horizontal = [](Dir d) { return d == Right || d == Left; };
    // Beside the hidden block on the side that grows in `d`, within the gap.
    const auto beside = [&](Dir d, const NormRect& b) {
        switch (d) {
        case Right: return b.right() <= c.x + 1e-9 && c.x - b.right() <= gapX;
        case Left: return b.x >= c.right() - 1e-9 && b.x - c.right() <= gapX;
        case Down: return b.bottom() <= c.y + 1e-9 && c.y - b.bottom() <= gapY;
        case Up: return b.y >= c.bottom() - 1e-9 && b.y - c.bottom() <= gapY;
        }
        return false;
    };
    // Within the hidden block's span across `d`: growing it stays in the
    // hidden block's room, so it covers nothing else.
    const auto within = [&](Dir d, const NormRect& b) {
        return horizontal(d) ? (b.y >= c.y - gapY && b.bottom() <= c.bottom() + gapY)
                             : (b.x >= c.x - gapX && b.right() <= c.right() + gapX);
    };
    // Further from the hidden block than `a` on that side, in a's lane.
    const auto behind = [&](Dir d, const NormRect& a, const NormRect& b) {
        const bool lane = horizontal(d) ? (b.y < a.bottom() && b.bottom() > a.y) : (b.x < a.right() && b.right() > a.x);
        switch (d) {
        case Right: return lane && b.right() <= a.x + 1e-9;
        case Left: return lane && b.x >= a.right() - 1e-9;
        case Down: return lane && b.bottom() <= a.y + 1e-9;
        case Up: return lane && b.y >= a.bottom() - 1e-9;
        }
        return false;
    };
    struct Plan {
        Dir dir = Right;
        QList<CanvasItem*> adjacent;
        double cover = 0.0;
        bool display = false; // the display is in one of its lanes
    };
    QList<Plan> plans;
    for (const Dir d : {Right, Left, Down, Up}) {
        Plan p;
        p.dir = d;
        for (CanvasItem& it : s->items) {
            if (&it != hidden && !it.closed && beside(d, it.rect) && within(d, it.rect)) {
                p.adjacent << &it;
                p.cover += horizontal(d) ? it.rect.h : it.rect.w;
            }
        }
        if (p.adjacent.isEmpty()) {
            continue;
        }
        for (CanvasItem* a : std::as_const(p.adjacent)) {
            if (a->id == displayId) {
                p.display = true;
            }
            for (const CanvasItem& b : std::as_const(s->items)) {
                if (b.id == displayId && !b.closed && behind(d, a->rect, b.rect)) {
                    p.display = true;
                }
            }
        }
        plans << p;
    }
    // Classic's way first: the side where the display takes the room; else
    // the side whose blocks cover the hidden block's edge most.
    const Plan* best = nullptr;
    for (const Plan& p : std::as_const(plans)) {
        if (!best || (p.display && !best->display) || (p.display == best->display && p.cover > best->cover + 1e-9)) {
            best = &p;
        }
    }
    QHash<CanvasItem*, NormRect> after;
    if (best) {
        const Dir d = best->dir;
        // How far a block next to the hidden one moves or grows to its far edge.
        const auto reach = [&](const NormRect& a) {
            switch (d) {
            case Right: return c.right() - a.right();
            case Left: return a.x - c.x;
            case Down: return c.bottom() - a.bottom();
            case Up: return a.y - c.y;
            }
            return 0.0;
        };
        const auto grow = [&](NormRect r, double by) {
            switch (d) {
            case Right: r.w += by; break;
            case Left: r.x -= by; r.w += by; break;
            case Down: r.h += by; break;
            case Up: r.y -= by; r.h += by; break;
            }
            return r;
        };
        const auto shift = [&](NormRect r, double by) {
            switch (d) {
            case Right: r.x += by; break;
            case Left: r.x -= by; break;
            case Down: r.y += by; break;
            case Up: r.y -= by; break;
            }
            return r;
        };
        // The block in a lane that takes the room, as Classic's layout
        // stretches it: the display, else the slice chips (the slice row's
        // meters stay at its ends), else the block next to the hidden one.
        const QStringList stretch{displayId, itemId(orientation, QStringLiteral("slices"))};
        for (CanvasItem* a : best->adjacent) {
            const double by = reach(a->rect);
            // The stretching block behind it in its lane takes the room, and
            // the blocks between move along; only when each of those lies
            // within the hidden block's span.
            CanvasItem* display = nullptr;
            QList<CanvasItem*> between;
            for (CanvasItem& b : s->items) {
                if (&b == a || &b == hidden || b.closed || !behind(d, a->rect, b.rect) || !stretch.contains(b.id)) {
                    continue;
                }
                if (!display || stretch.indexOf(b.id) < stretch.indexOf(display->id)) {
                    display = &b;
                }
            }
            if (stretch.contains(a->id) && (!display || stretch.indexOf(a->id) <= stretch.indexOf(display->id))) {
                display = nullptr; // a stretches itself
            }
            if (display) {
                bool fits = true;
                between << a;
                for (CanvasItem& b : s->items) {
                    if (&b == a || &b == display || &b == hidden || b.closed) {
                        continue;
                    }
                    if (behind(d, a->rect, b.rect) && !behind(d, display->rect, b.rect)) {
                        between << &b;
                        fits = fits && within(d, b.rect);
                    }
                }
                if (fits) {
                    for (CanvasItem* m : std::as_const(between)) {
                        if (!after.contains(m)) {
                            after.insert(m, shift(m->rect, by));
                        }
                    }
                    if (!after.contains(display)) {
                        after.insert(display, grow(display->rect, by));
                    }
                    continue;
                }
            }
            if (!after.contains(a)) {
                after.insert(a, grow(a->rect, by));
            }
        }
    }
    QJsonObject record;
    QStringList changed;
    const int prefix = itemId(orientation, QString()).size();
    for (auto it = after.constBegin(); it != after.constEnd(); ++it) {
        const NormRect before = it.key()->rect;
        const NormRect now = clampToBounds(it.value());
        it.key()->rect = now;
        record.insert(it.key()->id.mid(prefix),
                      QJsonObject{{QStringLiteral("before"), rectJson(before)}, {QStringLiteral("after"), rectJson(now)}});
        changed << it.key()->id.mid(prefix);
    }
    changed.sort();
    hidden->closed = true;
    if (record.isEmpty()) {
        m_fill.remove(fillKey(orientation, block));
    } else {
        m_fill.insert(fillKey(orientation, block), record);
    }
    qCInfo(lcMobile).noquote() << "layouts:" << block << "hidden,"
                               << (changed.isEmpty() ? QStringLiteral("nothing grown")
                                                     : QStringLiteral("grown or moved: ") + changed.join(QLatin1Char(' ')));
    touch();
    return changed;
}

void MobileWorkspace::splitBlock(const QString& orientation, const QString& block, const QVariantList& parts)
{
    WorkspaceSurface* s = surface(orientation, false);
    CanvasItem* old = item(orientation, block);
    if (!s || !old) {
        return;
    }
    const bool closed = old->closed;
    QStringList made;
    for (const QVariant& v : parts) {
        const QVariantMap m = v.toMap();
        const QString name = m.value(QStringLiteral("block")).toString();
        const NormRect r = clampToBounds(rectFrom(m));
        if (name.isEmpty() || !r.isValid()) {
            continue;
        }
        if (name == block) {
            item(orientation, block)->rect = r;
        } else if (!item(orientation, name)) {
            CanvasItem it;
            it.id = itemId(orientation, name);
            it.contentType = name;
            it.rect = r;
            it.closed = closed;
            it.z = static_cast<int>(s->items.size());
            s->items.append(it);
        } else {
            continue;
        }
        made << name;
    }
    qCInfo(lcMobile).noquote() << "layouts:" << block << "split into" << made.join(QLatin1Char(' ')) << "(" << surfaceId(orientation) << ")";
    touch();
}

void MobileWorkspace::showAndRestore(const QString& orientation, const QString& block)
{
    CanvasItem* shown = item(orientation, block);
    if (!shown) {
        return;
    }
    const QString key = fillKey(orientation, block);
    const QJsonObject record = m_fill.value(key).toObject();
    QStringList restored;
    for (auto r = record.constBegin(); r != record.constEnd(); ++r) {
        CanvasItem* it = item(orientation, r.key());
        const QJsonObject o = r.value().toObject();
        // Only one still as it was grown: one edited since keeps its size.
        if (it && sameRect(it->rect, rectOf(o.value(QStringLiteral("after")).toObject()))) {
            it->rect = rectOf(o.value(QStringLiteral("before")).toObject());
            restored << r.key();
        }
    }
    m_fill.remove(key);
    shown->closed = false;
    raise(orientation, block);
    qCInfo(lcMobile).noquote() << "layouts:" << block << "shown," << (restored.isEmpty() ? QStringLiteral("nothing restored")
                                                                                        : QStringLiteral("restored: ") + restored.join(QLatin1Char(' ')));
    touch();
}

} // namespace AetherSDR::mobile
