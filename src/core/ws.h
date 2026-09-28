#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QUrl>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QSslError>

namespace nimbus {

// Exposed for tests. The 7/16/64-bit length boundaries are the part of RFC 6455
// that is easy to get subtly wrong and stays wrong until a payload large enough
// to hit the boundary arrives in production.
QByteArray encodeWebSocketFrame(quint8 firstByte, const QByteArray& payload,
                                const QByteArray& maskingKey);
QByteArray maskFramePayload(const QByteArray& data, const QByteArray& key);

// RFC 6455 client over TLS, written against QSslSocket because Qt ships no
// WebSocket implementation that can be pointed at an arbitrary TLS socket.
//
// Compression is deliberately not offered and the handshake never advertises it: the
// frame decoder has no inflater, so a server that accepted permessage-deflate would
// corrupt every message. rsv1 set on an inbound frame is therefore a protocol error
// rather than a bit to ignore.
class WebSocket : public QObject {
    Q_OBJECT
public:
    enum class State {
        Connecting,
        Handshake,
        Open,
        Closing,
        Closed,
    };

    explicit WebSocket(const QUrl& url, QObject* parent = nullptr);
    ~WebSocket() override;

    void open();
    void close(quint16 code = 1000, const QString& reason = {});
    void sendText(const QString& text);

    State state() const { return m_state; }

signals:
    void connected();
    void disconnected(quint16 code, const QString& reason);
    void textMessageReceived(const QString& text);
    void error(const QString& msg);

private:
    void doHandshake();
    void onReadyRead();
    void onSslErrors(const QList<QSslError>& errors);
    void parseFrames();
    void handleFrame(quint8 opcode, bool fin, const QByteArray& payload);
    void writeFrame(quint8 opcode, const QByteArray& payload);

    // Reads consume from the front of the buffer, so a cursor is kept instead of
    // removing the consumed prefix each time. remove(0, n) is O(n) and a
    // multi-megabyte Ready arrives in tens of thousands of chunks, which made
    // assembling one quadratic in its own size.
    int available() const { return m_readBuffer.size() - m_readPos; }
    QByteArray take(int count);
    void compact();

    QUrl m_url;
    State m_state = State::Connecting;
    QSslSocket* m_socket = nullptr;

    QByteArray m_readBuffer;
    int m_readPos = 0;

    enum class ParseState {
        Header,
        ExtendedLength16,
        ExtendedLength64,
        MaskingKey,
        Payload,
    };
    ParseState m_parseState = ParseState::Header;
    quint8 m_frameOpcode = 0;
    bool m_frameFin = false;
    bool m_frameMasked = false;
    quint64 m_payloadRemaining = 0;
    QByteArray m_maskingKey;

    // A large event is very likely to be fragmented, and each fragment is only
    // meaningful reassembled, so continuation frames are buffered rather than
    // delivered individually.
    QByteArray m_fragments;
    quint8 m_fragmentOpcode = 0;

    quint16 m_closeCode = 1000;
    QString m_closeReason;
};

} // namespace nimbus
