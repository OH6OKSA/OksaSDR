// Auto AGC-T (roadmap 32 and 48). The radio's own Auto AGC-T needs SmartSDR
// 4.3.16 and FlexRadio's paid Early Access licence (auto_agc_t); where the
// radio has it, the AGC panel's Auto turns it on and off (the engine's
// SliceModel::requestAgcThresholdMode, upstream #6508). Every other radio
// gets the app's own: as Thetis sets AGC-T from the noise floor, but the
// radio's 0-100 knee has no dBm mapping, so the app finds it by ear with the
// desktop's AgcTCalibrator: on a clear spot AGC-T is stepped from 100 down
// (half a second a step, about 13 s) while the post-AGC audio level is read,
// and the knee, where the noise starts to drop, is kept. The value is
// remembered for the band and the mode group (voice, CW, data: the filter's
// width moves the knee) and set again whenever the slice comes back to that
// band and group, until the slider is moved by hand or Auto measures again.
//
// Auto: follow the noise (roadmap 48, Thetis's Auto AGC): with it on, a
// measurement also keeps the noise floor at the slice then, the dB the noise
// rose per AGC-T step above the knee (the AGC's gain a step) and the filter's
// width; every second the floor at the slice is read again (the pan's bins
// around the passband, a noise estimate that leaves signals out: an AM
// carrier, FT8's many tones and a keyed CW signal alike), smoothed over
// about 5 s, and when it calls for a move worth 3 dB or more for 10 s, AGC-T
// goes to the measured value less the floor's rise over the slope. Held in
// FM (the radio has no AGC there), in AGC off (AGC-T is a fixed gain), while
// the radio's own Auto is on, while transmitting and 2 s after, while Auto
// measures, and with a filter half again as wide or narrow as measured with
// (CW's 250 Hz after a 500 Hz measurement: Auto again). Moving the slider by
// hand lets the band and mode's values go, so nothing follows there until
// Auto measures again.

#include "MobileEngine.h"

#include "core/AgcTCalibrator.h"
#include "gui/NoiseFloorEstimator.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "models/BandSettings.h"
#include "models/MeterModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QDateTime>

#include <cmath>
#include <limits>
#include <span>
#include <vector>
#include <algorithm>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
// No bin in the passband this far over the display's noise floor: a clear
// spot (an averaged noise trace's highest bin stands a few dB over its
// median; a carrier or a voice tens of dB).
constexpr double kQuietMarginDb = 12.0;
// The audio level must move at least this much over the sweep for its bend
// to be the knee.
constexpr float kMinCurveSpanDb = 3.0f;

QString groupName(const QString& group)
{
    return group == QLatin1String("cw") ? QObject::tr("CW")
        : group == QLatin1String("data") ? QObject::tr("data")
        : group == QLatin1String("fm") ? QObject::tr("FM") : QObject::tr("voice");
}

const QString kFollowKey = QStringLiteral("MobileAgcFollow");
// A move only when the floor calls for one worth this much, this long.
constexpr double kFollowMoveDb = 3.0;
constexpr int kFollowHoldSeconds = 10;
constexpr qint64 kFollowMinGapMs = 5000;
constexpr qint64 kFollowAfterTxMs = 2000;
// The floor's smoothing a second: about 5 s.
constexpr double kFollowAlpha = 0.18;
// A filter this many times wider or narrower than measured with holds it.
constexpr double kFollowWidthRatio = 1.5;

// The dB the post-AGC noise rose per AGC-T step below the knee (above it the
// AGC holds the noise at its level): the AGC's gain a step, a least-squares
// fit; NaN when the curve has no slope there. On the FLEX about 1 dB a step.
double kneeSlope(const QVector<AetherSDR::AgcTCalibrator::Point>& curve, int knee)
{
    double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (const AetherSDR::AgcTCalibrator::Point& p : curve) {
        if (p.value < knee) {
            n += 1;
            sx += p.value;
            sy += p.rmsDb;
            sxx += double(p.value) * p.value;
            sxy += double(p.value) * p.rmsDb;
        }
    }
    const double d = n * sxx - sx * sx;
    if (n < 3 || std::abs(d) < 1e-9) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double slope = (n * sxy - sx * sy) / d;
    return slope >= 0.05 ? std::min(slope, 3.0) : std::numeric_limits<double>::quiet_NaN();
}
} // namespace

void MobileEngine::setupAgcAuto()
{
    m_agcCal = new AgcTCalibrator(this);
    // Each step's level a mean over the second half of this (the radio's
    // AGC settles in the first): 26 steps, about 13 s.
    m_agcCal->setSettleMs(500);
    // The post-AGC audio level, from the audio thread.
    connect(m_audio, &AudioEngine::levelChanged, m_agcCal, &AgcTCalibrator::onAudioLevel, Qt::QueuedConnection);
    connect(&m_session.radioModel().meterModel(), &MeterModel::sLevelChanged, m_agcCal, &AgcTCalibrator::onSLevel);
    connect(m_agcCal, &AgcTCalibrator::progress, this, [this](int, int percent) {
        m_agcProgress = percent;
        emit agcAutoChanged();
    });
    connect(m_agcCal, &AgcTCalibrator::recommendation, this, [this](int value, bool knee) {
        m_agcKnee = knee;
        qCInfo(lcMobile) << "Auto AGC-T: recommendation" << value << (knee ? "(the knee)" : "(no clear knee)");
    });
    connect(m_agcCal, &AgcTCalibrator::finished, this, [this](bool applied) {
        if (applied) {
            const int value = m_agcCal->recommendedValue();
            const QString key = agcAutoKey();
            if (value >= 0 && !key.isEmpty()) {
                AppSettings::instance().setValue(key, value);
                // For following the noise: the floor, the slope and the
                // filter it was measured with.
                const double floorDbm = localFloorDbm();
                const double slope = kneeSlope(m_agcCal->curve(), value);
                const int width = m_slice ? m_slice->filterHigh() - m_slice->filterLow() : 0;
                AppSettings::instance().setValue(key + QStringLiteral("_floor"), std::isfinite(floorDbm) ? QString::number(floorDbm, 'f', 1) : QString());
                AppSettings::instance().setValue(key + QStringLiteral("_slope"), std::isfinite(slope) ? QString::number(slope, 'f', 3) : QString());
                AppSettings::instance().setValue(key + QStringLiteral("_width"), width);
                AppSettings::instance().save();
                qCInfo(lcMobile).noquote() << QStringLiteral("Auto AGC-T: %1 kept with the floor %2 dBm, %3 dB a step, %4 Hz filter")
                                                  .arg(key, std::isfinite(floorDbm) ? QString::number(floorDbm, 'f', 1) : QStringLiteral("-"),
                                                       std::isfinite(slope) ? QString::number(slope, 'f', 2) : QStringLiteral("-"))
                                                  .arg(width);
                m_agcRecallKey = key;
                m_agcFollowKey.clear();
            }
            m_agcMeasuring = false;
            const QString band = m_slice ? BandSettings::bandForFrequency(m_slice->frequency()) : QString();
            m_agcNote = m_agcKnee
                ? tr("AGC-T %1 measured for %2 %3; set again there.").arg(value).arg(band, groupName(agcModeClass(m_slice ? m_slice->mode() : QString())))
                : tr("AGC-T %1 set for %2 %3 (no clear knee: a best guess; try a quieter spot).").arg(value).arg(band, groupName(agcModeClass(m_slice ? m_slice->mode() : QString())));
            qCInfo(lcMobile).noquote() << "Auto AGC-T:" << m_agcNote;
            emit agcAutoChanged();
            return;
        }
        // The sweep ended with a value in place: keep it (applied -> true),
        // unless the level never moved with AGC-T: then the "knee" is only
        // the curve's end (no receive audio, or nothing for AGC to act on).
        if (m_agcMeasuring && !m_agcStopping && m_agcCal->hasRecommendation()) {
            const QVector<AgcTCalibrator::Point> curve = m_agcCal->curve();
            float lowDb = 0.0f;
            float highDb = -1000.0f;
            QStringList points;
            for (const AgcTCalibrator::Point& p : curve) {
                lowDb = points.isEmpty() ? p.rmsDb : std::min(lowDb, p.rmsDb);
                highDb = std::max(highDb, p.rmsDb);
                points << QStringLiteral("%1:%2").arg(p.value).arg(p.rmsDb, 0, 'f', 1);
            }
            qCInfo(lcMobile).noquote() << "Auto AGC-T: curve (AGC-T:dB)" << points.join(QLatin1Char(' '));
            if (curve.size() >= 3 && highDb - lowDb >= kMinCurveSpanDb) {
                QTimer::singleShot(0, this, [this] {
                    if (m_agcCal && m_agcMeasuring) {
                        m_agcCal->applyRecommendation();
                    }
                });
                return;
            }
            // Flat: AGC-T put back. The sweep is over, so stop() sends no
            // second finished: the measurement ends here (build 162 stayed
            // at "Measuring… 100 %").
            m_agcStopping = true;
            m_agcFlat = true;
            m_agcCal->stop();
        }
        m_agcMeasuring = false;
        m_agcNote = m_agcFlat ? tr("No knee heard: the noise did not change with AGC-T (no receive audio, or the AGC is not acting here). AGC-T put back.")
                  : m_agcStopping ? tr("Measurement stopped: AGC-T put back.")
                                  : tr("No result: AGC-T put back.");
        m_agcStopping = false;
        m_agcFlat = false;
        qCInfo(lcMobile).noquote() << "Auto AGC-T:" << m_agcNote;
        emit agcAutoChanged();
    });
    connect(&m_session.radioModel(), &RadioModel::licenseFeaturesChanged, this, &MobileEngine::agcAutoChanged);
    m_agcRecallTimer.setSingleShot(true);
    m_agcRecallTimer.setInterval(900);
    connect(&m_agcRecallTimer, &QTimer::timeout, this, &MobileEngine::recallAgcThreshold);
    m_agcFollowTimer.setInterval(1000);
    connect(&m_agcFollowTimer, &QTimer::timeout, this, &MobileEngine::agcFollowTick);
    if (agcFollowOn()) {
        m_agcFollowTimer.start();
    }
}

// A new active slice: the calibrator follows it, and the band and mode
// group are watched for the remembered value.
void MobileEngine::bindAgcAutoSlice()
{
    if (m_agcMeasuring && m_agcCal) {
        m_agcStopping = true;
        m_agcCal->stop();
    }
    if (m_agcCal) {
        m_agcCal->setSlice(m_slice);
    }
    if (!m_slice) {
        return;
    }
    m_sliceConnections.append(connect(m_slice, &SliceModel::agcThresholdModeChanged, this, &MobileEngine::agcAutoChanged));
    m_sliceConnections.append(connect(m_slice, &SliceModel::agcThresholdChanged, this, &MobileEngine::agcAutoChanged));
    m_sliceConnections.append(connect(m_slice, &SliceModel::modeChanged, this, &MobileEngine::scheduleAgcRecall));
    m_sliceConnections.append(connect(m_slice, &SliceModel::frequencyChanged, this, &MobileEngine::scheduleAgcRecall));
    m_agcRecallKey.clear();
    scheduleAgcRecall();
    emit agcAutoChanged();
}

QString MobileEngine::agcAutoKey() const
{
    if (!m_slice) {
        return {};
    }
    return QStringLiteral("MobileAgcT_%1_%2").arg(BandSettings::bandForFrequency(m_slice->frequency()),
                                                   agcModeClass(m_slice->mode()));
}

void MobileEngine::scheduleAgcRecall()
{
    const QString key = agcAutoKey();
    if (!key.isEmpty() && key != m_agcRecallKey) {
        m_agcRecallKey = key;
        // After the radio's own band recall and the app's per-mode AGC.
        m_agcRecallTimer.start();
        emit agcAutoChanged();
    }
}

void MobileEngine::recallAgcThreshold()
{
    if (!m_slice || m_agcMeasuring || m_slice->automaticAgcThresholdActiveOrPending()) {
        return;
    }
    const QString key = agcAutoKey();
    const int value = AppSettings::instance().value(key, -1).toInt();
    if (value < 0 || value == m_slice->agcThreshold()) {
        return;
    }
    qCInfo(lcMobile).noquote() << QStringLiteral("Auto AGC-T: %1 (measured) for %2").arg(value).arg(key);
    m_agcRecalling = true;
    m_slice->recallAgcThreshold(value);
    m_agcRecalling = false;
}

double MobileEngine::passbandOverFloorDb() const
{
    const qsizetype n = m_lastFftBins.size();
    const double bandwidth = panBandwidthMhz();
    if (!m_slice || n < 16 || bandwidth <= 0.0 || QDateTime::currentMSecsSinceEpoch() - m_lastFftMs > 3000) {
        qCInfo(lcMobile) << "Auto AGC-T: no FFT frame to check: bins" << n << "span" << bandwidth
                         << "age ms" << (QDateTime::currentMSecsSinceEpoch() - m_lastFftMs);
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double lowMhz = panCenterMhz() - bandwidth / 2.0;
    const double binMhz = bandwidth / static_cast<double>(n);
    const double slice = m_slice->frequency();
    const qsizetype lo = std::max<qsizetype>(0, static_cast<qsizetype>(std::floor((slice + m_slice->filterLow() / 1e6 - lowMhz) / binMhz)));
    const qsizetype hi = std::min<qsizetype>(n - 1, static_cast<qsizetype>(std::ceil((slice + m_slice->filterHigh() / 1e6 - lowMhz) / binMhz)));
    if (lo > hi) {
        qCInfo(lcMobile) << "Auto AGC-T: the passband is outside the pan:" << slice << lowMhz << bandwidth << lo << hi;
        return std::numeric_limits<double>::quiet_NaN();
    }
    float peak = -1000.0f;
    qsizetype peakAt = lo;
    std::vector<float> inBand;
    for (qsizetype i = lo; i <= hi; ++i) {
        if (std::isfinite(m_lastFftBins.at(i))) {
            inBand.push_back(m_lastFftBins.at(i));
            if (m_lastFftBins.at(i) > peak) {
                peak = m_lastFftBins.at(i);
                peakAt = i;
            }
        }
    }
    const float floorDbm = estimateNoiseFloorDbm(std::span<const float>(m_lastFftBins.constData(), n));
    if (!inBand.empty()) {
        std::nth_element(inBand.begin(), inBand.begin() + inBand.size() / 2, inBand.end());
        qCInfo(lcMobile).noquote() << QStringLiteral("Auto AGC-T: passband %1 bins, peak %2 dBm at %3 MHz, median %4 dBm, floor %5 dBm (pan %6 MHz, %7 kHz)")
                                          .arg(inBand.size()).arg(peak, 0, 'f', 1).arg(lowMhz + (peakAt + 0.5) * binMhz, 0, 'f', 5)
                                          .arg(inBand[inBand.size() / 2], 0, 'f', 1).arg(floorDbm, 0, 'f', 1)
                                          .arg(panCenterMhz(), 0, 'f', 4).arg(bandwidth * 1000.0, 0, 'f', 1);
    }
    return peak > -999.0f && floorDbm > -999.0f ? static_cast<double>(peak - floorDbm) : std::numeric_limits<double>::quiet_NaN();
}

void MobileEngine::noteSpectrumFloor(double dbm)
{
    m_spectrumFloorDbm = dbm;
    if (m_agcCal) {
        m_agcCal->setNoiseFloorDb(static_cast<float>(dbm));
    }
}

QVariantMap MobileEngine::agcAuto() const
{
    const RadioModel& rm = m_session.radioModel();
    const bool radioAvailable = m_slice && rm.autoAgcThresholdUnavailableReason(m_slice).isEmpty();
    const auto mode = m_slice ? m_slice->agcThresholdMode() : std::nullopt;
    const bool radioOn = radioAvailable && mode && *mode == AgcThresholdMode::Automatic;
    const int remembered = m_slice ? AppSettings::instance().value(agcAutoKey(), -1).toInt() : -1;
    const bool noAgc = m_slice && agcModeClass(m_slice->mode()) == QLatin1String("fm");
    QString note = m_agcNote;
    if (noAgc) {
        note = tr("FM has no AGC: the radio does not use AGC-T in FM.");
    } else if (radioAvailable) {
        note = radioOn ? tr("The radio sets AGC-T itself (Auto); moving the slider sets it by hand again.")
                       : tr("Auto: the radio sets AGC-T itself (its Early Access licence).");
    } else if (m_agcMeasuring) {
        note = tr("Measuring AGC-T… %1 % (the noise level changes meanwhile)").arg(m_agcProgress);
    } else if (note.isEmpty()) {
        note = remembered >= 0
            ? tr("Auto measured %1 for this band and mode; set again whenever you come back.").arg(remembered)
            : tr("Auto finds the knee on a clear spot (about 13 s) and keeps it for this band and mode.");
    }
    return {{QStringLiteral("radioAvailable"), radioAvailable},
            {QStringLiteral("radioOn"), radioOn},
            {QStringLiteral("radioPending"), m_slice && m_slice->automaticAgcThresholdActiveOrPending() && !radioOn},
            {QStringLiteral("measuring"), m_agcMeasuring},
            {QStringLiteral("progress"), m_agcProgress},
            {QStringLiteral("remembered"), remembered},
            {QStringLiteral("noAgc"), noAgc},
            {QStringLiteral("follow"), agcFollowOn()},
            {QStringLiteral("followNote"), agcFollowOn() ? m_agcFollowNote : QString()},
            {QStringLiteral("note"), note}};
}

void MobileEngine::agcAutoPress()
{
    if (!m_slice || !m_connected) {
        return;
    }
    const QVariantMap state = agcAuto();
    if (state.value(QStringLiteral("radioAvailable")).toBool()) {
        const bool on = state.value(QStringLiteral("radioOn")).toBool();
        qCInfo(lcMobile) << "Auto AGC-T: the radio's" << (on ? "off" : "on");
        m_slice->requestAgcThresholdMode(on ? AgcThresholdMode::Manual : AgcThresholdMode::Automatic);
        emit agcAutoChanged();
        return;
    }
    if (m_agcMeasuring) {
        m_agcStopping = true;
        m_agcCal->stop();
        return;
    }
    if (agcModeClass(m_slice->mode()) == QLatin1String("fm")) {
        return;  // no AGC in FM: the button is dimmed
    }
    if (m_pttHeld || m_session.radioModel().transmitModel().isTransmitting()) {
        m_agcNote = tr("Not while transmitting.");
        emit agcAutoChanged();
        return;
    }
    // A signal in the passband would read as the knee: none of its bins in
    // the pan's latest FFT frame may stand well over the noise floor.
    const double overFloor = passbandOverFloorDb();
    const bool passbandKnown = std::isfinite(overFloor);
    if (passbandKnown && overFloor > kQuietMarginDb) {
        m_agcNote = tr("A signal in the passband (%1 dB over the noise): tune to a clear spot and press Auto again.")
                        .arg(qRound(overFloor));
        qCInfo(lcMobile).noquote() << "Auto AGC-T refused:" << m_agcNote;
        emit agcAutoChanged();
        return;
    }
    if (!m_slice->agcCalibrationAllowed()) {
        m_agcNote = tr("AGC-T cannot be measured on this slice now.");
        emit agcAutoChanged();
        return;
    }
    m_agcCal->setSlice(m_slice);
    m_agcMeasuring = true;
    m_agcStopping = false;
    m_agcKnee = true;
    m_agcProgress = 0;
    m_agcNote.clear();
    qCInfo(lcMobile).noquote() << QStringLiteral("Auto AGC-T: measuring on %1 MHz %2, AGC %3, from %4 (S meter %5 dBm, passband peak %6 dB over the noise%7)")
                                      .arg(m_slice->frequency(), 0, 'f', 4).arg(m_slice->mode(), m_slice->agcMode())
                                      .arg(m_slice->agcThreshold()).arg(qRound(m_sMeterDbm)).arg(passbandKnown ? qRound(overFloor) : 0)
                                      .arg(passbandKnown ? QString() : QStringLiteral(", no FFT frame"));
    m_agcCal->startAutoSweep();
    if (!m_agcCal->isRunning()) {
        m_agcMeasuring = false;
        m_agcNote = tr("AGC-T cannot be measured on this slice now.");
    }
    emit agcAutoChanged();
}

// Twice a second, from the averaged frame, while the line is shown.
void MobileEngine::updateFloorEstimate()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!noiseFloorLine() || now - m_floorEstimateMs < 500 || m_lastFftBins.size() < 16) {
        return;
    }
    m_floorEstimateMs = now;
    const float f = estimateNoiseFloorDbm(std::span<const float>(m_lastFftBins.constData(), static_cast<size_t>(m_lastFftBins.size())));
    if (f > -999.0f && std::abs(f - m_floorEstimateDbm) >= 0.3) {
        m_floorEstimateDbm = f;
        emit noiseFloorEstimateChanged();
    }
}

bool MobileEngine::agcFollowOn() const
{
    return AppSettings::instance().value(kFollowKey).toString() == QLatin1String("1");
}

void MobileEngine::setAgcFollow(bool on)
{
    AppSettings::instance().setValue(kFollowKey, on ? QStringLiteral("1") : QStringLiteral("0"));
    AppSettings::instance().save();
    m_agcFollowKey.clear();
    m_agcFollowNote.clear();
    m_agcFollowTicks = 0;
    if (on) {
        m_agcFollowTimer.start();
        agcFollowTick();
    } else {
        m_agcFollowTimer.stop();
    }
    qCInfo(lcMobile) << "Auto AGC-T: follow the noise" << (on ? "on" : "off");
    emit agcAutoChanged();
}

// The noise floor at the slice: the latest FFT frame's bins around the
// passband (5 kHz either side of its middle, or its own width when wider),
// by the display's noise estimate (a low quantile, signals left out): the
// noise slopes across a wide pan, so not the whole pan's floor.
double MobileEngine::localFloorDbm() const
{
    const qsizetype n = m_lastFftBins.size();
    const double bandwidth = panBandwidthMhz();
    if (!m_slice || n < 16 || bandwidth <= 0.0 || QDateTime::currentMSecsSinceEpoch() - m_lastFftMs > 3000) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double lowMhz = panCenterMhz() - bandwidth / 2.0;
    const double binMhz = bandwidth / static_cast<double>(n);
    const double middle = m_slice->frequency() + (m_slice->filterLow() + m_slice->filterHigh()) / 2.0e6;
    const double half = std::max(0.005, (m_slice->filterHigh() - m_slice->filterLow()) / 1.0e6);
    qsizetype lo = std::max<qsizetype>(0, static_cast<qsizetype>(std::floor((middle - half - lowMhz) / binMhz)));
    qsizetype hi = std::min<qsizetype>(n - 1, static_cast<qsizetype>(std::ceil((middle + half - lowMhz) / binMhz)));
    if (hi - lo + 1 < 16) {
        // A narrow span: the whole frame.
        lo = 0;
        hi = n - 1;
    }
    const float floorDbm = estimateNoiseFloorDbm(std::span<const float>(m_lastFftBins.constData() + lo, static_cast<size_t>(hi - lo + 1)));
    return floorDbm > -999.0f ? static_cast<double>(floorDbm) : std::numeric_limits<double>::quiet_NaN();
}

void MobileEngine::agcFollowTick()
{
    if (!agcFollowOn()) {
        m_agcFollowTimer.stop();
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (radioTransmitting() || m_pttHeld) {
        m_agcFollowTxMs = now;
    }
    QString hold;
    const QString key = agcAutoKey();
    const AppSettings& s = AppSettings::instance();
    const int measured = key.isEmpty() ? -1 : s.value(key, -1).toInt();
    bool okFloor = false;
    bool okSlope = false;
    const double measuredFloor = key.isEmpty() ? 0.0 : s.value(key + QStringLiteral("_floor")).toString().toDouble(&okFloor);
    const double slope = key.isEmpty() ? 0.0 : s.value(key + QStringLiteral("_slope")).toString().toDouble(&okSlope);
    const int measuredWidth = key.isEmpty() ? 0 : s.value(key + QStringLiteral("_width"), 0).toInt();
    const int width = m_slice ? m_slice->filterHigh() - m_slice->filterLow() : 0;
    if (!m_slice || !m_connected) {
        hold = QStringLiteral(" ");
    } else if (agcModeClass(m_slice->mode()) == QLatin1String("fm")) {
        hold = tr("FM has no AGC: nothing to follow.");
    } else if (m_slice->agcMode() == QLatin1String("off")) {
        hold = tr("AGC is off: AGC-T is a fixed gain there, not followed.");
    } else if (m_slice->automaticAgcThresholdActiveOrPending()) {
        hold = tr("The radio sets AGC-T itself (its Auto).");
    } else if (m_agcMeasuring) {
        hold = tr("Measuring.");
    } else if (now - m_agcFollowTxMs < kFollowAfterTxMs + 1000) {
        hold = tr("Held while transmitting.");
    } else if (measured < 0 || !okFloor || !okSlope || slope <= 0.0) {
        hold = tr("Follows once Auto has measured this band and mode.");
    } else if (measuredWidth > 0 && width > 0
               && (width > measuredWidth * kFollowWidthRatio || width * kFollowWidthRatio < measuredWidth)) {
        hold = tr("The filter (%1 Hz) is not the width Auto measured with (%2 Hz): press Auto again.").arg(width).arg(measuredWidth);
    }
    const auto setNote = [this](const QString& note) {
        if (note != m_agcFollowNote) {
            m_agcFollowNote = note;
            emit agcAutoChanged();
        }
    };
    if (!hold.isEmpty()) {
        m_agcFollowTicks = 0;
        setNote(hold.trimmed());
        return;
    }
    const double floorNow = localFloorDbm();
    if (!std::isfinite(floorNow)) {
        return;
    }
    if (key != m_agcFollowKey) {
        // A new band or mode group (or a new measurement): from the floor
        // read now.
        m_agcFollowKey = key;
        m_agcFollowFloor = floorNow;
        m_agcFollowTicks = 0;
    } else {
        m_agcFollowFloor += kFollowAlpha * (floorNow - m_agcFollowFloor);
    }
    const int target = std::clamp(static_cast<int>(std::lround(measured - (m_agcFollowFloor - measuredFloor) / slope)), 0, 100);
    const int current = m_slice->agcThreshold();
    if (std::abs(target - current) * slope >= kFollowMoveDb) {
        ++m_agcFollowTicks;
    } else {
        m_agcFollowTicks = 0;
    }
    if (m_agcFollowTicks >= kFollowHoldSeconds && now - m_agcFollowMovedMs >= kFollowMinGapMs) {
        qCInfo(lcMobile).noquote() << QStringLiteral("Auto AGC-T follows the noise: floor %1 dBm (measured %2 at %3 dBm, %4 dB a step): %5 -> %6")
                                          .arg(m_agcFollowFloor, 0, 'f', 1).arg(measured).arg(measuredFloor, 0, 'f', 1)
                                          .arg(slope, 0, 'f', 2).arg(current).arg(target);
        m_agcFollowTicks = 0;
        m_agcFollowMovedMs = now;
        m_agcRecalling = true;
        m_slice->recallAgcThreshold(target);
        m_agcRecalling = false;
    }
    setNote(tr("Following the noise: %1 dBm at the slice (measured %2 at %3 dBm).")
                .arg(qRound(m_agcFollowFloor)).arg(measured).arg(qRound(measuredFloor)));
}

} // namespace AetherSDR::mobile
