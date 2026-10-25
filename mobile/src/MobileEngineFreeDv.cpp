// FreeDV Reporter (qso.freedv.org) for the app's RADE, on the FreeDV tab:
// the stations the server lists (fetched while the tab shows), and, while
// RADE runs and the user turned it on, this station reporting itself as the
// desktop does (MainWindow::startFreeDvReporting): callsign, grid,
// frequency, transmit, and the callsign and SNR RADE hears.

#include "MobileEngine.h"

#include "core/AppSettings.h"
#include "core/MaidenheadLocator.h"
#include "models/BandSettings.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#ifdef HAVE_WEBSOCKETS
#include "core/FreeDvClient.h"
#endif

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>
#include <vector>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

void MobileEngine::setupFreeDv()
{
    const AppSettings& settings = AppSettings::instance();
    m_freedvReporterOn = settings.value(QStringLiteral("MobileFreeDvReporter"), QStringLiteral("False")).toString()
                         == QLatin1String("True");
    m_freedvAllBands = settings.value(QStringLiteral("MobileFreeDvAllBands"), QStringLiteral("False")).toString()
                       == QLatin1String("True");
    m_freedvMessage = settings.value(QStringLiteral("MobileFreeDvMessage")).toString();
    m_freedvRebuildTimer.setSingleShot(true);
    m_freedvRebuildTimer.setInterval(500);
    connect(&m_freedvRebuildTimer, &QTimer::timeout, this, &MobileEngine::rebuildFreedvStations);
    // "TX 3 min ago" moves on while the list is shown.
    m_freedvAgeTimer.setInterval(30000);
    connect(&m_freedvAgeTimer, &QTimer::timeout, this, [this] {
        if (m_freedvViewers > 0) {
            rebuildFreedvStations();
        }
    });
    m_freedvAgeTimer.start();
    m_freedvFreqTimer.setSingleShot(true);
    m_freedvFreqTimer.setInterval(1000);
#ifdef HAVE_WEBSOCKETS
    m_freedv = new FreeDvClient(this);
    m_freedv->initialize();
    // Coalesced: a busy server sends many updates a second.
    const auto scheduleRebuild = [this] {
        if (!m_freedvRebuildTimer.isActive()) {
            m_freedvRebuildTimer.start();
        }
    };
    connect(m_freedv, &FreeDvClient::stationUpdated, this, scheduleRebuild);
    connect(m_freedv, &FreeDvClient::stationRemoved, this, scheduleRebuild);
    connect(m_freedv, &FreeDvClient::stationsCleared, this, scheduleRebuild);
    connect(m_freedv, &FreeDvClient::rawLineReceived, this, [this](const QString& line) {
        if (line.startsWith(QLatin1String("Connected to"))) {
            m_freedvError.clear();
            qCInfo(lcMobile).noquote() << "FreeDV Reporter:" << line << (m_freedvReporting ? "(reporting)" : "(viewing)");
            updateFreeDv();
        }
    });
    connect(m_freedv, &FreeDvClient::connectionError, this, [this](const QString& error) {
        m_freedvError = error;
        qCInfo(lcMobile) << "FreeDV Reporter:" << error;
        updateFreeDv();
    });
    connect(&m_freedvFreqTimer, &QTimer::timeout, this, [this] {
        if (SliceModel* s = m_session.radioModel().slice(m_radeSliceId); s && m_freedvReporting) {
            m_freedv->reportFreqChange(s->frequency());
        }
    });
#endif
    // This band's stations follow the slice; RADE's state drives reporting.
    connect(this, &MobileEngine::frequencyChanged, this, [this] {
        if (!m_freedvAllBands && m_freedvStarted && !m_freedvRebuildTimer.isActive()) {
            m_freedvRebuildTimer.start();
        }
    });
    connect(this, &MobileEngine::radeChanged, this, &MobileEngine::updateFreeDv);
    connect(this, &MobileEngine::connectionStateChanged, this, &MobileEngine::updateFreeDv);
}

void MobileEngine::setFreedvReporter(bool on)
{
    if (on == m_freedvReporterOn) {
        return;
    }
    m_freedvReporterOn = on;
    AppSettings::instance().setValue(QStringLiteral("MobileFreeDvReporter"), on ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    updateFreeDv();
    emit freedvChanged();
}

void MobileEngine::setFreedvMessage(const QString& message)
{
    const QString text = message.trimmed().left(80);
    if (text == m_freedvMessage) {
        return;
    }
    m_freedvMessage = text;
    AppSettings::instance().setValue(QStringLiteral("MobileFreeDvMessage"), text);
    AppSettings::instance().save();
#ifdef HAVE_WEBSOCKETS
    if (m_freedv) {
        m_freedv->updateMessage(text);
    }
#endif
    emit freedvChanged();
}

void MobileEngine::setFreedvAllBands(bool all)
{
    if (all == m_freedvAllBands) {
        return;
    }
    m_freedvAllBands = all;
    AppSettings::instance().setValue(QStringLiteral("MobileFreeDvAllBands"), all ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    rebuildFreedvStations();
    emit freedvStationsChanged();
}

void MobileEngine::freedvViewer(bool shown)
{
    m_freedvViewers = std::max(0, m_freedvViewers + (shown ? 1 : -1));
    updateFreeDv();
}

bool MobileEngine::freedvLive() const
{
#ifdef HAVE_WEBSOCKETS
    return m_freedvReporting && m_freedv && m_freedv->isConnected();
#else
    return false;
#endif
}

QString MobileEngine::freedvStatus() const
{
#ifndef HAVE_WEBSOCKETS
    return tr("Not in this build.");
#else
    if (!m_freedvReporterOn) {
        return m_freedvStarted && !m_freedvError.isEmpty() ? tr("Station list: %1").arg(m_freedvError) : QString();
    }
    if (digiMyCall().isEmpty()) {
        return tr("Needs the radio's callsign (Radio Setup).");
    }
    if (myGrid().size() < 4) {
        return tr("Needs your grid (Log tab › My grid, or the radio's GPS).");
    }
    if (!radeActive()) {
        return tr("Reports while RADE is on.");
    }
    if (!m_freedvError.isEmpty() && !m_freedv->isConnected()) {
        return tr("Not connected: %1").arg(m_freedvError);
    }
    if (!m_freedv->isConnected()) {
        return tr("Connecting to qso.freedv.org…");
    }
    const SliceModel* s = m_session.radioModel().slice(m_radeSliceId);
    return tr("Reporting as %1 (%2) on %3 MHz.")
        .arg(digiMyCall(), myGrid(), s ? QString::number(s->frequency(), 'f', 3) : QStringLiteral("?"));
#endif
}

void MobileEngine::updateFreeDv()
{
#ifdef HAVE_WEBSOCKETS
    if (!m_freedv) {
        return;
    }
    RadioModel& rm = m_session.radioModel();
    SliceModel* radeSlice = rm.slice(m_radeSliceId);
    const QString call = digiMyCall();
    const QString grid = myGrid();
    const bool report = m_freedvReporterOn && radeActive() && m_connected && radeSlice && !call.isEmpty()
                        && grid.size() >= 4;
    const bool wanted = m_freedvViewers > 0 || report;

    if (report != m_freedvReporting) {
        m_freedvReporting = report;
        disconnect(m_freedvMoxConnection);
        disconnect(m_freedvFreqConnection);
        m_freedvLastRxCall.clear();
        if (report) {
            m_freedv->enableReporting(call, grid, m_freedvMessage, QStringLiteral("OksaSDR %1").arg(appVersion()),
                                      radeSlice->frequency());
            // Voice transmit only: not a tune or an ATU cycle (as the desktop).
            m_freedvMoxConnection = connect(&rm.transmitModel(), &TransmitModel::moxChanged, this, [this](bool tx) {
                const TransmitModel& t = m_session.radioModel().transmitModel();
                if (t.isTuning() || t.atuStatus() == ATUStatus::InProgress) {
                    return;
                }
                m_freedv->reportTxState(tx);
            });
            m_freedvFreqConnection = connect(radeSlice, &SliceModel::frequencyChanged, this, [this] { m_freedvFreqTimer.start(); });
            qCInfo(lcMobile).noquote() << "FreeDV Reporter: reporting as" << call << grid;
        } else {
            m_freedvFreqTimer.stop();
            m_freedv->disableReporting();
            qCInfo(lcMobile) << "FreeDV Reporter: not reporting";
        }
    }
    if (wanted && !m_freedvStarted) {
        m_freedvStarted = true;
        m_freedvError.clear();
        m_freedv->startConnection();
    } else if (!wanted && m_freedvStarted) {
        m_freedvStarted = false;
        m_freedv->stopConnection();
        m_freedvRebuildTimer.stop();
        rebuildFreedvStations();
    }
    // What RADE hears, for the rx_report (SNR each second while synced, the
    // call at once at its end of over).
    if (m_freedvReporting) {
        m_freedv->updateRxSnr(m_radeSnr);
        m_freedv->updateRxSynced(m_radeSynced);
        if (!m_radeSynced) {
            m_freedvLastRxCall.clear();
        } else if (!m_radeRxCallsign.isEmpty() && m_radeRxCallsign != m_freedvLastRxCall) {
            m_freedvLastRxCall = m_radeRxCallsign;
            m_freedv->updateRxCallsign(m_radeRxCallsign);
        }
    }
#endif
    const QString status = freedvStatus() + (freedvLive() ? QStringLiteral("\x01") : QString());
    if (status != m_freedvLastStatus) {
        m_freedvLastStatus = status;
        emit freedvChanged();
    }
}

void MobileEngine::rebuildFreedvStations()
{
    QVariantList list;
#ifdef HAVE_WEBSOCKETS
    if (m_freedv && m_freedvStarted) {
        const QString band = m_slice ? BandSettings::bandForFrequency(m_slice->frequency()) : QString();
        double myLat = 0.0;
        double myLon = 0.0;
        const bool located = MaidenheadLocator::toLatLon(myGrid(), myLat, myLon);
        const QString me = digiMyCall();
        // Sorted for listening: transmitting now first, then the most
        // recently on the air, then the others by frequency (a tester
        // wanted to go and listen while a station is active).
        struct Row {
            bool tx;
            qint64 ago;  // seconds since the last transmission, -1 none
            double mhz;
            QVariantMap map;
        };
        std::vector<Row> rows;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QHash<QString, FreeDvClient::StationInfo> stations = m_freedv->stations();
        for (auto it = stations.cbegin(); it != stations.cend(); ++it) {
            const FreeDvClient::StationInfo& st = it.value();
            // On the air: a callsign and a frequency (0 is leaving).
            if (st.callsign.isEmpty() || st.freqMhz <= 0.0) {
                continue;
            }
            if (!m_freedvAllBands && !band.isEmpty() && BandSettings::bandForFrequency(st.freqMhz) != band) {
                continue;
            }
            double km = -1.0;
            double lat = 0.0;
            double lon = 0.0;
            if (located && MaidenheadLocator::toLatLon(st.gridSquare, lat, lon)) {
                km = MaidenheadLocator::distanceKm(myLat, myLon, lat, lon);
            }
            const bool heard = !st.rxCallsign.isEmpty() && st.snr > -99.0f;
            const bool tx = st.status == QLatin1String("TX");
            const qint64 ago = st.lastTxTime.isValid() ? std::max<qint64>(0, st.lastTxTime.secsTo(now)) : -1;
            rows.push_back({tx, ago, st.freqMhz,
                            {
                                {QStringLiteral("txAgoMin"), !tx && ago >= 0 ? QVariant(int(ago / 60)) : QVariant()},
                                {QStringLiteral("callsign"), st.callsign},
                                {QStringLiteral("grid"), st.gridSquare},
                                {QStringLiteral("km"), km >= 0.0 ? QVariant(int(std::lround(km))) : QVariant()},
                                {QStringLiteral("mhz"), st.freqMhz},
                                {QStringLiteral("mode"), st.mode},
                                {QStringLiteral("tx"), tx},
                                {QStringLiteral("rxOnly"), st.rxOnly},
                                {QStringLiteral("heard"), heard ? st.rxCallsign : QString()},
                                {QStringLiteral("snr"), heard ? QVariant(int(std::lround(st.snr))) : QVariant()},
                                {QStringLiteral("message"), st.message},
                                {QStringLiteral("you"), !me.isEmpty() && st.callsign.compare(me, Qt::CaseInsensitive) == 0},
                            }});
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.tx != b.tx) {
                return a.tx;
            }
            if ((a.ago >= 0) != (b.ago >= 0)) {
                return a.ago >= 0;
            }
            if (a.ago >= 0 && a.ago != b.ago) {
                return a.ago < b.ago;
            }
            if (a.mhz != b.mhz) {
                return a.mhz < b.mhz;
            }
            return a.map.value(QStringLiteral("callsign")).toString() < b.map.value(QStringLiteral("callsign")).toString();
        });
        for (const Row& row : rows) {
            list.append(row.map);
        }
    }
#endif
    if (list != m_freedvStations) {
        m_freedvStations = list;
        emit freedvStationsChanged();
    }
}

void MobileEngine::freedvTune(double mhz, const QString& mode)
{
    if (!m_slice || !m_connected || mhz <= 0.0) {
        return;
    }
    qCInfo(lcMobile) << "FreeDV Reporter: tune to" << mhz << mode;
    tuneTo(mhz);
    // RADE stations (mode RADEV1): RADE on, once the slice is there (its
    // band decides DIGU or DIGL).
    if (mode.startsWith(QLatin1String("RADE"), Qt::CaseInsensitive)) {
        if (radeAvailable() && !radeActive()) {
            QTimer::singleShot(400, this, [this] { setRadeActive(true); });
        }
        return;
    }
    // Any other station (an SSB one, LSB / USB in the list): RADE off, or
    // its mute kept the station unheard (a tester tapped an LSB station on
    // 80 m and heard nothing); SSB in the station's sideband.
    if (radeActive()) {
        setRadeActive(false);
    }
    const QString m = mode.trimmed().toUpper();
    if (m == QLatin1String("LSB") || m == QLatin1String("USB")) {
        QTimer::singleShot(400, this, [this, m] {
            if (m_slice && !radeActive() && m_slice->mode() != m) {
                setMode(m);
            }
        });
    }
}

} // namespace AetherSDR::mobile
