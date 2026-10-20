#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

namespace AetherSDR::mobile {

// The app's own QSO log: one ADIF file in the app's data folder
// (oksasdr-log.adi), appended to as contacts are logged, read back at start.
// Each record is a map of ADIF field names (upper case) to values, e.g.
// CALL, QSO_DATE, TIME_ON, FREQ, BAND, MODE, SUBMODE, RST_SENT, RST_RCVD,
// TX_PWR, NAME, QTH, GRIDSQUARE, COUNTRY, CONT, CQZ, STATION_CALLSIGN,
// MY_GRIDSQUARE, COMMENT, SIG, SIG_INFO, MY_SIG, MY_SIG_INFO.
class QsoLog {
public:
    explicit QsoLog(QString path = QString());

    const QList<QVariantMap>& records() const { return m_records; }
    QString path() const { return m_path; }

    // Appends one record to the file (writing the header first for a new
    // file) and to the list; false when the file cannot be written.
    bool append(const QVariantMap& record);

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

    QString m_path;
    QList<QVariantMap> m_records;
};

} // namespace AetherSDR::mobile
