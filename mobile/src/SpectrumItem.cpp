#include "SpectrumItem.h"

#include <QFontMetricsF>

#include "MobileEngine.h"
#include "SpectrumPalette.h"
#include "gui/NoiseFloorEstimator.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGRectangleNode>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <QSGVertexColorMaterial>
#include <QVarLengthArray>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>

namespace AetherSDR::mobile {

Q_LOGGING_CATEGORY(lcSpectrum, "aether.mobile.spectrum")

namespace {

// How often the spectrum's levels go to the log.
constexpr qint64 kLevelLogIntervalMs = 30000;

// Auto trace scale: the noise floor this far up from the bottom (the
// desktop's default noise floor position, 75 % down from the top).
constexpr float kNoiseTargetFraction = 0.75f;
// Trace averaging, the desktop's SMOOTH_ALPHA (share of the new frame).
constexpr float kTraceAverage = 0.35f;
// Row interval for the TX waterfall drawn from trace frames.
constexpr qint64 kTxRowIntervalMs = 100;
// Waterfall rows dropped after a transmission while the AGC settles.
constexpr qint64 kPostTxSettleMs = 400;

const QColor kBackground(0x0b, 0x0f, 0x14);
const QColor kTraceColor(63, 182, 255);
constexpr qreal kTraceWidth = 1.5;
constexpr int kGridLines = 9;

// The trace fill's gradient, the desktop's: the trace colour at the top,
// darker and fainter toward the bottom (fill alpha 0.70).
QColor fillTop() { return QColor(63, 182, 255, 140); }
QColor fillBottom()
{
    QColor bottom = kTraceColor.darker(300);
    bottom.setAlpha(42);
    return bottom;
}

} // namespace

// The waterfall ring on the GPU. Rows written since the last frame are sent
// on their own (a few hundred bytes each); the whole picture only after a
// resize, a retune or zoom (the history moved) or a new texture. What the
// item hands over (in its sync, the GUI thread waiting) is copied here, so
// the render thread never reads the item's picture.
class WaterfallTexture : public QSGTexture {
public:
    ~WaterfallTexture() override { delete m_texture; }

    qint64 comparisonKey() const override { return qint64(reinterpret_cast<quintptr>(this)); }
    QRhiTexture* rhiTexture() const override { return m_texture; }
    QSize textureSize() const override { return m_size; }
    bool hasAlphaChannel() const override { return false; }
    bool hasMipmaps() const override { return false; }

    void setImage(const QImage& image)
    {
        m_full = image;
        m_size = image.size();
        m_rows.clear();
    }
    void addRow(const QImage& image, int y)
    {
        m_rows.append({y, image.copy(0, y, image.width(), 1)});
    }
    qsizetype pendingRows() const { return m_rows.size(); }

    void commitTextureOperations(QRhi* rhi, QRhiResourceUpdateBatch* batch) override
    {
        if (m_size.isEmpty()) {
            return;
        }
        if (!m_texture || m_texture->pixelSize() != m_size) {
            delete m_texture;
            // QImage RGB32 is BGRA in memory: sent as it is where the GPU
            // takes BGRA, else converted.
            m_bgra = rhi->isTextureFormatSupported(QRhiTexture::BGRA8);
            m_texture = rhi->newTexture(m_bgra ? QRhiTexture::BGRA8 : QRhiTexture::RGBA8, m_size);
            if (!m_texture->create()) {
                qCWarning(lcSpectrum) << "waterfall texture" << m_size << "could not be created";
                delete m_texture;
                m_texture = nullptr;
                return;
            }
        }
        if (!m_full.isNull()) {
            batch->uploadTexture(m_texture, QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(forGpu(m_full))));
            m_full = QImage();
            m_rows.clear();
        } else if (!m_rows.isEmpty()) {
            QVarLengthArray<QRhiTextureUploadEntry, 16> entries;
            for (const Row& row : std::as_const(m_rows)) {
                QRhiTextureSubresourceUploadDescription part(forGpu(row.image));
                part.setDestinationTopLeft(QPoint(0, row.y));
                entries.append(QRhiTextureUploadEntry(0, 0, part));
            }
            QRhiTextureUploadDescription upload;
            upload.setEntries(entries.cbegin(), entries.cend());
            batch->uploadTexture(m_texture, upload);
            m_rows.clear();
        }
    }

private:
    QImage forGpu(const QImage& image) const
    {
        return m_bgra ? image : image.convertToFormat(QImage::Format_RGBA8888);
    }

    struct Row {
        int y;
        QImage image;
    };
    QRhiTexture* m_texture{nullptr};
    QSize m_size;
    bool m_bgra{true};
    QImage m_full;
    QVector<Row> m_rows;
};

namespace {

// The item's nodes, back to front: background, the waterfall ring's two
// parts, the scale strip, the trace (taken out in 3D, where Spectrum3DItem
// draws the spectrum area). The textures are kept here, not by the nodes.
class SpectrumNode : public QSGNode {
public:
    ~SpectrumNode() override
    {
        if (trace && !trace->parent()) {
            delete trace;
        }
        delete wfTexture;
        delete stripTexture;
        delete traceTexture;
        delete placeholder;
    }

    // Image nodes never go without a texture (the renderer compares them):
    // a transparent pixel until there is a picture.
    QSGTexture* placeholder{nullptr};

    QSGRectangleNode* background{nullptr};
    QSGImageNode* wfTop{nullptr};
    QSGImageNode* wfBottom{nullptr};
    QSGTexture* wfTexture{nullptr};
    QSGImageNode* strip{nullptr};
    QSGTexture* stripTexture{nullptr};
    QSGNode* trace{nullptr};
    // GPU: grid rectangles, fill, line, centre line.
    std::array<QSGRectangleNode*, kGridLines - 1> grid{};
    QSGGeometryNode* fill{nullptr};
    QSGGeometryNode* line{nullptr};
    QSGRectangleNode* centre{nullptr};
    // Software renderer: the trace as one painted image.
    QSGImageNode* traceImage{nullptr};
    QSGTexture* traceTexture{nullptr};
};

} // namespace

SpectrumItem::SpectrumItem(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    for (int i = 0; i < 256; ++i) {
        m_palette[i] = spectrumPaletteAt(i / 255.0f);
    }
}

void SpectrumItem::setEngine(MobileEngine* engine)
{
    if (engine == m_engine) {
        return;
    }
    if (m_engine) {
        disconnect(m_engine, nullptr, this, nullptr);
    }
    m_engine = engine;
    if (m_engine) {
        connect(m_engine, &MobileEngine::spectrumFrame, this, &SpectrumItem::onSpectrum);
        connect(m_engine, &MobileEngine::waterfallRow, this, &SpectrumItem::onWaterfallRow);
        connect(m_engine, &MobileEngine::panGeometryChanged, this, [this] {
            followView();
            stripChanged();
        });
        connect(m_engine, &MobileEngine::displayLevelsChanged, this, [this] {
            stripChanged();
            traceChanged();
            update();
        });
        connect(m_engine, &MobileEngine::bandPlanChanged, this, [this] {
            emit bandPlanHeightChanged();
            stripChanged();
        });
        reportPixels();
    }
    emit engineChanged();
}

void SpectrumItem::setSpectrumFraction(double fraction)
{
    fraction = std::clamp(fraction, 0.1, 0.9);
    if (qFuzzyCompare(fraction, m_spectrumFraction)) {
        return;
    }
    m_spectrumFraction = fraction;
    reportPixels();
    stripChanged();
    traceChanged();
    update();
    emit spectrumFractionChanged();
}

void SpectrumItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    reportPixels();
    stripChanged();
    traceChanged();
    update();
    if (newGeometry.height() != oldGeometry.height()) {
        emit bandPlanHeightChanged();
    }
}

void SpectrumItem::reportPixels()
{
    const int w = static_cast<int>(width());
    const int specH = static_cast<int>(height() * m_spectrumFraction);
    const int wfH = static_cast<int>(height()) - specH;
    if (w <= 0 || specH <= 0 || wfH <= 0) {
        return;
    }
    // The waterfall keeps its rows at one pixel each and never loses height:
    // a lower window shows its newest rows, a taller one the older rows
    // again. A phone turning (WA2N: the display went dark for seconds)
    // keeps the history; only the width is scaled, from the picture as it
    // was when the burst of sizes of one turn began.
    if (m_waterfall.width() != w || m_waterfall.height() < wfH) {
        if (!m_resizeBurst.isValid() || m_resizeBurst.elapsed() > 1000) {
            m_waterfallBeforeResize = waterfallInOrder();
        }
        m_resizeBurst.start();
        const QImage& from = m_waterfallBeforeResize;
        const int rows = std::max(wfH, from.isNull() ? 0 : from.height());
        QImage resized(w, rows, QImage::Format_RGB32);
        resized.fill(Qt::black);
        if (from.isNull()) {
            m_viewLowMhz = 0.0;
            m_viewHighMhz = 0.0;
        } else {
            QPainter painter(&resized);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QRectF(0, 0, w, from.height()), from);
        }
        m_waterfall = resized;
        m_wfHead = 0;
        m_wfDirtyRows.clear();
        m_wfFullUpload = true;
        update();
    }
    if (m_engine) {
        m_engine->setPanPixels(w, specH);
    }
}

void SpectrumItem::onSpectrum(const QVector<float>& binsDbm)
{
    // Non-finite bins (empty frames) would make the scale infinite.
    QVector<float> raw = binsDbm;
    for (float& v : raw) {
        if (!std::isfinite(v)) {
            v = -200.0f;
        }
        v = std::clamp(v, -200.0f, 50.0f);
    }
    if (raw.isEmpty()) {
        traceChanged();
        return;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool transmitting = m_engine && m_engine->radioTransmitting();
    // A retune or zoom moves every bin to a new frequency: start the
    // averaging and the noise floor afresh, as the desktop does.
    const double center = m_engine ? m_engine->panCenterMhz() : 0.0;
    const double bandwidth = m_engine ? m_engine->panBandwidthMhz() : 0.0;
    const bool frameChanged = !qFuzzyCompare(1.0 + center, 1.0 + m_frameCenterMhz)
        || !qFuzzyCompare(1.0 + bandwidth, 1.0 + m_frameBandwidthMhz);
    if (frameChanged) {
        m_frameCenterMhz = center;
        m_frameBandwidthMhz = bandwidth;
        m_noise.reset();
    }
    // Trace averaging (the desktop's SMOOTH_ALPHA): steadies the trace,
    // restarted on a new frame, a new width and after a transmission.
    if (m_spectrum.size() != raw.size() || frameChanged || (m_wasTransmitting && !transmitting)) {
        m_spectrum = raw;
    } else {
        for (qsizetype i = 0; i < raw.size(); ++i) {
            m_spectrum[i] = kTraceAverage * raw.at(i) + (1.0f - kTraceAverage) * m_spectrum.at(i);
        }
    }
    if (m_wasTransmitting && !transmitting) {
        m_txEndMs = nowMs;
    }
    m_wasTransmitting = transmitting;

    // Noise floor as the desktop tracks it. Without the RF gain, so a
    // preamp or attenuator step shows on the trace, waterfall and 3D (as
    // on a fixed SmartSDR scale) instead of being levelled away.
    const float gain = m_engine ? static_cast<float>(m_engine->rfGain()) : 0.0f;
    if (!transmitting) {
        m_noise.observe(estimateNoiseFloorDbm(std::span<const float>(raw.constData(), raw.size())) - gain, nowMs);
    }
    const float noise = m_noise.valid() ? m_noise.baseline() : m_autoNoise;
    // The 3D floor's anchor (SpectrumWidget::dssFloorDbm): follows the
    // baseline slowly, jumps when it is far off, in half-dB steps.
    if (!m_anchorSeeded || std::abs(noise - m_anchor) > 18.0f) {
        m_anchor = noise;
        m_anchorSeeded = true;
    } else {
        m_anchor += (noise - m_anchor) * 0.04f;
    }
    m_autoNoise = std::round(m_anchor * 2.0f) / 2.0f;

    // Trace scale. Auto as the desktop: the span stays (Range), the noise
    // floor sits a quarter of it above the bottom, and the level eases
    // there (a changing span made signal heights jump). Manual: reference
    // level at the top, range below it.
    const float range = m_engine ? static_cast<float>(m_engine->traceRangeDb()) : 90.0f;
    if (m_engine && !m_engine->traceAutoRange()) {
        m_traceTop = static_cast<float>(m_engine->traceRefDbm());
        m_traceSeeded = true;
    } else {
        const float target = noise + kNoiseTargetFraction * range;
        const float elapsedSec = m_lastTraceMs > 0 ? (nowMs - m_lastTraceMs) / 1000.0f : 1.0f;
        m_traceTop = m_traceSeeded ? easeLevelToward(m_traceTop, target, elapsedSec) : target;
        m_traceSeeded = true;
    }
    m_traceBottom = m_traceTop - range;
    m_lastTraceMs = nowMs;

    // Levels in the log now and then: bins pinned at the lowest value
    // mean the noise sits below the radio's scale and draws flat.
    if (m_engine && nowMs >= m_nextLevelLogMs) {
        m_nextLevelLogMs = nowMs + kLevelLogIntervalMs;
        QVector<float> sorted = raw;
        std::sort(sorted.begin(), sorted.end());
        const auto atLowest = std::upper_bound(sorted.cbegin(), sorted.cend(), sorted.first()) - sorted.cbegin();
        qCInfo(lcSpectrum).noquote()
            << QStringLiteral("spectrum levels: %1 bins on %2x%3, %4, lowest %5 dBm (%6 % of bins), "
                              "noise %7 dBm, peak %8 dBm, RF gain %9 dB, trace %10..%11 dBm")
                   .arg(sorted.size())
                   .arg(static_cast<int>(width()))
                   .arg(static_cast<int>(height() * m_spectrumFraction))
                   .arg(m_engine->panScaleInfo())
                   .arg(sorted.first(), 0, 'f', 1)
                   .arg(qRound(100.0 * atLowest / sorted.size()))
                   .arg(noise + gain, 0, 'f', 1)
                   .arg(sorted.last(), 0, 'f', 1)
                   .arg(m_engine->rfGain())
                   .arg(m_traceBottom, 0, 'f', 0)
                   .arg(m_traceTop, 0, 'f', 0);
    }

    // Tell the scale strip and the 3D view when a level moved.
    if (std::abs(m_traceTop - m_reportedTop) >= 0.5f || std::abs(m_traceBottom - m_reportedBottom) >= 0.5f
        || std::abs(m_autoNoise - m_reportedNoise) >= 0.5f) {
        m_reportedTop = m_traceTop;
        m_reportedBottom = m_traceBottom;
        m_reportedNoise = m_autoNoise;
        emit traceScaleChanged();
    }
    // Own transmission in the waterfall, when the radio's Show TX in
    // waterfall is on: the radio pauses waterfall rows during TX, so draw
    // them from the trace on the RX colour scale. Off, the waterfall simply
    // waits for the transmission to end, as SmartSDR's (the user: the TX
    // rows lit the whole span on 20 m).
    if (transmitting && !m_waterfall.isNull() && nowMs >= m_nextTxRowMs && m_engine->showTxInWaterfall()) {
        m_nextTxRowMs = nowMs + kTxRowIntervalMs;
        followView();
        pushRow(m_spectrum, m_viewLowMhz, m_viewHighMhz, waterfallParams(noise));
        return;
    }
    traceChanged();
}

double SpectrumItem::cwPeakNear(double mhz) const
{
    const qsizetype n = m_spectrum.size();
    if (n < 16 || m_frameBandwidthMhz <= 0.0 || width() <= 0.0) {
        return -1.0;
    }
    const double lowMhz = m_frameCenterMhz - m_frameBandwidthMhz / 2.0;
    const double binMhz = m_frameBandwidthMhz / static_cast<double>(n);
    const double centre = (mhz - lowMhz) / binMhz;
    if (centre < 0.0 || centre >= static_cast<double>(n)) {
        return -1.0;
    }
    const double halfMhz = std::max(14.0 * m_frameBandwidthMhz / width(), 60e-6);
    const qsizetype half = std::max<qsizetype>(2, static_cast<qsizetype>(std::ceil(halfMhz / binMhz)));
    const qsizetype c = static_cast<qsizetype>(centre);
    const qsizetype lo = std::max<qsizetype>(1, c - half);
    const qsizetype hi = std::min<qsizetype>(n - 2, c + half);
    if (lo > hi) {
        return -1.0;
    }
    qsizetype best = lo;
    for (qsizetype i = lo + 1; i <= hi; ++i) {
        if (m_spectrum.at(i) > m_spectrum.at(best)) {
            best = i;
        }
    }
    // The bins around it (three times as wide) for its floor: their median.
    std::vector<float> around;
    for (qsizetype i = std::max<qsizetype>(0, c - 3 * half); i <= std::min<qsizetype>(n - 1, c + 3 * half); ++i) {
        around.push_back(m_spectrum.at(i));
    }
    std::nth_element(around.begin(), around.begin() + around.size() / 2, around.end());
    const float floor = around[around.size() / 2];
    const float peak = m_spectrum.at(best);
    const double bestMhz = lowMhz + (static_cast<double>(best) + 0.5) * binMhz;
    qCInfo(lcSpectrum).noquote() << QStringLiteral("cw peak near %1 MHz (±%2 Hz): %3 dBm at %4 MHz, floor %5 dBm: %6")
                                        .arg(mhz, 0, 'f', 6)
                                        .arg(std::lround(halfMhz * 1e6))
                                        .arg(peak, 0, 'f', 1)
                                        .arg(bestMhz, 0, 'f', 6)
                                        .arg(floor, 0, 'f', 1)
                                        .arg(peak >= floor + 8.0f ? QStringLiteral("a signal") : QStringLiteral("none"));
    if (peak < floor + 8.0f) {
        return -1.0;
    }
    // Between the bins: the vertex of the parabola through the peak and its
    // neighbours.
    const float l = m_spectrum.at(best - 1);
    const float r = m_spectrum.at(best + 1);
    const float denom = l - 2.0f * peak + r;
    const double shift = denom < 0.0f ? std::clamp(0.5 * (l - r) / denom, -0.5, 0.5) : 0.0;
    return lowMhz + (static_cast<double>(best) + 0.5 + shift) * binMhz;
}

WaterfallLevelMap::Params SpectrumItem::waterfallParams(float autoBlack) const
{
    // The desktop's waterfall law: black at the tracked noise floor (Black
    // level biases it, 50 = none) or, with Auto off, a fixed black point;
    // the colour range's width set by Colour gain alone, so the same signal
    // looks the same on any band and a strong carrier does not darken the
    // rest.
    WaterfallLevelMap::Params p;
    p.autoBlack = !m_engine || m_engine->wfAutoLevel();
    p.autoBlackThresh = autoBlack;
    p.autoBlackOffset = m_engine ? m_engine->wfBlackLevel() : 50;
    p.blackLevel = m_engine ? m_engine->wfBlackLevel() : 50;
    p.colorGain = m_engine ? m_engine->wfColorGain() : 50;
    return p;
}

void SpectrumItem::followView()
{
    if (!m_engine || m_waterfall.isNull() || m_engine->panBandwidthMhz() <= 0.0) {
        return;
    }
    const double low = m_engine->panCenterMhz() - m_engine->panBandwidthMhz() / 2.0;
    const double high = m_engine->panCenterMhz() + m_engine->panBandwidthMhz() / 2.0;
    const bool hadView = m_viewHighMhz > m_viewLowMhz;
    if (hadView && qFuzzyCompare(low, m_viewLowMhz) && qFuzzyCompare(high, m_viewHighMhz)) {
        return;
    }
    if (hadView) {
        // Old pixel x showed m_viewLow + x/w * oldSpan; place it at the same
        // frequency in the new view. Uncovered parts turn black. Every row
        // moves alike, so the ring's order stays.
        const double w = m_waterfall.width();
        const double newSpan = high - low;
        const QRectF target((m_viewLowMhz - low) / newSpan * w, 0.0,
                            (m_viewHighMhz - m_viewLowMhz) / newSpan * w, m_waterfall.height());
        QImage moved(m_waterfall.size(), QImage::Format_RGB32);
        moved.fill(Qt::black);
        QPainter painter(&moved);
        painter.drawImage(target, m_waterfall);
        painter.end();
        m_waterfall = moved;
        m_wfDirtyRows.clear();
        m_wfFullUpload = true;
        update();
    }
    m_viewLowMhz = low;
    m_viewHighMhz = high;
}

void SpectrumItem::onWaterfallRow(const QVector<float>& bins, double lowMhz, double highMhz)
{
    // While transmitting the TX rows come from the trace (onSpectrum); for
    // 400 ms after it the radio's AGC settles and its rows are dropped, as
    // the desktop's kPostTxAgcSettleMs.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (bins.isEmpty() || m_waterfall.isNull() || (m_engine && m_engine->radioTransmitting())
        || nowMs - m_txEndMs < kPostTxSettleMs) {
        return;
    }
    followView();
    // Black point: the tile's noise floor (mean of the bins under the mean),
    // smoothed across rows; held while transmitting. Without the RF gain, so
    // a gain step brightens or darkens the picture as on the desktop.
    const float gain = m_engine ? static_cast<float>(m_engine->rfGain()) : 0.0f;
    const float estimate = estimateNoiseFloorDbm(std::span<const float>(bins.constData(), bins.size()));
    if (estimate > -500.0f) {
        m_wfBlack = m_wfBlackSeeded ? 0.95f * m_wfBlack + 0.05f * (estimate - gain) : estimate - gain;
        m_wfBlackSeeded = true;
    }
    WaterfallLevelMap::Params p = waterfallParams(m_wfBlack);
    // Rows carry the RF gain; the black point does not.
    pushRow(bins, lowMhz, highMhz, p);
}

void SpectrumItem::pushRow(const QVector<float>& bins, double lowMhz, double highMhz,
                           const WaterfallLevelMap::Params& levels)
{
    if (bins.isEmpty() || m_waterfall.isNull()) {
        return;
    }
    const int w = m_waterfall.width();
    const int h = m_waterfall.height();
    // The newest row goes one place up the ring, over the oldest.
    m_wfHead = (m_wfHead + h - 1) % h;
    auto* row = reinterpret_cast<QRgb*>(m_waterfall.scanLine(m_wfHead));
    const qsizetype n = bins.size();
    // Place the row by frequency: it covers lowMhz..highMhz, which need not
    // be the pan's range (Flex rows lag a zoom or retune). Without a range,
    // stretch it across the view.
    const bool placed = highMhz > lowMhz && m_viewHighMhz > m_viewLowMhz;
    const double viewSpan = m_viewHighMhz - m_viewLowMhz;
    const double rowSpan = highMhz - lowMhz;
    for (int x = 0; x < w; ++x) {
        qsizetype i = 0;
        if (placed) {
            const double f = m_viewLowMhz + (x + 0.5) / w * viewSpan;
            const double pos = (f - lowMhz) / rowSpan * n;
            if (pos < 0.0 || pos >= n) {
                row[x] = qRgb(0, 0, 0);
                continue;
            }
            i = static_cast<qsizetype>(pos);
        } else {
            i = std::min<qsizetype>(n - 1, static_cast<qsizetype>(x) * n / w);
        }
        row[x] = m_palette[std::clamp(static_cast<int>(WaterfallLevelMap::level(bins.at(i), levels) * 255.0f + 0.5f), 0, 255)];
    }
    if (!m_wfFullUpload) {
        m_wfDirtyRows.append(m_wfHead);
        // Many rows waiting (the item hidden for a while): one picture.
        if (m_wfDirtyRows.size() > h / 4) {
            m_wfDirtyRows.clear();
            m_wfFullUpload = true;
        }
    }
    update();
}

double SpectrumItem::bandPlanHeight() const
{
    if (!m_engine || m_engine->bandPlan().isEmpty()) {
        return 0.0;
    }
    return std::clamp<qreal>(height() * 0.025, 14.0, 20.0);
}

void SpectrumItem::drawBandPlan(QPainter* painter, qreal w, qreal specH) const
{
    // The band plan as a strip along the bottom of the spectrum, as the
    // desktop draws it: each segment in its colour, mixed into the dark
    // background, with its label where it fits.
    if (!m_engine || m_engine->panBandwidthMhz() <= 0.0 || w <= 0) {
        return;
    }
    const QVector<BandPlanManager::Segment> segments = m_engine->bandPlanSegments();
    if (segments.isEmpty()) {
        return;
    }
    const double span = m_engine->panBandwidthMhz();
    const double low = m_engine->panCenterMhz() - span / 2.0;
    const double high = low + span;
    const qreal stripH = bandPlanHeight();
    const qreal y = specH - stripH;
    QFont font = painter->font();
    font.setPixelSize(static_cast<int>(stripH * 0.62));
    font.setBold(true);
    painter->setFont(font);
    const QFontMetricsF fm(font);
    const QColor bg(0x0a, 0x0a, 0x14);
    for (const BandPlanManager::Segment& seg : segments) {
        if (seg.highMhz <= low || seg.lowMhz >= high) {
            continue;
        }
        const qreal x1 = (std::max(seg.lowMhz, low) - low) / span * w;
        const qreal x2 = (std::min(seg.highMhz, high) - low) / span * w;
        if (x2 - x1 < 1.0) {
            continue;
        }
        // License contrast as the desktop: wider access brighter.
        float blend = 0.5f;
        if (seg.license == QLatin1String("E")) blend = 0.2f;
        else if (seg.license == QLatin1String("E,G")) blend = 0.4f;
        else if (seg.license.contains(QLatin1Char('T'))) blend = 0.6f;
        const QColor fill(static_cast<int>(seg.color.red() * blend + bg.red() * (1 - blend)),
                          static_cast<int>(seg.color.green() * blend + bg.green() * (1 - blend)),
                          static_cast<int>(seg.color.blue() * blend + bg.blue() * (1 - blend)));
        painter->fillRect(QRectF(x1, y, x2 - x1, stripH), fill);
        painter->setPen(QColor(10, 10, 20, 200));
        painter->drawLine(QPointF(x1, y), QPointF(x1, y + stripH));
        if (fm.horizontalAdvance(seg.label) + 6 < x2 - x1) {
            painter->setPen(Qt::white);
            painter->drawText(QRectF(x1, y, x2 - x1, stripH), Qt::AlignCenter, seg.label);
        }
    }
}

void SpectrumItem::drawFrequencyAxis(QPainter* painter, qreal w, qreal specH) const
{
    // Frequency scale at the top of the waterfall, as AetherSDR's: ticks on
    // a round step (1/2/5 x 10^n kHz) giving labels about 110 px apart.
    if (!m_engine || m_engine->panBandwidthMhz() <= 0.0 || w <= 0) {
        return;
    }
    const double span = m_engine->panBandwidthMhz();
    const double low = m_engine->panCenterMhz() - span / 2.0;
    const double wantedMhz = span * 110.0 / w;
    double step = 1e-6;  // 1 Hz
    for (const double decade : {1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1, 1.0}) {
        bool found = false;
        for (const double m : {1.0, 2.0, 5.0}) {
            if (decade * m >= wantedMhz) {
                step = decade * m;
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }
    const qreal axisH = axisHeight();
    painter->fillRect(QRectF(0, specH, w, axisH), QColor(11, 15, 20, 215));
    QFont font = painter->font();
    font.setPixelSize(static_cast<int>(axisH * 0.62));
    painter->setFont(font);
    // Decimals: enough to tell neighbouring labels apart.
    const int decimals = step >= 0.1 ? 1 : step >= 0.01 ? 2 : step >= 0.001 ? 3 : step >= 0.0001 ? 4 : 5;
    const double first = std::ceil(low / step) * step;
    for (double f = first; f <= low + span; f += step) {
        const qreal x = (f - low) / span * w;
        painter->setPen(QColor(230, 237, 243, 150));
        painter->drawLine(QPointF(x, specH), QPointF(x, specH + axisH * 0.3));
        const QString label = QString::number(f, 'f', decimals);
        const QRectF box(x - 50, specH + axisH * 0.25, 100, axisH * 0.75);
        if (box.left() < 0 || box.right() > w) {
            // Only labels that fit whole; keep the tick.
            const qreal textW = painter->fontMetrics().horizontalAdvance(label);
            if (x - textW / 2 < 2 || x + textW / 2 > w - 2) {
                continue;
            }
        }
        painter->setPen(QColor(200, 210, 220));
        painter->drawText(box, Qt::AlignHCenter | Qt::AlignVCenter, label);
    }
}

void SpectrumItem::drawTrace(QPainter* painter, qreal w, qreal specH) const
{
    painter->setPen(QColor(255, 255, 255, 25));
    for (int i = 1; i < kGridLines; ++i) {
        const qreal y = specH * i / kGridLines;
        painter->drawLine(QPointF(0, y), QPointF(w, y));
    }

    if (m_spectrum.size() > 1) {
        QPainterPath path;
        const qsizetype n = m_spectrum.size();
        for (qsizetype i = 0; i < n; ++i) {
            const qreal x = w * i / (n - 1);
            const float norm = (m_spectrum.at(i) - m_traceBottom) / (m_traceTop - m_traceBottom);
            const qreal y = specH * (1.0 - std::clamp(norm, 0.0f, 1.0f));
            if (i == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }
        QPainterPath fill = path;
        fill.lineTo(w, specH);
        fill.lineTo(0, specH);
        fill.closeSubpath();
        QLinearGradient gradient(0, 0, 0, specH);
        gradient.setColorAt(0.0, fillTop());
        gradient.setColorAt(1.0, fillBottom());
        painter->fillPath(fill, gradient);
        painter->setPen(QPen(kTraceColor, kTraceWidth));
        painter->drawPath(path);
    }

    // Centre line marks the pan centre; the slice marker is drawn in QML.
    painter->setPen(QColor(255, 255, 255, 60));
    painter->drawLine(QPointF(0, specH), QPointF(w, specH));
}

QImage SpectrumItem::waterfallInOrder() const
{
    if (m_waterfall.isNull() || m_wfHead == 0) {
        return m_waterfall;
    }
    const int h = m_waterfall.height();
    const qsizetype rowBytes = m_waterfall.bytesPerLine();
    QImage ordered(m_waterfall.size(), m_waterfall.format());
    const qsizetype first = h - m_wfHead;
    std::memcpy(ordered.bits(), m_waterfall.constScanLine(m_wfHead), rowBytes * first);
    std::memcpy(ordered.scanLine(static_cast<int>(first)), m_waterfall.constBits(), rowBytes * m_wfHead);
    return ordered;
}

void SpectrumItem::traceChanged()
{
    // In 3D the trace is not drawn: its frames change nothing here.
    if (m_engine && m_engine->spectrum3D()) {
        return;
    }
    m_traceDirty = true;
    update();
}

void SpectrumItem::stripChanged()
{
    m_stripDirty = true;
    polish();
}

void SpectrumItem::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemDevicePixelRatioHasChanged || change == ItemSceneChange) {
        stripChanged();
        traceChanged();
    }
}

void SpectrumItem::updatePolish()
{
    // The scale strip: the frequency axis at the top of the waterfall and
    // the band plan along the bottom of the spectrum, painted at the
    // screen's pixel density when the view, the plan or the size changed.
    if (!m_stripDirty) {
        return;
    }
    m_stripDirty = false;
    m_stripNew = true;
    m_strip = QImage();
    const qreal w = width();
    if (!m_engine || m_engine->panBandwidthMhz() <= 0.0 || w <= 0 || height() <= 0) {
        update();
        return;
    }
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const qreal specH = specHeight();
    // On a device pixel, so the strip lands where the item drew it before.
    const qreal top = std::floor((specH - bandPlanHeight()) * dpr) / dpr;
    const qreal bottom = specH + axisHeight();
    QImage strip(QSize(static_cast<int>(std::ceil(w * dpr)), static_cast<int>(std::ceil((bottom - top) * dpr))),
                 QImage::Format_ARGB32_Premultiplied);
    strip.setDevicePixelRatio(dpr);
    strip.fill(Qt::transparent);
    QPainter painter(&strip);
    painter.translate(0, -top);
    drawFrequencyAxis(&painter, w, specH);
    drawBandPlan(&painter, w, specH);
    painter.end();
    m_strip = strip;
    m_stripTop = top;
    update();
}

QSGNode* SpectrumItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    QQuickWindow* win = window();
    if (!win) {
        delete oldNode;
        return nullptr;
    }
    const bool software = !QSGRendererInterface::isApiRhiBased(win->rendererInterface()->graphicsApi());
    auto* node = static_cast<SpectrumNode*>(oldNode);
    if (!node) {
        node = new SpectrumNode;
        node->background = win->createRectangleNode();
        node->background->setColor(kBackground);
        node->appendChildNode(node->background);
        QImage pixel(1, 1, QImage::Format_ARGB32_Premultiplied);
        pixel.fill(Qt::transparent);
        node->placeholder = win->createTextureFromImage(pixel);
        node->wfTop = win->createImageNode();
        node->wfBottom = win->createImageNode();
        for (QSGImageNode* part : {node->wfTop, node->wfBottom}) {
            part->setFiltering(QSGTexture::Nearest);
            part->setOwnsTexture(false);
            part->setTexture(node->placeholder);
            node->appendChildNode(part);
        }
        node->strip = win->createImageNode();
        node->strip->setFiltering(QSGTexture::Linear);
        node->strip->setOwnsTexture(false);
        node->strip->setTexture(node->placeholder);
        node->appendChildNode(node->strip);
        node->trace = new QSGNode;
        if (software) {
            node->traceImage = win->createImageNode();
            node->traceImage->setFiltering(QSGTexture::Linear);
            node->traceImage->setOwnsTexture(false);
            node->traceImage->setTexture(node->placeholder);
            node->trace->appendChildNode(node->traceImage);
        } else {
            for (QSGRectangleNode*& line : node->grid) {
                line = win->createRectangleNode();
                line->setColor(QColor(255, 255, 255, 25));
                node->trace->appendChildNode(line);
            }
            node->fill = new QSGGeometryNode;
            auto* fillGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
            fillGeometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            node->fill->setGeometry(fillGeometry);
            node->fill->setFlag(QSGNode::OwnsGeometry);
            node->fill->setMaterial(new QSGVertexColorMaterial);
            node->fill->setFlag(QSGNode::OwnsMaterial);
            node->trace->appendChildNode(node->fill);
            node->line = new QSGGeometryNode;
            auto* lineGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
            lineGeometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            node->line->setGeometry(lineGeometry);
            node->line->setFlag(QSGNode::OwnsGeometry);
            auto* lineMaterial = new QSGFlatColorMaterial;
            lineMaterial->setColor(kTraceColor);
            node->line->setMaterial(lineMaterial);
            node->line->setFlag(QSGNode::OwnsMaterial);
            node->trace->appendChildNode(node->line);
            node->centre = win->createRectangleNode();
            node->centre->setColor(QColor(255, 255, 255, 60));
            node->trace->appendChildNode(node->centre);
        }
        m_wfFullUpload = true;
        m_wfDirtyRows.clear();
        m_traceDirty = true;
        m_stripNew = true;
    }

    const qreal w = width();
    const qreal specH = specHeight();
    node->background->setRect(boundingRect());

    // Waterfall: the ring's rows from the newest (m_wfHead) down to its
    // end, then its top part, as many as the waterfall area shows.
    if (m_waterfall.isNull()) {
        node->wfTop->setRect(QRectF());
        node->wfBottom->setRect(QRectF());
    } else {
        const int rows = m_waterfall.height();
        if (software) {
            if (m_wfFullUpload || !m_wfDirtyRows.isEmpty() || !node->wfTexture) {
                QSGTexture* texture = win->createTextureFromImage(m_waterfall);
                node->wfTop->setTexture(texture);
                node->wfBottom->setTexture(texture);
                delete node->wfTexture;
                node->wfTexture = texture;
            }
        } else {
            auto* texture = static_cast<WaterfallTexture*>(node->wfTexture);
            if (!texture) {
                texture = new WaterfallTexture;
                node->wfTexture = texture;
                m_wfFullUpload = true;
            }
            if (m_wfFullUpload || texture->textureSize() != m_waterfall.size()
                || texture->pendingRows() + m_wfDirtyRows.size() > rows / 4) {
                texture->setImage(m_waterfall);
            } else {
                for (int y : std::as_const(m_wfDirtyRows)) {
                    texture->addRow(m_waterfall, y);
                }
            }
            if (node->wfTop->texture() != texture) {
                node->wfTop->setTexture(texture);
                node->wfBottom->setTexture(texture);
            }
            if (m_wfFullUpload || !m_wfDirtyRows.isEmpty()) {
                // The rows reach the GPU when the material is next prepared.
                node->wfTop->markDirty(QSGNode::DirtyMaterial);
                node->wfBottom->markDirty(QSGNode::DirtyMaterial);
            }
        }
        m_wfFullUpload = false;
        m_wfDirtyRows.clear();
        const int shown = std::clamp(static_cast<int>(height()) - static_cast<int>(specH), 0, rows);
        const int first = std::min(shown, rows - m_wfHead);
        const int wfW = m_waterfall.width();
        node->wfTop->setSourceRect(QRectF(0, m_wfHead, wfW, first));
        node->wfTop->setRect(first > 0 ? QRectF(0, specH, wfW, first) : QRectF());
        node->wfBottom->setSourceRect(QRectF(0, 0, wfW, shown - first));
        node->wfBottom->setRect(shown > first ? QRectF(0, specH + first, wfW, shown - first) : QRectF());
    }

    // Scale strip, painted in updatePolish.
    if (m_stripNew) {
        m_stripNew = false;
        QSGTexture* texture = m_strip.isNull() ? nullptr : win->createTextureFromImage(m_strip);
        if (texture) {
            node->strip->setTexture(texture);
            node->strip->setRect(QRectF(QPointF(0, m_stripTop), m_strip.deviceIndependentSize()));
        } else {
            node->strip->setTexture(node->placeholder);
            node->strip->setRect(QRectF());
        }
        delete node->stripTexture;
        node->stripTexture = texture;
    }

    // Trace: out of the tree in 3D.
    const bool show2D = !(m_engine && m_engine->spectrum3D());
    if (show2D != (node->trace->parent() != nullptr)) {
        if (show2D) {
            node->appendChildNode(node->trace);
            m_traceDirty = true;
        } else {
            node->removeChildNode(node->trace);
        }
    }
    if (show2D && m_traceDirty) {
        m_traceDirty = false;
        if (software) {
            const qreal dpr = win->effectiveDevicePixelRatio();
            QImage image(QSize(static_cast<int>(std::ceil(w * dpr)), static_cast<int>(std::ceil((specH + 1) * dpr))),
                         QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(dpr);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            drawTrace(&painter, w, specH);
            painter.end();
            QSGTexture* texture = image.isNull() ? nullptr : win->createTextureFromImage(image);
            if (texture) {
                node->traceImage->setTexture(texture);
                node->traceImage->setRect(QRectF(QPointF(0, 0), image.deviceIndependentSize()));
            } else {
                node->traceImage->setTexture(node->placeholder);
                node->traceImage->setRect(QRectF());
            }
            delete node->traceTexture;
            node->traceTexture = texture;
        } else {
            for (int i = 1; i < kGridLines; ++i) {
                node->grid[i - 1]->setRect(QRectF(0, specH * i / kGridLines, w, 1));
            }
            node->centre->setRect(QRectF(0, specH, w, 1));
            const qsizetype n = m_spectrum.size() > 1 ? m_spectrum.size() : 0;
            QSGGeometry* fill = node->fill->geometry();
            QSGGeometry* line = node->line->geometry();
            fill->allocate(static_cast<int>(2 * n));
            line->allocate(static_cast<int>(2 * n));
            if (n > 0) {
                QVarLengthArray<QPointF, 2048> points(n);
                for (qsizetype i = 0; i < n; ++i) {
                    const float norm = (m_spectrum.at(i) - m_traceBottom) / (m_traceTop - m_traceBottom);
                    points[i] = QPointF(w * i / (n - 1), specH * (1.0 - std::clamp(norm, 0.0f, 1.0f)));
                }
                // Fill: a strip from the trace down to the bottom, each
                // vertex in the gradient's colour at its height
                // (premultiplied, as the material takes it).
                const QColor top = fillTop();
                const QColor bottom = fillBottom();
                const auto gradientAt = [&](qreal t, QSGGeometry::ColoredPoint2D& v, float x, float y) {
                    const qreal a = top.alpha() + (bottom.alpha() - top.alpha()) * t;
                    const auto channel = [&](int from, int to) {
                        return static_cast<uchar>(std::lround((from + (to - from) * t) * a / 255.0));
                    };
                    v.set(x, y, channel(top.red(), bottom.red()), channel(top.green(), bottom.green()),
                          channel(top.blue(), bottom.blue()), static_cast<uchar>(std::lround(a)));
                };
                QSGGeometry::ColoredPoint2D* fv = fill->vertexDataAsColoredPoint2D();
                QSGGeometry::Point2D* lv = line->vertexDataAsPoint2D();
                const qreal half = kTraceWidth / 2.0;
                for (qsizetype i = 0; i < n; ++i) {
                    const QPointF p = points[i];
                    gradientAt(specH > 0 ? p.y() / specH : 0.0, fv[2 * i], p.x(), p.y());
                    gradientAt(1.0, fv[2 * i + 1], p.x(), specH);
                    // Line: a ribbon kTraceWidth wide across the trace's
                    // direction at each point.
                    const QPointF d = points[std::min(i + 1, n - 1)] - points[std::max<qsizetype>(i - 1, 0)];
                    const qreal len = std::hypot(d.x(), d.y());
                    const QPointF normal = len > 0 ? QPointF(-d.y() / len, d.x() / len) * half : QPointF(0, half);
                    lv[2 * i].set(p.x() + normal.x(), p.y() + normal.y());
                    lv[2 * i + 1].set(p.x() - normal.x(), p.y() - normal.y());
                }
            }
            node->fill->markDirty(QSGNode::DirtyGeometry);
            node->line->markDirty(QSGNode::DirtyGeometry);
        }
    }
    return node;
}

} // namespace AetherSDR::mobile
