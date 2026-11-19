#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>

#include <memory>

namespace AetherSDR {

// The radio's SmartLink TLS connection through Apple's Network framework, for
// iOS (and the iPad app on a Mac), behind the part of QSslSocket's interface
// WanConnection uses. Qt's TLS on iOS is Secure Transport, Apple's old stack
// (TLS 1.2 at most, its own ClientHello); a mobile network that cut the
// iPad app's sessions right after the handshake (Mohammed A71AT over 5G,
// OksaSDR roadmap 99) let SmartSDR for iOS and the Android app through. This
// is Apple's current TLS, as other iOS apps use it: TLS 1.2-1.3 offered (the
// radio itself negotiates 1.2), the radio's self-signed certificate accepted
// and its DER handed to WanConnection, which pins it (TOFU).
//
// Signals as QSslSocket's: encrypted() once the TLS handshake is done (no
// separate connected()), readyRead(), errorOccurred() (errorString() says
// what), then disconnected() if the session had been up; a close asked for
// (disconnectFromHost) ends with disconnected() alone. All on the thread
// the socket lives in; the framework's own queue is internal.
class AppleTlsSocket : public QObject {
    Q_OBJECT

public:
    explicit AppleTlsSocket(QObject* parent = nullptr);
    ~AppleTlsSocket() override;

    QAbstractSocket::SocketState state() const { return m_state; }
    QHostAddress peerAddress() const { return m_peer; }
    quint16 localPort() const { return m_localPort; }

    // TLS 1.2 or 1.3 to host:port; any certificate is accepted (the caller
    // pins it, see peerCertificateDer()).
    void connectToHostEncrypted(const QString& host, quint16 port);
    // Sends what was written, then closes (disconnected() at the end).
    void disconnectFromHost();
    // Closes at once; disconnected() if the session was up.
    void abort();

    qint64 write(const QByteArray& data);
    qint64 write(const char* data) { return write(QByteArray(data)); }
    bool flush() { return true; }   // every write goes to the framework at once
    QByteArray readAll();
    QString errorString() const { return m_error; }
    void ignoreSslErrors() {}       // the certificate is accepted in any case

    // The session, once encrypted(): "1.3" or "1.2", the cipher suite's
    // name, and the radio's certificate (DER) as the handshake showed it.
    QString protocolName() const { return m_protocol; }
    QString cipherName() const { return m_cipher; }
    QByteArray peerCertificateDer() const { return m_peerCert; }

signals:
    void encrypted();
    void disconnected();
    void readyRead();
    void errorOccurred(QAbstractSocket::SocketError error);

private:
    struct Impl;      // the framework's side (AppleTlsSocket.mm)
    struct Shared;    // what the framework's queue and this object share
    struct ReadyInfo;
    friend struct Impl;

    void handleReady(const ReadyInfo& info);
    void handleData(const QByteArray& data);
    void handleFailed(const QString& message, QAbstractSocket::SocketError error);
    void handleCancelled();
    void finishConnection();   // cancel and release the connection, Unconnected

    std::shared_ptr<Shared> m_shared;
    void* m_queue{nullptr};    // dispatch_queue_t
    void* m_conn{nullptr};     // nw_connection_t of the current connection (a reference of its own)
    quint64 m_connId{0};       // events of an older connection are dropped
    QAbstractSocket::SocketState m_state{QAbstractSocket::UnconnectedState};
    QHostAddress m_peer;
    quint16 m_localPort{0};
    QString m_protocol;
    QString m_cipher;
    QByteArray m_peerCert;
    QByteArray m_readBuffer;
    QString m_error;
};

} // namespace AetherSDR
