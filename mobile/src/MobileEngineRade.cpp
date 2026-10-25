// RADE (FreeDV Radio Autoencoder) digital voice in the app: the desktop's
// MainWindow::activateRADE / deactivateRADE, without its widgets. The modem
// runs here; the radio is a plain DIGU / DIGL passthrough. Receive: the
// slice's DAX channel -> RADEEngine -> decoded speech to the speaker (the
// slice itself muted). Transmit: this device's microphone -> RADEEngine ->
// the radio's dax_tx stream (transmit dax=1), with the end-of-over frame
// played out before the carrier drops.

#include "MobileEngine.h"

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/DaxTxPolicy.h"
#include "core/backends/flex/PanadapterStream.h"
#include "core/PcmFrame.h"
#include "core/RADEEngine.h"
#include "gui/ModeFilterPresets.h"
#include "models/BandDefs.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

#include <QLoggingCategory>
#include <QThread>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

using namespace AetherSDR;

namespace AetherSDR::mobile {

namespace {

// RADE's own filter: 750-2250 Hz from the carrier, on either side.
bool isRadeFilter(int lo, int hi)
{
    return (std::abs(lo - 750) <= 20 && std::abs(hi - 2250) <= 20)
        || (std::abs(lo + 2250) <= 20 && std::abs(hi + 750) <= 20);
}

// The band's voice sideband: LSB below 10 MHz (60 m USB), USB above.
QString bandVoiceMode(double mhz)
{
    for (const auto& band : AetherSDR::kBands) {
        if (mhz >= band.lowMhz && mhz <= band.highMhz) {
            return QString::fromLatin1(band.defaultMode) == QLatin1String("LSB") ? QStringLiteral("LSB")
                                                                               : QStringLiteral("USB");
        }
    }
    return mhz < 10.0 ? QStringLiteral("LSB") : QStringLiteral("USB");
}

// Set while RADE has the slice muted; cleared once the unmute reaches the
// radio. Still set on the next connect (the app disconnected, closed or was
// killed while RADE ran), the radio kept the slice muted: healRadeMute().
const QString kRadeMutedKey = QStringLiteral("MobileRadeMuted");

} // namespace

bool MobileEngine::radeAvailable() const
{
#ifdef HAVE_RADE
    return true;
#else
    return false;
#endif
}

bool MobileEngine::radeActive() const
{
    return m_radeEngine != nullptr && m_radeSliceId >= 0;
}

QVariantMap MobileEngine::radeStatus() const
{
    return {{QStringLiteral("active"), radeActive()},
            {QStringLiteral("synced"), m_radeSynced},
            {QStringLiteral("snr"), qRound(m_radeSnr)},
            {QStringLiteral("callsign"), m_radeRxCallsign}};
}

void MobileEngine::setRadeActive(bool on)
{
    if (on == radeActive()) {
        return;
    }
    if (on) {
        activateRade();
    } else {
        deactivateRade();
    }
    emit radeChanged();
}

void MobileEngine::activateRade()
{
#ifdef HAVE_RADE
    RadioModel& rm = m_session.radioModel();
    SliceModel* s = m_slice.data();
    if (!s || !m_connected) {
        return;
    }
    // The modem receives on a DAX channel, which only a Flex offers.
    if (!rm.panStream()) {
        setLastError(tr("RADE needs DAX audio, which this radio does not provide."));
        return;
    }
    if (digiRunning()) {
        setDigiRunning(false);
    }
    const int sliceId = s->sliceId();
    if (!s->isTxSlice()) {
        s->setTxSlice(true);
    }
    // DIGU, or DIGL where the band's voice mode is LSB (60 m is USB).
    QString mode = QStringLiteral("DIGU");
    for (const auto& band : AetherSDR::kBands) {
        if (s->frequency() >= band.lowMhz && s->frequency() <= band.highMhz) {
            mode = QString::fromLatin1(band.defaultMode) == QLatin1String("LSB") ? QStringLiteral("DIGL") : QStringLiteral("DIGU");
            break;
        }
    }
    // What RADE ends back to. A slice still in RADE's own DIGU / DIGL filter
    // (RADE off and on again at once, as a tap in the FreeDV list does, the
    // radio's answer not in yet) is not the user's: the state saved before
    // stays (it mixed RADE's own in, and a band was left muted or in DIGU).
    if (!((s->mode() == QLatin1String("DIGU") || s->mode() == QLatin1String("DIGL"))
          && isRadeFilter(s->filterLow(), s->filterHigh()))) {
        m_radePrevMode = s->mode();
        m_radePrevFilterLow = s->filterLow();
        m_radePrevFilterHigh = s->filterHigh();
    }
    armModeAgc();
    s->setMode(mode);
    if (mode == QLatin1String("DIGL")) {
        s->setFilterWidth(-2250, -750);
    } else {
        s->setFilterWidth(750, 2250);
    }
    // The radio answers a mode change with that mode's remembered filter,
    // which can land after ours (from LSB / USB the slice stayed at 3 kHz):
    // RADE's filter again once the mode is in.
    QTimer::singleShot(1000, this, [this, sliceId] {
        SliceModel* sl = m_session.radioModel().slice(sliceId);
        if (!sl || !radeActive() || m_radeSliceId != sliceId || isRadeFilter(sl->filterLow(), sl->filterHigh())) {
            return;
        }
        if (sl->mode() == QLatin1String("DIGL")) {
            sl->setFilterWidth(-2250, -750);
        } else if (sl->mode() == QLatin1String("DIGU")) {
            sl->setFilterWidth(750, 2250);
        } else {
            return;
        }
        qCInfo(lcMobile) << "RADE: filter set again after the mode change";
    });
    m_radeSliceId = sliceId;
    AppSettings::instance().setValue(kRadeMutedKey, QStringLiteral("1"));
    AppSettings::instance().save();
    s->setAudioMute(true);
    // A DAX channel for the modem's receive audio.
    m_radeAssignedDax = false;
    if (s->daxChannel() < 1 || s->daxChannel() > 8) {
        s->setDaxChannel(1);
        m_radeAssignedDax = true;
    }

    m_radeEngine = new RADEEngine;
    m_radeThread = new QThread(this);
    m_radeThread->setObjectName(QStringLiteral("RADEEngine"));
    m_radeEngine->moveToThread(m_radeThread);
    connect(m_radeThread, &QThread::finished, m_radeEngine, &QObject::deleteLater);
    m_radeThread->start(QThread::HighPriority);
    bool ok = false;
    RADEEngine* engine = m_radeEngine;
    QMetaObject::invokeMethod(engine, [engine, &ok] { ok = engine->start(); }, Qt::BlockingQueuedConnection);
    if (!ok) {
        qCWarning(lcMobile) << "RADE: the engine did not start";
        deactivateRade();
        setLastError(tr("RADE could not start."));
        return;
    }
    rm.setDigitalVoiceTxSlice(sliceId);
    const QString callsign = rm.callsign().trimmed().toUpper();
    if (!callsign.isEmpty()) {
        QMetaObject::invokeMethod(engine, [engine, callsign] { engine->setTxCallsign(callsign); }, Qt::QueuedConnection);
    }
    // Modem audio goes out on the radio's dax_tx stream, which the radio
    // modulates only with DAX transmit on.
    rm.ensureDaxTxStream(DaxTxRequestReason::RadeModemTx);
    m_radePrevDax = rm.transmitModel().daxOn();
    rm.transmitModel().setDax(true);
    m_audio->setRadeMode(true);

    // Transmit: microphone -> encoder -> dax_tx.
    connect(m_audio, &AudioEngine::txRawPcmReady, engine, &RADEEngine::feedTxAudio, Qt::QueuedConnection);
    connect(engine, &RADEEngine::txModemReady, m_audio, &AudioEngine::sendModemTxAudio, Qt::QueuedConnection);

    // End of over: the radio stays keyed until the EOO frame has played out.
    rm.transmitModel().setPttOffHook([this](TransmitModel::PttRelease release) {
        if (m_radeEooPending && m_radePttRelease.current()) {
            return;
        }
        m_radePttRelease = release;
        const quint64 requestId = ++m_radeEooRequestId;
        m_radeEooPending = true;
        RADEEngine* e = m_radeEngine;
        QMetaObject::invokeMethod(e, [e, release, requestId] {
            if (release.current()) {
                e->setEooRequested(true, requestId);
            }
        }, Qt::QueuedConnection);
    });
    connect(engine, &RADEEngine::eooFinished, this, [this](quint64 requestId) {
        if (requestId != m_radeEooRequestId) {
            return;
        }
        const TransmitModel::PttRelease release = m_radePttRelease;
        if (!m_radeEooPending || !release.current()) {
            return;
        }
        m_radeEooPending = false;
        m_radeTxActive = false;
        QMetaObject::invokeMethod(m_audio, [audio = m_audio, release] {
            if (release.current()) {
                audio->setTransmitting(false);
            }
        }, Qt::QueuedConnection);
        constexpr int kEooPlaybackMs =
            RADEEngine::kEooFrameMs + RADEEngine::kEooSilenceTailMs + RADEEngine::kEooTransportMarginMs;
        QTimer::singleShot(kEooPlaybackMs, this, [release] { release.release(); });
    });
    const TxCoordinator::Producer producer = rm.registerTxProducer(engine);
    const auto beginOver = [this, producer] {
        if (!m_radeEngine || !m_radeEngine->isActive() || (m_radeTxActive && !m_radeEooPending)) {
            return;
        }
        ++m_radeEooRequestId;
        if (m_radeFallbackFence) {
            m_radeFallbackFence->store(false, std::memory_order_release);
        }
        m_radeEooPending = false;
        m_radeTxActive = true;
        const TxCoordinator::Context context = m_session.radioModel().captureTxMedia(producer);
        RADEEngine* e = m_radeEngine;
        QMetaObject::invokeMethod(e, [e, context] {
            if (context.permitsDispatch(TxCoordinator::monotonicMs())) {
                e->resetTx(context);
            }
        }, Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_audio, [audio = m_audio, context] { audio->setRawMicrophoneContext(context); },
                                  Qt::QueuedConnection);
    };
    m_radeConnections.append(connect(&rm, &RadioModel::localTransmitEngaged, this, beginOver));
    m_radeConnections.append(connect(&rm.transmitModel(), &TransmitModel::moxChanged, this, [this, beginOver](bool tx) {
        if (!m_radeEngine || !m_radeEngine->isActive()) {
            return;
        }
        // The audio gate opens with MOX; it closes after the EOO (eooFinished).
        if (tx) {
            m_audio->setTransmitting(true);
            beginOver();
        } else if (!m_radeEooPending && m_radeTxActive) {
            // The radio unkeyed without our release: finish the encoder only.
            m_radeEooPending = true;
            m_radeFallbackFence = std::make_shared<std::atomic<bool>>(true);
            const std::shared_ptr<std::atomic<bool>> fence = m_radeFallbackFence;
            m_radePttRelease = {[fence] { return fence->load(std::memory_order_acquire); }, {}, {}};
            const quint64 requestId = ++m_radeEooRequestId;
            RADEEngine* e = m_radeEngine;
            QMetaObject::invokeMethod(e, [e, fence, requestId] {
                if (fence->load(std::memory_order_acquire)) {
                    e->setEooRequested(true, requestId);
                }
            }, Qt::QueuedConnection);
        }
    }));
    m_radeConnections.append(connect(&rm, &RadioModel::txAudioGateChanged, this, [this](bool tx) {
        if (!tx && !(m_radeEooPending && m_radePttRelease.current())) {
            m_audio->setTransmitting(false);
        }
    }));
    m_radeConnections.append(connect(&rm.transmitModel(), &TransmitModel::pttReleaseCancelled, this, [this] {
        ++m_radeEooRequestId;
        m_radePttRelease = {};
        m_radeEooPending = false;
        m_radeTxActive = false;
        if (RADEEngine* e = m_radeEngine) {
            QMetaObject::invokeMethod(e, [e] { e->resetTx(); }, Qt::QueuedConnection);
        }
        m_audio->setTransmitting(false);
    }));

    // Receive: the slice's DAX channel (looked up live) -> decoder -> speaker.
    connect(rm.panStream(), &PanadapterStream::daxPcmReady, engine, [this, engine, sliceId](int channel, const PcmFrame& frame) {
        const QByteArray pcm = frame.legacyStereo24();
        if (pcm.isEmpty()) {
            return;
        }
        SliceModel* sl = m_session.radioModel().slice(sliceId);
        if (sl && channel == sl->daxChannel()) {
            engine->feedRxAudio(channel, pcm);
        }
    }, Qt::QueuedConnection);
    connect(engine, &RADEEngine::rxSpeechReady, m_audio, &AudioEngine::feedDecodedSpeech, Qt::QueuedConnection);
    m_radeDaxChannel = s->daxChannel();
    rm.acquireDaxChannel(m_radeDaxChannel, PanadapterStream::DaxConsumer::Rade);
    m_radeConnections.append(connect(s, &SliceModel::daxChannelChanged, this, [this](int ch) {
        PanadapterStream* ps = m_session.radioModel().panStream();
        if (!ps) {
            return;
        }
        const int old = m_radeDaxChannel;
        m_radeDaxChannel = ch >= 1 && ch <= 8 ? ch : 0;
        if (m_radeDaxChannel) {
            ps->acquireDaxChannel(m_radeDaxChannel, PanadapterStream::DaxConsumer::Rade);
        }
        if (old >= 1 && old <= 8 && old != m_radeDaxChannel) {
            ps->releaseDaxChannel(old, PanadapterStream::DaxConsumer::Rade);
        }
    }));
    // A mode change from anywhere (profile, another client) ends RADE.
    m_radeConnections.append(connect(s, &SliceModel::modeChanged, this, [this](const QString& m) {
        if (m != QLatin1String("DIGU") && m != QLatin1String("DIGL")) {
            QTimer::singleShot(0, this, [this] { setRadeActive(false); });
        }
    }));

    m_radeSynced = false;
    m_radeSnr = 0;
    m_radeRxCallsign.clear();
    connect(engine, &RADEEngine::syncChanged, this, [this](bool synced) {
        m_radeSynced = synced;
        emit radeChanged();
    });
    connect(engine, &RADEEngine::snrChanged, this, [this](float snr) {
        m_radeSnr = snr;
        emit radeChanged();
    });
    connect(engine, &RADEEngine::eooCallsignReceived, this, [this](const QString& call) {
        m_radeRxCallsign = call;
        qCInfo(lcMobile) << "RADE: end of over from" << call;
        emit radeChanged();
    });
    qCInfo(lcMobile) << "RADE: on, slice" << sliceId << mode << "DAX channel" << m_radeDaxChannel << RADEEngine::versionString();
#endif
}

// The slice out of RADE's DIGU / DIGL: back to what it was before (a voice
// mode, or a digital mode with its own filter); else, or after a move to
// another band, the band's voice sideband with a 2.7 kHz filter. A mode the
// user chose meanwhile (out of DIGU / DIGL) stays.
void MobileEngine::restoreAfterRade(SliceModel* s)
{
    if (s->mode() != QLatin1String("DIGU") && s->mode() != QLatin1String("DIGL")) {
        return;
    }
    QString mode = m_radePrevMode;
    int lo = m_radePrevFilterLow;
    int hi = m_radePrevFilterHigh;
    const bool prevDigital = mode.isEmpty() || mode == QLatin1String("DIGU") || mode == QLatin1String("DIGL");
    if (prevDigital && !mode.isEmpty() && mode == s->mode() && !isRadeFilter(lo, hi)) {
        s->setFilterWidth(lo, hi);
    } else {
        // SSB follows the band (a list tap may have changed it under RADE).
        if (prevDigital || mode == QLatin1String("LSB") || mode == QLatin1String("USB")) {
            mode = bandVoiceMode(s->frequency());
        }
        const bool fits = !prevDigital && !isRadeFilter(lo, hi)
                          && (mode == QLatin1String("LSB")   ? (hi <= 0 && lo < hi)
                              : mode == QLatin1String("USB") ? (lo >= 0 && hi > lo)
                                                             : true);
        if (!fits) {
            const ModeFilters::Edges e = ModeFilters::edgesForWidth(mode, 2700, ModeFilters::SliceContext{});
            lo = e.lo;
            hi = e.hi;
        }
        armModeAgc();
        s->setMode(mode);
        s->setFilterWidth(lo, hi);
    }
    qCInfo(lcMobile).noquote() << QStringLiteral("RADE: off; slice %1 back to %2, %3..%4 Hz, unmuted")
                                      .arg(s->letter(), mode).arg(lo).arg(hi);
}

// A slice RADE muted when the app last went away (disconnected, closed or
// killed while RADE ran): the radio kept it muted. Unmuted on the connect.
void MobileEngine::healRadeMute()
{
    AppSettings& settings = AppSettings::instance();
    if (settings.value(kRadeMutedKey).toString() != QLatin1String("1") || !m_connected || radeActive()) {
        return;
    }
    settings.setValue(kRadeMutedKey, QStringLiteral("0"));
    settings.save();
    if (!m_slice || m_slice->sliceId() == m_splitTxId) {
        return;
    }
    if (m_slice->audioMute()) {
        m_slice->setAudioMute(false);
        qCInfo(lcMobile).noquote() << "RADE had muted slice" << m_slice->letter()
                                   << "when the app last went away: unmuted";
    }
    // Still in RADE's DIGU / DIGL filter: out of it, as RADE off does.
    if ((m_slice->mode() == QLatin1String("DIGU") || m_slice->mode() == QLatin1String("DIGL"))
        && isRadeFilter(m_slice->filterLow(), m_slice->filterHigh())) {
        m_radePrevMode.clear();
        restoreAfterRade(m_slice.data());
    }
}

void MobileEngine::deactivateRade()
{
#ifdef HAVE_RADE
    RadioModel& rm = m_session.radioModel();
    for (const QMetaObject::Connection& c : std::as_const(m_radeConnections)) {
        disconnect(c);
    }
    m_radeConnections.clear();
    if (m_radeSliceId >= 0) {
        if (SliceModel* s = rm.slice(m_radeSliceId)) {
            // RADE muted the slice: heard again (a split's TX slice stays
            // muted). Not the state before RADE: read back at once after a
            // quick RADE off and on, that was RADE's own mute.
            if (s->sliceId() != m_splitTxId) {
                s->setAudioMute(false);
            }
            if (rm.isConnected()) {
                AppSettings::instance().setValue(kRadeMutedKey, QStringLiteral("0"));
                AppSettings::instance().save();
            }
            if (m_radeAssignedDax) {
                s->setDaxChannel(0);
            }
            restoreAfterRade(s);
        }
    }
    m_radeSliceId = -1;
    rm.transmitModel().clearPttOffHook();
    const TransmitModel::PttRelease pending = m_radePttRelease;
    m_radeEooPending = false;
    m_radeTxActive = false;
    pending.release();
    ++m_radeEooRequestId;
    if (m_radeFallbackFence) {
        m_radeFallbackFence->store(false, std::memory_order_release);
    }
    rm.transmitModel().invalidatePttRelease();
    m_radePttRelease = {};
    if (m_audio) {
        m_audio->setRadeMode(false);
        m_audio->setTransmitting(false);
        m_audio->clearTxAccumulators();
    }
    rm.setDigitalVoiceTxSlice(-1);
    if (rm.transmitModel().daxOn() != m_radePrevDax) {
        rm.transmitModel().setDax(m_radePrevDax);
    }
    if (m_radeEngine) {
        disconnect(m_audio, &AudioEngine::txRawPcmReady, m_radeEngine, nullptr);
        disconnect(m_radeEngine, nullptr, this, nullptr);
        disconnect(m_radeEngine, nullptr, m_audio, nullptr);
        if (rm.panStream()) {
            disconnect(rm.panStream(), &PanadapterStream::daxPcmReady, m_radeEngine, nullptr);
        }
        if (m_radeDaxChannel >= 1 && m_radeDaxChannel <= 8) {
            rm.releaseDaxChannel(m_radeDaxChannel, PanadapterStream::DaxConsumer::Rade);
        }
        m_radeDaxChannel = 0;
        QMetaObject::invokeMethod(m_radeEngine, &RADEEngine::stop, Qt::BlockingQueuedConnection);
        m_radeEngine = nullptr;  // deleted with its thread
    }
    if (m_radeThread) {
        m_radeThread->quit();
        m_radeThread->wait(2000);
        m_radeThread->deleteLater();
        m_radeThread = nullptr;
    }
    m_radeSynced = false;
    qCInfo(lcMobile) << "RADE: off";
#endif
}

}  // namespace AetherSDR::mobile
