#include "MobileWorkspace.h"

#include "core/AppSettings.h"
#include "gui/workspace/CanvasInteraction.h"

#include <QLoggingCategory>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

const QString MobileWorkspace::kSettingsKey = QStringLiteral("MobileWorkspace");

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
    AppSettings::instance().save();
}

void MobileWorkspace::reload()
{
    m_saveTimer.stop();
    m_dirty = false;
    m_undo.clear();
    m_doc = WorkspaceDocument();
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

} // namespace AetherSDR::mobile
