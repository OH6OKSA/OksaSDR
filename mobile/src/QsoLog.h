#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QVariantMap>

#include <functional>

namespace AetherSDR::mobile {

// The app's own QSO log: one ADIF file (oksasdr-log.adi), appended to as
// contacts are logged, read back at start. Where it lives is the caller's
// choice (defaultPath(): a Documents folder the operator can reach), and a
// mirror copy can be kept somewhere else (Android's shared Documents).
// Each record is a map of ADIF field names (upper case) to values, e.g.
// CALL, QSO_DATE, TIME_ON, FREQ, BAND, MODE, SUBMODE, RST_SENT, RST_RCVD,
// TX_PWR, NAME, QTH, GRIDSQUARE, COUNTRY, CONT, CQZ, STATION_CALLSIGN,
// MY_GRIDSQUARE, COMMENT, SIG, SIG_INFO, MY_SIG, MY_SIG_INFO.
// One contact as the app keeps it in memory: what worked-before, dupes and
// the import's duplicate check need. The full record stays in the file (a
// map per contact took about 2.7 kB: a 100 000-contact logbook would have
// taken 270 MB). Band and mode strings are shared between entries.
struct QsoEntry {
    QString call;     // upper case
    QString band;     // lower case ADIF band ("20m")
    QString mode;     // upper case ADIF MODE
    QString submode;  // upper case ADIF SUBMODE
    quint32 date{0};  // QSO_DATE as yyyymmdd
    quint16 time{0};  // TIME_ON as hhmm
    quint8 cqz{0};
};

class QsoLog {
public:
    // With load false the file is read later (read() in a worker, then
    // adopt()), so a big logbook does not hold the start.
    explicit QsoLog(QString path = QString(), bool load = true);

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
    QString path() const { return m_path; }

    // What a read of the file brings: every contact's entry and the newest
    // full records (for the Log tab's list).
    struct Loaded {
        QList<QsoEntry> entries;
        QList<QVariantMap> recent;
    };
    // Reads the file at path, from any thread.
    static Loaded read(const QString& path);
    // Puts a read in the log, before the contacts appended meanwhile.
    void adopt(Loaded loaded);
    bool isLoaded() const { return m_loaded; }

    const QList<QsoEntry>& entries() const { return m_entries; }
    qsizetype count() const { return m_entries.size(); }
    // The newest full records, oldest first (at most kRecent).
    const QList<QVariantMap>& recent() const { return m_recent; }
    static constexpr qsizetype kRecent = 50;

    // Appends one record to the file (writing the header first for a new
    // file) and to the log; false when the file cannot be written.
    bool append(const QVariantMap& record);
    bool appendAll(const QList<QVariantMap>& records);
    // The file part, from any thread (one writer at a time): appends ADIF
    // text (records, each ending in <EOR>) to the file at path and copies
    // it to mirror (empty: none). An import writes in a worker, then
    // addWritten() puts the entries in the log on the log's thread.
    static bool writeText(const QString& path, const QString& mirror, const QString& adif, bool* mirrorOk = nullptr);
    void addWritten(const QList<QsoEntry>& entries, const QList<QVariantMap>& newest, bool mirrorOk);

    // Contacts with this call; and with it on this band and mode group
    // (CW, PHONE, DATA), a dupe in the usual sense.
    int workedCount(const QString& call) const;
    bool isDupe(const QString& call, const QString& band, const QString& mode) const;

    // The entry of a record; the import's "same contact" key (call, date,
    // start minute, band, mode).
    static QsoEntry entryOf(const QVariantMap& record);
    static QString dedupeKey(const QsoEntry& e);

    // ADIF text of one record ("<CALL:5>OH1AB ... <EOR>").
    static QString adifRecord(const QVariantMap& record);
    // Records of an ADIF text (header skipped); parseAdifEach hands them
    // over one at a time, so a big file is never all in memory as maps.
    static QList<QVariantMap> parseAdif(const QString& text);
    static void parseAdifEach(const QByteArray& utf8, const std::function<void(QVariantMap&&)>& each);
    // The ADIF band of a frequency in MHz ("20m", "70cm"), empty when none.
    static QString bandOf(double mhz);
    // ADIF MODE / SUBMODE of a radio mode (USB -> SSB/USB, DIGU -> DATA...).
    static QPair<QString, QString> adifMode(const QString& radioMode);
    static QString modeGroup(const QString& adifMode);

private:
    void writeMirror();
    void addEntries(const QList<QsoEntry>& entries, const QList<QVariantMap>& newest);

    QString m_path;
    QString m_mirrorPath;
    bool m_mirrorOk{false};
    bool m_loaded{false};
    QList<QsoEntry> m_entries;
    QList<QVariantMap> m_recent;
};

} // namespace AetherSDR::mobile
