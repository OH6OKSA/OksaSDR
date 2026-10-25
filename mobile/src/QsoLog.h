#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

namespace AetherSDR::mobile {

// The app's own QSO log: one ADIF file (oksasdr-log.adi), appended to as
// contacts are logged, read back at start. Where it lives is the caller's
// choice (defaultPath(): a Documents folder the operator can reach), and a
// mirror copy can be kept somewhere else (Android's shared Documents).
// Each record is a map of ADIF field names (upper case) to values, e.g.
// CALL, QSO_DATE, TIME_ON, FREQ, BAND, MODE, SUBMODE, RST_SENT, RST_RCVD,
// TX_PWR, NAME, QTH, GRIDSQUARE, COUNTRY, CONT, CQZ, STATION_CALLSIGN,
// MY_GRIDSQUARE, COMMENT, SIG, SIG_INFO, MY_SIG, MY_SIG_INFO.
class QsoLog {
public:
    explicit QsoLog(QString path = QString());

    // The log in a Documents folder the operator sees in the system's file
    // app: iOS (and the iPad app on a Mac) the app's own Documents, shown
    // in Files under On My iPhone / iPad › OksaSDR; Android the app's data
    // folder (its shared Documents copy is the mirror); desktops
    // Documents/OksaSDR. An older log in the app's data folder moves there.
    static QString defaultPath();
    // Android: Documents/OksaSDR/oksasdr-log.adi in the shared storage, kept
    // as a copy of the log; empty elsewhere.
    static QString defaultMirrorPath();

    // A copy of the whole file written here after every change (empty: none).
    void setMirrorPath(const QString& path);
    QString mirrorPath() const { return m_mirrorPath; }
    bool mirrorOk() const { return m_mirrorOk; }

    const QList<QVariantMap>& records() const { return m_records; }
    QString path() const { return m_path; }

    // Appends one record to the file (writing the header first for a new
    // file) and to the list; false when the file cannot be written.
    bool append(const QVariantMap& record);
    bool appendAll(const QList<QVariantMap>& records);
    // The file part of appendAll, from any thread (one writer at a time):
    // appends the records to the file at path and copies it to mirror
    // (empty: none). An import writes in a worker, then addWritten() puts
    // the records in the list on the log's thread.
    static bool writeRecords(const QString& path, const QString& mirror, const QList<QVariantMap>& records,
                             bool* mirrorOk = nullptr);
    void addWritten(const QList<QVariantMap>& records, bool mirrorOk);

    // Contacts with this call; and with it on this band and mode group
    // (CW, PHONE, DATA), a dupe in the usual sense.
    int workedCount(const QString& call) const;
    bool isDupe(const QString& call, const QString& band, const QString& mode) const;

    // ADIF text of one record ("<CALL:5>OH1AB ... <EOR>").
    static QString adifRecord(const QVariantMap& record);
    // Records of an ADIF text (header skipped).
    static QList<QVariantMap> parseAdif(const QString& text);
    // The ADIF band of a frequency in MHz ("20m", "70cm"), empty when none.
    static QString bandOf(double mhz);
    // ADIF MODE / SUBMODE of a radio mode (USB -> SSB/USB, DIGU -> DATA...).
    static QPair<QString, QString> adifMode(const QString& radioMode);
    static QString modeGroup(const QString& adifMode);

private:
    void load();
    void writeMirror();

    QString m_path;
    QString m_mirrorPath;
    bool m_mirrorOk{false};
    QList<QVariantMap> m_records;
};

} // namespace AetherSDR::mobile
