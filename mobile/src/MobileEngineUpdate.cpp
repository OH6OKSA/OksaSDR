// A newer Android beta (roadmap 111): the APK comes from GitHub, where no
// store tells its users of the next beta, so the app reads GitHub's latest
// release once at start and then about once a day, and offers a newer one:
// Download opens the APK in the browser and Android's own installer installs
// it over this one (the same signing key, so the settings stay). Nothing is
// downloaded or installed by the app itself.
//
// iOS, iPadOS and the Mac update through TestFlight (later the App Store),
// and a Google Play build through Play, so those are built without it
// (AETHER_MOBILE_UPDATE_CHECK, on for Android only). A Linux build gets it
// once Linux builds are release assets with their own tag.
//
// Development: OKSASDR_UPDATE_CHECK=1 turns it on in any build,
// OKSASDR_UPDATE_OWN_BETA=N stands in for the app's own beta and
// OKSASDR_UPDATE_URL for GitHub's address (a stand-in answer).

#include "MobileEngine.h"
#include "MobileWorkspace.h"

#include "core/AppSettings.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kUpdateCheckKey = QStringLiteral("MobileUpdateCheck");
const QString kUpdateDismissedKey = QStringLiteral("MobileUpdateDismissed");
const QString kUpdateCheckedKey = QStringLiteral("MobileUpdateCheckedUtc");
// The first read a while after start (the radio's connection first), then
// about once a day: at most a few of GitHub's 60 requests an hour.
constexpr int kFirstCheckMs = 15000;
constexpr qint64 kCheckIntervalSec = 24 * 3600;
// Back in front after a night: read again if the last read is this old.
constexpr qint64 kRecheckOnResumeSec = 12 * 3600;

QUrl latestReleaseUrl()
{
    const QString dev = qEnvironmentVariable("OKSASDR_UPDATE_URL");
    return QUrl(dev.isEmpty() ? QStringLiteral("https://api.github.com/repos/OH6OKSA/OksaSDR/releases/latest") : dev);
}
} // namespace

QObject* MobileEngine::workspace() const
{
    return m_workspace;
}

bool MobileEngine::updateCheckBuilt() const
{
#if defined(AETHER_MOBILE_UPDATE_CHECK)
    return true;
#else
    return qEnvironmentVariableIntValue("OKSASDR_UPDATE_CHECK") == 1;
#endif
}

int MobileEngine::ownBeta() const
{
    const int dev = qEnvironmentVariableIntValue("OKSASDR_UPDATE_OWN_BETA");
    if (dev > 0) {
        return dev;
    }
    bool ok = false;
    const int n = QStringLiteral(AETHER_MOBILE_BUILD_NUMBER).toInt(&ok);
    return ok ? n : 0;
}

// This APK's versionCode, the CI run that built it (roadmap 133); 0 where
// unknown (a desktop build, which only checks with OKSASDR_UPDATE_CHECK).
int MobileEngine::ownRun() const
{
    const int dev = qEnvironmentVariableIntValue("OKSASDR_UPDATE_OWN_RUN");
    if (dev > 0) {
        return dev;
    }
#if defined(AETHER_MOBILE_ANDROID_RUN)
    return AETHER_MOBILE_ANDROID_RUN;
#else
    return 0;
#endif
}

bool MobileEngine::updateCheck() const
{
    return AppSettings::instance().value(kUpdateCheckKey, QStringLiteral("True")).toString() == QLatin1String("True");
}

void MobileEngine::setUpdateCheck(bool on)
{
    if (on == updateCheck()) {
        return;
    }
    AppSettings::instance().setValue(kUpdateCheckKey, on ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    if (on) {
        checkForUpdate();
    } else {
        m_updateTimer.stop();
        m_updateStatus.clear();
    }
    emit updateChanged();
}

bool MobileEngine::updateOffered() const
{
    return m_updateBeta > 0
        && m_updateBeta > AppSettings::instance().value(kUpdateDismissedKey, QStringLiteral("0")).toString().toInt();
}

void MobileEngine::dismissUpdate()
{
    if (m_updateBeta <= 0) {
        return;
    }
    // Asked once per beta: the next one is offered again.
    AppSettings::instance().setValue(kUpdateDismissedKey, QString::number(m_updateBeta));
    AppSettings::instance().save();
    emit updateChanged();
}

void MobileEngine::setupUpdateCheck()
{
    if (!updateCheckBuilt()) {
        return;
    }
    m_updateTimer.setSingleShot(true);
    connect(&m_updateTimer, &QTimer::timeout, this, &MobileEngine::checkForUpdate);
    if (updateCheck()) {
        m_updateTimer.start(kFirstCheckMs);
    }
    // A phone keeps the app asleep for days: back in front, read again when
    // the last read is old.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive || !updateCheck() || m_updateTimer.isActive()) {
            return;
        }
        const qint64 last = AppSettings::instance().value(kUpdateCheckedKey, QStringLiteral("0")).toString().toLongLong();
        if (QDateTime::currentSecsSinceEpoch() - last > kRecheckOnResumeSec) {
            m_updateTimer.start(5000);
        }
    });
}

void MobileEngine::checkForUpdate()
{
    if (!updateCheckBuilt() || !updateCheck() || !m_onlineLogNet) {
        return;
    }
    QNetworkRequest req(latestReleaseUrl());
    // A plain name, nothing about the user (GitHub wants a User-Agent).
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OksaSDR"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(20000);
    QNetworkReply* reply = m_onlineLogNet->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        // The next read in a day, whatever this one gave.
        m_updateTimer.start(static_cast<int>(kCheckIntervalSec * 1000));
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            const QString why = status > 0 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString();
            qCInfo(lcMobile).noquote() << "update check: not read -" << why;
            m_updateStatus = tr("Not read (%1); tried again later.").arg(why);
            emit updateChanged();
            return;
        }
        readLatestRelease(reply->readAll());
    });
}

void MobileEngine::readLatestRelease(const QByteArray& body)
{
    AppSettings::instance().setValue(kUpdateCheckedKey, QString::number(QDateTime::currentSecsSinceEpoch()));
    AppSettings::instance().save();
    const QJsonObject release = QJsonDocument::fromJson(body).object();
    static const QRegularExpression tagRe(QStringLiteral("^android-beta-(\\d+)$"));
    const QRegularExpressionMatch m = tagRe.match(release.value(QStringLiteral("tag_name")).toString());
    const QString checked = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
    if (!m.hasMatch()) {
        // Not an Android beta (a release of another kind on top): nothing to
        // offer, nothing wrong.
        qCInfo(lcMobile).noquote() << "update check: the latest release is not an Android beta -"
                                   << release.value(QStringLiteral("tag_name")).toString();
        m_updateStatus = tr("Beta %1; checked %2.").arg(ownBeta()).arg(checked);
        emit updateChanged();
        return;
    }
    const int latest = m.captured(1).toInt();
    QString apk;
    for (const QJsonValue& v : release.value(QStringLiteral("assets")).toArray()) {
        const QString url = v.toObject().value(QStringLiteral("browser_download_url")).toString();
        if (url.endsWith(QLatin1String(".apk"))) {
            apk = url;
            break;
        }
    }
    const QString page = release.value(QStringLiteral("html_url")).toString();
    const int own = ownBeta();
    // The release's build: the CI writes "android_run: N" (an HTML comment,
    // unseen on the page) into its notes from beta 113 on (roadmap 133). A
    // preview's APK has the beta number of the release that follows it, so
    // only the run tells the released build is newer.
    static const QRegularExpression runRe(QStringLiteral("android_run:\\s*(\\d+)"));
    const QRegularExpressionMatch rm = runRe.match(release.value(QStringLiteral("body")).toString());
    const int latestRun = rm.hasMatch() ? rm.captured(1).toInt() : 0;
    const int myRun = ownRun();
    qCInfo(lcMobile).noquote() << "update check: latest Android beta" << latest << ", this is beta" << own
                               << QStringLiteral("(run %1, this run %2)").arg(latestRun).arg(myRun)
                               << (apk.isEmpty() ? "(no APK listed)" : "");
    const bool newerBeta = latest > own && own > 0;
    const bool newerBuild = latest == own && own > 0 && latestRun > 0 && myRun > 0 && latestRun > myRun;
    if ((newerBeta || newerBuild) && !apk.isEmpty()) {
        m_updateBeta = latest;
        m_updateSameBeta = newerBuild;
        m_updateApkUrl = apk;
        m_updateNotesUrl = page;
        m_updateStatus = newerBuild
                             ? tr("A newer build of beta %1 is out (this one is an earlier build); checked %2.").arg(latest).arg(checked)
                             : tr("Beta %1 is out (this is beta %2); checked %3.").arg(latest).arg(own).arg(checked);
    } else {
        m_updateBeta = 0;
        m_updateSameBeta = false;
        m_updateApkUrl.clear();
        m_updateNotesUrl.clear();
        m_updateStatus = tr("Beta %1 is the newest; checked %2.").arg(own).arg(checked);
    }
    qCInfo(lcMobile).noquote() << "update check:"
                               << (m_updateBeta == 0 ? "nothing to offer" : m_updateSameBeta ? "offered, a newer build of this beta" : "offered, a newer beta");
    emit updateChanged();
}

} // namespace AetherSDR::mobile
