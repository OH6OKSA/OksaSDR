// Not ARC: the framework's objects are released by hand (nw_release,
// sec_release, dispatch_release), as the project's other .mm files do.
#include "AppleTlsSocket.h"
#include "core/LogManager.h"

#include <QMetaObject>
#include <QTimer>

#include <cerrno>
#include <mutex>
#include <utility>

#import <Foundation/Foundation.h>
#import <Network/Network.h>
#import <Security/Security.h>

namespace AetherSDR {

struct AppleTlsSocket::Shared {
    std::mutex mutex;
    AppleTlsSocket* owner{nullptr};   // null once the socket is gone
    QByteArray peerCert;              // set by the verify block, read at ready
};

struct AppleTlsSocket::ReadyInfo {
    QString protocol;
    QString cipher;
    QHostAddress peer;
    quint16 localPort{0};
    QByteArray cert;
};

namespace {

// How long a graceful close may take before the connection is dropped.
constexpr int kCloseTimeoutMs = 2000;

QString describe(nw_error_t error)
{
    if (!error) {
        return QStringLiteral("connection failed");
    }
    QString text;
    CFErrorRef cf = nw_error_copy_cf_error(error);
    if (cf) {
        CFStringRef description = CFErrorCopyDescription(cf);
        if (description) {
            text = QString::fromCFString(description);
            CFRelease(description);
        }
        CFRelease(cf);
    }
    const nw_error_domain_t domain = nw_error_get_error_domain(error);
    const QString where = domain == nw_error_domain_posix ? QStringLiteral("POSIX")
                          : domain == nw_error_domain_dns ? QStringLiteral("DNS")
                          : domain == nw_error_domain_tls ? QStringLiteral("TLS")
                                                          : QStringLiteral("domain %1").arg(int(domain));
    const QString code = QStringLiteral("%1 %2").arg(where).arg(nw_error_get_error_code(error));
    return text.isEmpty() ? code : QStringLiteral("%1 (%2)").arg(text, code);
}

QAbstractSocket::SocketError socketError(nw_error_t error)
{
    if (!error) {
        return QAbstractSocket::UnknownSocketError;
    }
    switch (nw_error_get_error_domain(error)) {
    case nw_error_domain_dns:
        return QAbstractSocket::HostNotFoundError;
    case nw_error_domain_tls:
        return QAbstractSocket::SslHandshakeFailedError;
    case nw_error_domain_posix:
        switch (nw_error_get_error_code(error)) {
        case ECONNREFUSED:
            return QAbstractSocket::ConnectionRefusedError;
        case ECONNRESET:
        case EPIPE:
            return QAbstractSocket::RemoteHostClosedError;
        case ETIMEDOUT:
            return QAbstractSocket::SocketTimeoutError;
        default:
            return QAbstractSocket::NetworkError;
        }
    default:
        return QAbstractSocket::UnknownSocketError;
    }
}

// The IANA numbers of the suites a radio is likely to pick, by OpenSSL's
// names (as QSslCipher gives them on the other platforms). Not cipherName:
// inside AppleTlsSocket::Impl that name is the socket's own member.
QString suiteName(uint16_t suite)
{
    switch (suite) {
    case 0x1301:
        return QStringLiteral("TLS_AES_128_GCM_SHA256");
    case 0x1302:
        return QStringLiteral("TLS_AES_256_GCM_SHA384");
    case 0x1303:
        return QStringLiteral("TLS_CHACHA20_POLY1305_SHA256");
    case 0xC02B:
        return QStringLiteral("ECDHE-ECDSA-AES128-GCM-SHA256");
    case 0xC02C:
        return QStringLiteral("ECDHE-ECDSA-AES256-GCM-SHA384");
    case 0xC02F:
        return QStringLiteral("ECDHE-RSA-AES128-GCM-SHA256");
    case 0xC030:
        return QStringLiteral("ECDHE-RSA-AES256-GCM-SHA384");
    case 0xCCA8:
        return QStringLiteral("ECDHE-RSA-CHACHA20-POLY1305");
    case 0xCCA9:
        return QStringLiteral("ECDHE-ECDSA-CHACHA20-POLY1305");
    default:
        return QStringLiteral("0x%1").arg(uint(suite), 4, 16, QLatin1Char('0'));
    }
}

} // namespace

// The framework's side: everything that runs on its queue, handing each
// event to the socket's thread for the connection it belongs to.
struct AppleTlsSocket::Impl {
    template <typename F>
    static void post(const std::shared_ptr<Shared>& shared, quint64 id, F&& f)
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        AppleTlsSocket* owner = shared->owner;
        if (!owner) {
            return;
        }
        // An event posted to a QObject is dropped if the object goes first;
        // the owner is cleared under this lock before it does.
        QMetaObject::invokeMethod(owner, [owner, id, f = std::forward<F>(f)]() mutable {
            if (owner->m_connId == id) {
                f(owner);
            }
        }, Qt::QueuedConnection);
    }

    static ReadyInfo readyInfo(nw_connection_t connection)
    {
        ReadyInfo info;
        nw_protocol_definition_t tls = nw_protocol_copy_tls_definition();
        nw_protocol_metadata_t metadata = nw_connection_copy_protocol_metadata(connection, tls);
        if (metadata) {
            sec_protocol_metadata_t sec = nw_tls_copy_sec_protocol_metadata(metadata);
            if (sec) {
                const uint16_t version = uint16_t(sec_protocol_metadata_get_negotiated_tls_protocol_version(sec));
                info.protocol = version == 0x0304   ? QStringLiteral("1.3")
                                : version == 0x0303 ? QStringLiteral("1.2")
                                                    : QStringLiteral("0x%1").arg(uint(version), 4, 16, QLatin1Char('0'));
                info.cipher = suiteName(uint16_t(sec_protocol_metadata_get_negotiated_tls_ciphersuite(sec)));
                sec_release(sec);
            }
            nw_release(metadata);
        }
        nw_release(tls);
        nw_path_t path = nw_connection_copy_current_path(connection);
        if (path) {
            nw_endpoint_t remote = nw_path_copy_effective_remote_endpoint(path);
            if (remote) {
                if (nw_endpoint_get_type(remote) == nw_endpoint_type_address) {
                    if (const struct sockaddr* address = nw_endpoint_get_address(remote)) {
                        info.peer = QHostAddress(address);
                    }
                }
                nw_release(remote);
            }
            nw_endpoint_t local = nw_path_copy_effective_local_endpoint(path);
            if (local) {
                info.localPort = nw_endpoint_get_port(local);
                nw_release(local);
            }
            nw_release(path);
        }
        return info;
    }

    static void receive(nw_connection_t connection, std::shared_ptr<Shared> shared, quint64 id)
    {
        nw_connection_receive(connection, 1, 64 * 1024,
            ^(dispatch_data_t content, nw_content_context_t context, bool isComplete, nw_error_t error) {
            if (content) {
                __block QByteArray bytes;
                dispatch_data_apply(content, ^bool(dispatch_data_t region, size_t offset, const void* buffer, size_t size) {
                    (void)region;
                    (void)offset;
                    bytes.append(static_cast<const char*>(buffer), qsizetype(size));
                    return true;
                });
                // A __block variable cannot go into a lambda: a copy can.
                const QByteArray received = bytes;
                post(shared, id, [received](AppleTlsSocket* socket) { socket->handleData(received); });
            }
            if (error) {
                const QString message = describe(error);
                const QAbstractSocket::SocketError kind = socketError(error);
                post(shared, id, [message, kind](AppleTlsSocket* socket) { socket->handleFailed(message, kind); });
                nw_connection_cancel(connection);
                return;
            }
            if (isComplete && context && nw_content_context_get_is_final(context)) {
                // The radio (or something on the way) closed the session.
                post(shared, id, [](AppleTlsSocket* socket) {
                    socket->handleFailed(QStringLiteral("The remote host closed the connection"),
                                         QAbstractSocket::RemoteHostClosedError);
                });
                nw_connection_cancel(connection);
                return;
            }
            receive(connection, shared, id);
        });
    }
};

AppleTlsSocket::AppleTlsSocket(QObject* parent)
    : QObject(parent)
    , m_shared(std::make_shared<Shared>())
{
    m_shared->owner = this;
    m_queue = dispatch_queue_create("fi.oksa.oksasdr.wan-tls", DISPATCH_QUEUE_SERIAL);
}

AppleTlsSocket::~AppleTlsSocket()
{
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        m_shared->owner = nullptr;
    }
    finishConnection();
    dispatch_release((dispatch_queue_t)m_queue);
}

void AppleTlsSocket::connectToHostEncrypted(const QString& host, quint16 port)
{
    finishConnection();
    m_readBuffer.clear();
    m_error.clear();
    m_peer = QHostAddress();
    m_localPort = 0;
    m_protocol.clear();
    m_cipher.clear();
    m_peerCert.clear();
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        m_shared->peerCert.clear();
    }
    const quint64 id = ++m_connId;
    const std::shared_ptr<Shared> shared = m_shared;
    dispatch_queue_t queue = (dispatch_queue_t)m_queue;

    nw_parameters_configure_protocol_block_t configureTls = ^(nw_protocol_options_t tlsOptions) {
        sec_protocol_options_t sec = nw_tls_copy_sec_protocol_options(tlsOptions);
        sec_protocol_options_set_min_tls_protocol_version(sec, tls_protocol_version_TLSv12);
        sec_protocol_options_set_max_tls_protocol_version(sec, tls_protocol_version_TLSv13);
        // The radio's certificate is self-signed: accepted here, its DER kept
        // for WanConnection's TOFU pin (the check that catches a stranger).
        sec_protocol_options_set_verify_block(sec,
            ^(sec_protocol_metadata_t metadata, sec_trust_t trustRef, sec_protocol_verify_complete_t complete) {
            (void)metadata;
            QByteArray der;
            SecTrustRef trust = sec_trust_copy_ref(trustRef);
            if (trust) {
                CFArrayRef chain = SecTrustCopyCertificateChain(trust);
                if (chain) {
                    if (CFArrayGetCount(chain) > 0) {
                        SecCertificateRef leaf = (SecCertificateRef)CFArrayGetValueAtIndex(chain, 0);
                        CFDataRef data = SecCertificateCopyData(leaf);
                        if (data) {
                            der = QByteArray(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                                             qsizetype(CFDataGetLength(data)));
                            CFRelease(data);
                        }
                    }
                    CFRelease(chain);
                }
                CFRelease(trust);
            }
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                shared->peerCert = der;
            }
            complete(true);
        }, queue);
        sec_release(sec);
    };
    nw_parameters_t parameters = nw_parameters_create_secure_tcp(configureTls, NW_PARAMETERS_DEFAULT_CONFIGURATION);
    const QByteArray hostName = host.toUtf8();
    const QByteArray portText = QByteArray::number(port);
    nw_endpoint_t endpoint = nw_endpoint_create_host(hostName.constData(), portText.constData());
    nw_connection_t connection = nw_connection_create(endpoint, parameters);
    nw_release(endpoint);
    nw_release(parameters);
    if (!connection) {
        m_error = QStringLiteral("could not create a connection to %1:%2").arg(host).arg(port);
        emit errorOccurred(QAbstractSocket::UnknownSocketError);
        return;
    }
    // Two references, each released by its own holder (as Apple's nwcat
    // sample): this object's, the one nw_connection_create gave, released in
    // finishConnection(); and the state handler's, released by the handler
    // at the cancelled state, the last it sees. Build 188 had only the one,
    // released by the handler while m_conn still pointed at it: a failed
    // handshake (the framework cancels on its queue) left finishConnection()
    // cancelling a freed connection, and the app crashed (Barry N2BJ's
    // SmartLink, EXC_BAD_ACCESS in nw_connection_cancel_inner).
    m_conn = connection;
    m_state = QAbstractSocket::ConnectingState;
    nw_connection_set_queue(connection, queue);
    // Not retained by the handler (a __block variable is not, without ARC),
    // so the connection and its handler hold no cycle: the handler's
    // reference is this nw_retain.
    nw_retain(connection);
    __block nw_connection_t conn = connection;
    nw_connection_set_state_changed_handler(connection, ^(nw_connection_state_t state, nw_error_t error) {
        switch (state) {
        case nw_connection_state_ready: {
            ReadyInfo info = Impl::readyInfo(conn);
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                info.cert = shared->peerCert;
            }
            Impl::post(shared, id, [info](AppleTlsSocket* socket) { socket->handleReady(info); });
            Impl::receive(conn, shared, id);
            break;
        }
        case nw_connection_state_waiting:
        case nw_connection_state_failed: {
            // Waiting: no usable path now (no network, refused); QSslSocket
            // fails at once there, and the callers retry, so this does too.
            const QString message = describe(error);
            const QAbstractSocket::SocketError kind = socketError(error);
            Impl::post(shared, id, [message, kind](AppleTlsSocket* socket) { socket->handleFailed(message, kind); });
            nw_connection_cancel(conn);
            break;
        }
        case nw_connection_state_cancelled:
            Impl::post(shared, id, [](AppleTlsSocket* socket) { socket->handleCancelled(); });
            nw_release(conn);
            break;
        default:
            break;
        }
    });
    qCDebug(lcSmartLink) << "AppleTlsSocket: connecting to" << host << ":" << port << "(TLS 1.2-1.3, Network framework)";
    nw_connection_start(connection);
}

void AppleTlsSocket::disconnectFromHost()
{
    if (m_state == QAbstractSocket::UnconnectedState || m_state == QAbstractSocket::ClosingState) {
        return;
    }
    if (m_state != QAbstractSocket::ConnectedState) {
        abort();
        return;
    }
    m_state = QAbstractSocket::ClosingState;
    nw_connection_t connection = (nw_connection_t)m_conn;
    // The end of the stream after what was written; then the connection is
    // cancelled, and its cancelled state brings disconnected().
    nw_connection_send(connection, nullptr, NW_CONNECTION_FINAL_MESSAGE_CONTEXT, true, ^(nw_error_t error) {
        (void)error;   // closing either way
        nw_connection_cancel(connection);
    });
    const quint64 id = m_connId;
    QTimer::singleShot(kCloseTimeoutMs, this, [this, id] {
        if (m_connId == id && m_state == QAbstractSocket::ClosingState) {
            abort();
        }
    });
}

void AppleTlsSocket::abort()
{
    const bool wasUp = m_state == QAbstractSocket::ConnectedState || m_state == QAbstractSocket::ClosingState;
    finishConnection();
    m_readBuffer.clear();
    if (wasUp) {
        emit disconnected();
    }
}

qint64 AppleTlsSocket::write(const QByteArray& data)
{
    if (!m_conn || m_state != QAbstractSocket::ConnectedState) {
        return -1;
    }
    if (data.isEmpty()) {
        return 0;
    }
    dispatch_data_t content = dispatch_data_create(data.constData(), size_t(data.size()),
                                                   (dispatch_queue_t)m_queue,
                                                   DISPATCH_DATA_DESTRUCTOR_DEFAULT);   // copies the bytes
    const std::shared_ptr<Shared> shared = m_shared;
    const quint64 id = m_connId;
    nw_connection_send((nw_connection_t)m_conn, content, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, true,
        ^(nw_error_t error) {
        if (error) {
            const QString message = describe(error);
            const QAbstractSocket::SocketError kind = socketError(error);
            Impl::post(shared, id, [message, kind](AppleTlsSocket* socket) { socket->handleFailed(message, kind); });
        }
    });
    dispatch_release(content);
    return data.size();
}

QByteArray AppleTlsSocket::readAll()
{
    return std::exchange(m_readBuffer, QByteArray());
}

void AppleTlsSocket::handleReady(const ReadyInfo& info)
{
    if (m_state != QAbstractSocket::ConnectingState) {
        return;
    }
    m_state = QAbstractSocket::ConnectedState;
    m_peer = info.peer;
    m_localPort = info.localPort;
    m_protocol = info.protocol;
    m_cipher = info.cipher;
    m_peerCert = info.cert;
    emit encrypted();
}

void AppleTlsSocket::handleData(const QByteArray& data)
{
    if (m_state != QAbstractSocket::ConnectedState && m_state != QAbstractSocket::ClosingState) {
        return;
    }
    m_readBuffer.append(data);
    emit readyRead();
}

void AppleTlsSocket::handleFailed(const QString& message, QAbstractSocket::SocketError error)
{
    if (m_state == QAbstractSocket::UnconnectedState) {
        return;
    }
    const bool wasUp = m_state == QAbstractSocket::ConnectedState || m_state == QAbstractSocket::ClosingState;
    m_error = message;
    qCWarning(lcSmartLink).noquote() << "AppleTlsSocket:" << message;
    finishConnection();
    // As QSslSocket: the error first, then disconnected() for a session
    // that had been up.
    emit errorOccurred(error);
    if (wasUp) {
        emit disconnected();
    }
}

void AppleTlsSocket::handleCancelled()
{
    if (m_state == QAbstractSocket::UnconnectedState) {
        return;
    }
    const bool wasUp = m_state == QAbstractSocket::ConnectedState || m_state == QAbstractSocket::ClosingState;
    finishConnection();
    if (wasUp) {
        emit disconnected();
    }
}

void AppleTlsSocket::finishConnection()
{
    ++m_connId;   // whatever the old connection still sends is dropped
    m_state = QAbstractSocket::UnconnectedState;
    if (m_conn) {
        // Cancelled (nothing more if the framework already cancelled it on a
        // failure), then this object's reference released; the state handler
        // keeps its own until the cancelled state (see connectToHostEncrypted).
        nw_connection_t connection = (nw_connection_t)m_conn;
        m_conn = nullptr;
        nw_connection_cancel(connection);
        nw_release(connection);
    }
}

} // namespace AetherSDR
