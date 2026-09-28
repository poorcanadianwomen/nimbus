#include "ws.h"

#include "log.h"

#include <QtCore/QRandomGenerator>
#include <QtNetwork/QSslSocket>

namespace nimbus {

namespace {

constexpr int kCompactThreshold = 64 * 1024;

// Largest frame accepted. A Ready carrying every server, channel and member of a
// large account is the biggest thing that arrives and is well under this; the cap
// exists so a corrupt length field cannot ask for an arbitrary allocation.
constexpr quint64 kMaxPayloadBytes = 64ull * 1024 * 1024;

QByteArray generateKey() {
    QByteArray bytes(16, 0);
    for (int i = 0; i < 16; ++i) bytes[i] = char(QRandomGenerator::global()->bounded(256));
    return bytes.toBase64();
}

} // namespace

QByteArray maskFramePayload(const QByteArray& data, const QByteArray& key) {
    QByteArray out = data;
    if (key.isEmpty()) return out;
    for (int i = 0; i < out.size(); ++i) {
        out[i] ^= key[i % 4];
    }
    return out;
}

QByteArray encodeWebSocketFrame(quint8 firstByte, const QByteArray& payload,
                                const QByteArray& maskingKey) {
    const bool masked = !maskingKey.isEmpty();

    QByteArray frame;
    frame.append(char(firstByte));

    // The MASK bit lives in the *second* byte, alongside the length, and it is
    // independent of whether the length needs a 7/16/64-bit marker. Setting it
    // only via the marker produces an unmasked-declared frame carrying a key and
    // masked bytes, which the peer reads as a short payload of garbage.
    const int maskFlag = masked ? 0x80 : 0x00;
    const int len = payload.size();
    if (len < 126) {
        frame.append(char(maskFlag | len));
    } else if (len < 65536) {
        frame.append(char(maskFlag | 126));
        frame.append(char((len >> 8) & 0xFF));
        frame.append(char(len & 0xFF));
    } else {
        frame.append(char(maskFlag | 127));
        for (int i = 7; i >= 0; --i) frame.append(char((quint64(len) >> (i * 8)) & 0xFF));
    }

    if (masked) {
        frame.append(maskingKey);
        frame.append(maskFramePayload(payload, maskingKey));
    } else {
        frame.append(payload);
    }
    return frame;
}

WebSocket::WebSocket(const QUrl& url, QObject* parent)
    : QObject(parent), m_url(url) {
    m_socket = new QSslSocket(this);

    connect(m_socket, &QSslSocket::connected, this, &WebSocket::doHandshake);
    connect(m_socket, &QSslSocket::readyRead, this, &WebSocket::onReadyRead);
    connect(m_socket, &QSslSocket::disconnected, this, [this]() {
        if (m_state == State::Closed) return;
        // A socket that drops while we were closing is our own doing, so it is
        // reported with the code we asked for rather than as a 1006 failure.
        const bool deliberate = m_state == State::Closing;
        const quint16 code = deliberate ? m_closeCode : quint16(1006);
        const QString reason = deliberate ? m_closeReason : QStringLiteral("connection lost");
        m_state = State::Closed;
        emit disconnected(code, reason);
    });
    // Single-overload signal, so no QOverload disambiguation is needed; wrapping
    // it anyway fails to compile on this Qt.
    connect(m_socket, &QAbstractSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                emit error(m_socket->errorString());
            });
    connect(m_socket, &QSslSocket::sslErrors, this, &WebSocket::onSslErrors);
}

WebSocket::~WebSocket() {
    if (m_state == State::Open || m_state == State::Handshake) {
        close(1001, QStringLiteral("client closing"));
    }
}

void WebSocket::open() {
    if (m_state != State::Connecting) return;
    // No waitForEncrypted: it blocks the calling thread, which on the GUI thread
    // is a visible freeze for the length of the timeout on every reconnect. The
    // connected() signal already fires once the TLS handshake completes.
    m_socket->connectToHostEncrypted(m_url.host(), m_url.port(443));
}

void WebSocket::doHandshake() {
    // The query has to be part of the request target. In this protocol the query
    // is where authentication lives (there is no authenticate frame), so sending
    // path() alone connects successfully and then delivers nothing at all, which
    // reads as a silent server rather than a missing credential.
    QString target = m_url.path().isEmpty() ? QStringLiteral("/") : m_url.path();
    if (m_url.hasQuery()) {
        target += QLatin1Char('?') + m_url.query(QUrl::FullyEncoded);
    }

    QByteArray request;
    request += "GET " + target.toUtf8() + " HTTP/1.1\r\n";
    request += "Host: " + m_url.host().toUtf8() + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + generateKey() + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    // No Sec-WebSocket-Extensions header on purpose; see ws.h.
    request += "\r\n";

    m_socket->write(request);
    m_socket->flush();
    m_state = State::Handshake;
}

void WebSocket::onSslErrors(const QList<QSslError>& errors) {
    QString msg;
    for (const auto& e : errors) msg += e.errorString() + "; ";
    emit error(QStringLiteral("SSL: ") + msg);
    m_socket->ignoreSslErrors();
}

QByteArray WebSocket::take(int count) {
    const QByteArray out = m_readBuffer.mid(m_readPos, count);
    m_readPos += count;
    return out;
}

void WebSocket::compact() {
    if (m_readPos == 0) return;
    if (m_readPos >= m_readBuffer.size()) {
        m_readBuffer.clear();
    } else if (m_readPos >= kCompactThreshold) {
        m_readBuffer.remove(0, m_readPos);
    } else {
        return;
    }
    m_readPos = 0;
}

void WebSocket::onReadyRead() {
    const QByteArray chunk = m_socket->readAll();
    if (logLevel() == LogLevel::Trace) {
        qDebug() << "ws: read" << chunk.size() << "bytes, buffered"
                 << (m_readBuffer.size() + chunk.size()) << "state" << int(m_state);
    }
    m_readBuffer += chunk;

    if (m_state == State::Handshake) {
        const int headerEnd = m_readBuffer.indexOf("\r\n\r\n", m_readPos);
        if (headerEnd < 0) return;

        const QByteArray headers = m_readBuffer.mid(m_readPos, headerEnd - m_readPos);
        m_readPos = headerEnd + 4;
        compact();

        if (!headers.startsWith("HTTP/1.1 101")) {
            emit error(QStringLiteral("WebSocket handshake failed: ") +
                       QString::fromUtf8(headers.left(200)));
            close(1002, QStringLiteral("bad handshake"));
            return;
        }

        m_state = State::Open;
        emit connected();
    }

    if (m_state == State::Open) {
        parseFrames();
    }
}

void WebSocket::parseFrames() {
    while (true) {
        switch (m_parseState) {
            case ParseState::Header: {
                if (available() < 2) return;
                const quint8 b1 = quint8(m_readBuffer.at(m_readPos));
                const quint8 b2 = quint8(m_readBuffer.at(m_readPos + 1));
                m_readPos += 2;

                m_frameFin = (b1 & 0x80) != 0;
                const quint8 rsv1 = b1 & 0x40;
                const quint8 rsv2 = b1 & 0x20;
                const quint8 rsv3 = b1 & 0x10;
                m_frameOpcode = b1 & 0x0F;
                m_frameMasked = (b2 & 0x80) != 0;

                if (rsv1 || rsv2 || rsv3) {
                    // Nothing was negotiated, so a reserved bit can only mean a
                    // compressed frame arriving at a client that cannot inflate it.
                    emit error(QStringLiteral("rsv bit set with no negotiated extension"));
                    close(1002, QStringLiteral("rsv bits set"));
                    return;
                }

                const quint64 len = b2 & 0x7F;
                if (len == 126) {
                    m_parseState = ParseState::ExtendedLength16;
                } else if (len == 127) {
                    m_parseState = ParseState::ExtendedLength64;
                } else {
                    m_payloadRemaining = len;
                    m_parseState = m_frameMasked ? ParseState::MaskingKey : ParseState::Payload;
                }
                break;
            }
            case ParseState::ExtendedLength16: {
                if (available() < 2) return;
                m_payloadRemaining =
                    (quint64(quint8(m_readBuffer.at(m_readPos))) << 8) |
                    quint64(quint8(m_readBuffer.at(m_readPos + 1)));
                m_readPos += 2;
                m_parseState = m_frameMasked ? ParseState::MaskingKey : ParseState::Payload;
                break;
            }
            case ParseState::ExtendedLength64: {
                if (available() < 8) return;
                quint64 len = 0;
                for (int i = 0; i < 8; ++i) {
                    len = (len << 8) | quint64(quint8(m_readBuffer.at(m_readPos + i)));
                }
                m_readPos += 8;
                m_payloadRemaining = len;
                m_parseState = m_frameMasked ? ParseState::MaskingKey : ParseState::Payload;
                break;
            }
            case ParseState::MaskingKey: {
                if (available() < 4) return;
                m_maskingKey = take(4);
                m_parseState = ParseState::Payload;
                break;
            }
            case ParseState::Payload: {
                if (m_payloadRemaining > kMaxPayloadBytes) {
                    emit error(QStringLiteral("frame of %1 bytes exceeds the cap").arg(m_payloadRemaining));
                    close(1009, QStringLiteral("frame too large"));
                    return;
                }
                if (quint64(available()) < m_payloadRemaining) return;

                QByteArray payload = take(int(m_payloadRemaining));
                if (m_frameMasked) payload = maskFramePayload(payload, m_maskingKey);
                m_maskingKey.clear();
                compact();

                const quint8 opcode = m_frameOpcode;
                const bool fin = m_frameFin;
                if (logLevel() == LogLevel::Trace) {
                    qDebug() << "ws: frame opcode" << opcode << "fin" << fin << "len" << payload.size();
                }
                m_parseState = ParseState::Header;
                handleFrame(opcode, fin, payload);

                if (m_state != State::Open) return;
                break;
            }
        }
    }
}

void WebSocket::handleFrame(quint8 opcode, bool fin, const QByteArray& payload) {
    switch (opcode) {
        case 0x0: { // continuation
            if (m_fragmentOpcode == 0) {
                emit error(QStringLiteral("continuation frame with nothing to continue"));
                close(1002, QStringLiteral("bad continuation"));
                return;
            }
            m_fragments += payload;
            if (!fin) return;
            const quint8 original = m_fragmentOpcode;
            const QByteArray whole = m_fragments;
            m_fragments.clear();
            m_fragmentOpcode = 0;
            handleFrame(original, true, whole);
            return;
        }
        case 0x1:   // text
        case 0x2: { // binary
            if (!fin) {
                m_fragmentOpcode = opcode;
                m_fragments = payload;
                return;
            }
            if (opcode == 0x1) {
                emit textMessageReceived(QString::fromUtf8(payload));
            }
            // format=json is requested on the URL, so binary frames are not
            // expected and are dropped rather than guessed at.
            return;
        }
        case 0x8: { // close
            quint16 code = 1005;
            QString reason;
            if (payload.size() >= 2) {
                code = quint16((quint8(payload[0]) << 8) | quint8(payload[1]));
                if (payload.size() > 2) reason = QString::fromUtf8(payload.mid(2));
            }
            m_closeCode = code;
            m_closeReason = reason;
            m_state = State::Closing;
            writeFrame(0x8, payload);
            m_socket->flush();
            m_state = State::Closed;
            emit disconnected(code, reason);
            return;
        }
        case 0x9: // ping
            writeFrame(0xA, payload);
            m_socket->flush();
            return;
        case 0xA: // pong
            return;
        default:
            emit error(QStringLiteral("unknown opcode %1").arg(opcode));
            close(1002, QStringLiteral("unknown opcode"));
            return;
    }
}

void WebSocket::writeFrame(quint8 opcode, const QByteArray& payload) {
    if (m_socket->state() != QAbstractSocket::ConnectedState) return;
    // Every client-to-server frame is masked; an unmasked one is a protocol
    // violation that gets the connection dropped by the server.
    const quint32 random = QRandomGenerator::global()->generate();
    const QByteArray key(reinterpret_cast<const char*>(&random), 4);
    m_socket->write(encodeWebSocketFrame(quint8(0x80) | opcode, payload, key));
}

void WebSocket::close(quint16 code, const QString& reason) {
    if (m_state == State::Closed || m_state == State::Closing) return;
    m_state = State::Closing;
    m_closeCode = code;
    m_closeReason = reason;

    QByteArray payload;
    payload.append(char((code >> 8) & 0xFF));
    payload.append(char(code & 0xFF));
    payload += reason.toUtf8().left(123); // the control frame budget is 125 bytes

    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        writeFrame(0x8, payload);
        m_socket->flush();
    }
    m_socket->disconnectFromHost();
}

void WebSocket::sendText(const QString& text) {
    if (m_state != State::Open) return;
    writeFrame(0x1, text.toUtf8());
    m_socket->flush();
}

} // namespace nimbus
