#pragma once

// The operator's own layouts of the tablet's operating screen (roadmap 113,
// Larry Averbeck W9FLA): the desktop's Workspace Canvas (RFC #4887) on a
// touch screen. The model is the desktop's own, widget-free and tested
// headless upstream (src/gui/workspace: WorkspaceDocument, NormRect,
// CanvasInteraction's applyDrag and snapRect), so the rules for moving,
// resizing and snapping and the stored form are the desktop's.
//
// A layout is a workspace; its "main" surface is the landscape arrangement
// and "portrait" the upright one. Item ids are workspace-wide in the
// desktop's document, so a block's id carries its orientation:
// "land:display", "port:display". The blocks themselves (what each id
// shows, its smallest size) are the page's (OperatePage.qml); here only
// their places.
//
// "Classic" is no workspace: canvas off, the page as it always was. Saved
// as it changes (a debounced write, as the desktop's WorkspaceStore), so
// there is no Save step; App setups carry the key with the rest.

#include "gui/workspace/WorkspaceDocument.h"

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariant>

namespace AetherSDR::mobile {

class MobileWorkspace : public QObject {
    Q_OBJECT
    // An own layout in use (false: Classic).
    Q_PROPERTY(bool canvas READ canvas WRITE setCanvas NOTIFY changed)
    // The own layout in use, or chosen for when canvas comes on.
    Q_PROPERTY(QString active READ active WRITE setActive NOTIFY changed)
    Q_PROPERTY(QString activeLabel READ activeLabel NOTIFY changed)
    // [{id, label}] in the operator's order.
    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY changed)
    // Bumped on every change, for bindings that read items().
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)

public:
    static const QString kSettingsKey;

    explicit MobileWorkspace(QObject* parent = nullptr);
    ~MobileWorkspace() override;

    bool canvas() const { return m_doc.canvasEnabled && m_doc.contains(m_doc.activeWorkspace); }
    void setCanvas(bool on);
    QString active() const { return m_doc.activeWorkspace; }
    void setActive(const QString& id);
    QString activeLabel() const;
    QVariantList layouts() const;
    int revision() const { return m_revision; }
    bool canUndo() const { return !m_undo.isEmpty(); }

    // The active layout's arrangement for "land" or "port":
    // [{block, x, y, w, h, z, closed}], block without the orientation.
    Q_INVOKABLE QVariantList items(const QString& orientation) const;
    // Whether the active layout has an arrangement for that orientation.
    Q_INVOKABLE bool hasArrangement(const QString& orientation) const;

    // A new layout seeded with `blocks` ([{block, x, y, w, h, closed}],
    // fractions of the canvas) for `orientation`; made active, canvas on.
    Q_INVOKABLE QString createLayout(const QString& orientation, const QVariantList& blocks);
    // The active layout's arrangement for `orientation` set to `blocks`
    // (the first edit in an orientation it lacked, or Reset to Classic).
    Q_INVOKABLE void seed(const QString& orientation, const QVariantList& blocks);
    Q_INVOKABLE void rename(const QString& id, const QString& label);
    Q_INVOKABLE QString duplicate(const QString& id);
    // Refuses nothing: the last own layout removed leaves Classic.
    Q_INVOKABLE void remove(const QString& id);

    // A gesture in progress: the rect it would give, not stored. zone is
    // "move", "n", "s", "e", "w", "ne", "nw", "se" or "sw"; start and the
    // deltas in fractions of the canvas; the minimum and the snapping
    // tolerance in fractions too. Returns {x, y, w, h, vguides, hguides}.
    Q_INVOKABLE QVariantMap drag(const QString& orientation, const QString& block, const QString& zone,
                                 const QVariantMap& start, double dx, double dy, double minW, double minH,
                                 double tolX, double tolY) const;
    // Before a change the operator can undo (a gesture's start, a hide).
    Q_INVOKABLE void checkpoint();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void setRect(const QString& orientation, const QString& block, const QVariantMap& rect);
    Q_INVOKABLE void setClosed(const QString& orientation, const QString& block, bool closed);
    // On top of the others (the block being moved).
    Q_INVOKABLE void raise(const QString& orientation, const QString& block);
    // The top row's Hide controls in an own layout (roadmap 114): `block`
    // hidden and the blocks beside it grown into its room, as Classic's
    // display takes the hidden panel's width. The side is the one whose
    // neighbours cover the block's edge most, among the window edges the
    // block lies on (a column at the right: the blocks left of it grow
    // right; a row at the bottom: the blocks above grow down). A neighbour
    // grows only when it lies within the block's span (gapX / gapY: the
    // most room between it and the block, in fractions), so nothing grows
    // over a third block. What grew is remembered (stored with the layouts)
    // for showAndRestore. Returns the blocks grown.
    Q_INVOKABLE QStringList hideAndFill(const QString& orientation, const QString& block, double gapX, double gapY);
    // Pin Controls / + Add: `block` shown again, and the blocks grown for it
    // back to their size when still as they were grown (one edited since
    // keeps its new size). The page then checks the block's place is free.
    Q_INVOKABLE void showAndRestore(const QString& orientation, const QString& block);
    // An older build's block split into several where its parts were
    // (roadmap 117: build 197's "slices", the slice row with its meters,
    // into meter1, slices and meter2): parts are {block, x, y, w, h}; the
    // block itself takes its part's rect, the new ones its closed state,
    // stacked above the others. Not an editing step (no Undo).
    Q_INVOKABLE void splitBlock(const QString& orientation, const QString& block, const QVariantList& parts);
    // Written now when changed (the app leaving the front, an App setup
    // being saved).
    Q_INVOKABLE void flush();
    // Read again from the settings (an App setup applied): what the
    // setup holds, not what was in memory.
    void reload();
    // View's Edit / New: the page starts editing (a new layout seeded from
    // Classic when newLayout, or when Classic is in use).
    Q_INVOKABLE void requestEdit(bool newLayout) { emit editRequested(newLayout); }

signals:
    void changed();
    void editRequested(bool newLayout);

private:
    static QString surfaceId(const QString& orientation);
    static QString itemId(const QString& orientation, const QString& block);
    Workspace* activeWorkspace();
    const Workspace* activeWorkspaceConst() const;
    WorkspaceSurface* surface(const QString& orientation, bool create);
    CanvasItem* item(const QString& orientation, const QString& block);
    void touch();
    void load();
    QString fillKey(const QString& orientation, const QString& block) const;

    WorkspaceDocument m_doc;
    QList<QByteArray> m_undo;
    QTimer m_saveTimer;
    int m_revision{0};
    // Changed since written: a flush with nothing changed writes nothing,
    // so it never puts an old layout over one an App setup brought.
    bool m_dirty{false};
    // What hideAndFill grew, per layout, orientation and hidden block: each
    // neighbour's rect before and after (kFillKey in the settings).
    QJsonObject m_fill;
};

} // namespace AetherSDR::mobile
