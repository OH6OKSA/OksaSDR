#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <algorithm>
#include <array>

#include "NoiseFloorTracker.h"
#include "gui/WaterfallLevelMap.h"


namespace AetherSDR::mobile {

class MobileEngine;

class WaterfallTexture;

// Panadapter trace (top) and scrolling waterfall (bottom) for the active pan.
// The waterfall auto-ranges each row because Flex rows are radio intensities
// while seam backends deliver dBm.
//
// Drawn as scene graph nodes, so a frame redoes only what changed (battery:
// the whole item repainted on the CPU each frame cost a phone or a tablet
// much of its power, Mike VA3MW). The waterfall is a ring of rows in one
// texture: a new row is written at the ring's next place and only that row
// goes to the GPU, the picture shown as the ring's two parts one under the
// other. The trace and its fill are vertex geometry, the grid rectangles;
// the frequency scale and band plan strip are an image painted only when
// the view or the plan changes. The software renderer (screenshots without
// a GPU) has no vertex geometry: there the trace is painted into an image.
class SpectrumItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(AetherSDR::mobile::MobileEngine* engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(double spectrumFraction READ spectrumFraction WRITE setSpectrumFraction NOTIFY spectrumFractionChanged)
    // The trace's current scale (dBm at the top and bottom of the spectrum
    // area), for the dBm scale strip and its drag.
    Q_PROPERTY(double traceTopDbm READ traceTopDbm NOTIFY traceScaleChanged)
    Q_PROPERTY(double traceBottomDbm READ traceBottomDbm NOTIFY traceScaleChanged)
    // The tracked noise floor whatever the trace's mode (without the RF
    // gain), smoothed and in half-dB steps as the desktop's 3D floor anchor.
    // The 3D view takes it.
    Q_PROPERTY(double noiseFloorDbm READ noiseFloorDbm NOTIFY traceScaleChanged)
    // Height of the band plan strip at the bottom of the spectrum (0 when no
    // plan is shown): the 3D view ends above it.
    Q_PROPERTY(double bandPlanHeight READ bandPlanHeight NOTIFY bandPlanHeightChanged)

public:
    explicit SpectrumItem(QQuickItem* parent = nullptr);

    MobileEngine* engine() const { return m_engine; }
    void setEngine(MobileEngine* engine);
    double spectrumFraction() const { return m_spectrumFraction; }
    double traceTopDbm() const { return m_traceTop; }
    double traceBottomDbm() const { return m_traceBottom; }
    double noiseFloorDbm() const { return m_autoNoise; }
    double bandPlanHeight() const;
    void setSpectrumFraction(double fraction);

signals:
    void engineChanged();
    void spectrumFractionChanged();
    void traceScaleChanged();
    void bandPlanHeightChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void updatePolish() override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void onSpectrum(const QVector<float>& binsDbm);
    void onWaterfallRow(const QVector<float>& bins, double lowMhz, double highMhz);
    // Moves and stretches the waterfall history so it stays on the same
    // frequencies after the pan is retuned or zoomed.
    void followView();
    void reportPixels();
    // Scrolls the waterfall by one row coloured from bins over
    // lowMhz..highMhz by the desktop's waterfall level law.
    void pushRow(const QVector<float>& bins, double lowMhz, double highMhz,
                 const WaterfallLevelMap::Params& levels);
    WaterfallLevelMap::Params waterfallParams(float autoBlack) const;
    void drawFrequencyAxis(QPainter* painter, qreal w, qreal specH) const;
    void drawBandPlan(QPainter* painter, qreal w, qreal specH) const;
    // The trace (grid, fill, line, centre line) painted, for the software
    // renderer.
    void drawTrace(QPainter* painter, qreal w, qreal specH) const;
    // The waterfall rows, newest first (the ring unrolled).
    QImage waterfallInOrder() const;
    // Something the trace or the scale strip shows changed: draw it anew.
    void traceChanged();
    void stripChanged();
    double specHeight() const { return height() * m_spectrumFraction; }
    double axisHeight() const { return std::clamp(height() * 0.03, 16.0, 22.0); }

    QPointer<MobileEngine> m_engine;
    QVector<float> m_spectrum;
    // The waterfall ring: m_wfHead is the newest row, older rows follow it
    // (wrapping to the top). Rows written since the last frame wait in
    // m_wfDirtyRows for the GPU; m_wfFullUpload sends the whole picture.
    QImage m_waterfall;
    int m_wfHead{0};
    QVector<int> m_wfDirtyRows;
    bool m_wfFullUpload{true};
    bool m_traceDirty{true};
    // The scale strip (frequency axis and band plan) as last painted, and
    // whether the view or the plan changed since.
    QImage m_strip;
    double m_stripTop{0.0};
    bool m_stripDirty{true};
    bool m_stripNew{false};
    // The waterfall as it was when a burst of resizes (a rotation) began.
    QImage m_waterfallBeforeResize;
    QElapsedTimer m_resizeBurst;
    std::array<QRgb, 256> m_palette{};
    NoiseFloorTracker m_noise;
    double m_frameCenterMhz{0.0};
    double m_frameBandwidthMhz{0.0};
    float m_anchor{-120.0f};
    bool m_anchorSeeded{false};
    qint64 m_lastTraceMs{0};
    float m_wfBlack{100.0f};
    bool m_wfBlackSeeded{false};
    bool m_wasTransmitting{false};
    qint64 m_txEndMs{0};
    double m_viewLowMhz{0.0};
    double m_viewHighMhz{0.0};
    float m_traceBottom{-130.0f};
    float m_traceTop{-40.0f};
    float m_autoNoise{-120.0f};
    float m_reportedNoise{0.0f};
    bool m_traceSeeded{false};
    float m_reportedTop{0.0f};
    float m_reportedBottom{0.0f};
    double m_spectrumFraction{0.4};
    // The radio sends no waterfall rows while transmitting; the trace's
    // frames fill in, paced like the RX waterfall (as SpectrumWidget does).
    qint64 m_nextTxRowMs{0};
    qint64 m_nextLevelLogMs{0};
};

} // namespace AetherSDR::mobile
