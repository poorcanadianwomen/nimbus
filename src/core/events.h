#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QUrl>

namespace nimbus {

class WebSocket;

// The Stoat event stream, protocol version 1 over JSON.
//
// Three properties of this protocol shape the whole class and are worth stating
// because they differ from most gateways and are easy to get wrong:
//
//   * Authentication is a query parameter on the socket URL. There is no
//     IDENTIFY frame and no token in the first message.
//   * There is no sequence number and no RESUME. A dropped connection is a full
//     reconnect followed by a fresh Ready, so there is nothing to replay from.
//   * The heartbeat is a timestamped Ping/Pong, not an ack counter: the server
//     echoes back the exact millisecond value the client sent.
class Events : public QObject {
    Q_OBJECT
public:
    explicit Events(QObject* parent = nullptr);
    ~Events() override;

    void setToken(const QString& token) { m_token = token; }
    void setUrl(const QString& wsUrl) { m_wsUrl = wsUrl; }
    QString url() const { return m_wsUrl; }

    void start();
    void stop();
    bool isRunning() const { return m_running; }
    bool isReady() const { return m_ready; }

    // Round-trip time of the last completed heartbeat, for diagnostics.
    qint64 lastRttMs() const { return m_lastRttMs; }

    // Public and static so the connection URL can be asserted on directly. The
    // token is a query parameter in this protocol, so a missing version or format
    // parameter fails as a 1008 at connect time rather than at build time.
    static QUrl socketUrl(const QString& wsUrl, const QString& token);

signals:
    void connecting();
    void connected();
    void ready(const QJsonObject& payload);
    // Every event after Ready, passed through with its type intact so the app
    // owns dispatch and the transport stays ignorant of the domain.
    void event(const QString& type, const QJsonObject& data);
    void disconnected(int code, const QString& reason);
    void error(const QString& msg);

private:
    void openSocket();
    void scheduleReconnect(const QString& reason);
    void onSocketConnected();
    void onSocketDisconnected(int code, const QString& reason);
    void onTextMessage(const QString& text);
    void sendHeartbeat();
    void armPongWatchdog();
    QUrl buildSocketUrl() const;

    static constexpr int kPingIntervalMs = 30000;
    static constexpr int kPongTimeoutMs = 10000;
    static constexpr int kConnectTimeoutMs = 10000;
    static constexpr int kMinBackoffMs = 1000;
    static constexpr int kMaxBackoffMs = 30000;

    QString m_wsUrl;
    QString m_token;
    WebSocket* m_ws = nullptr;
    QTimer* m_pingTimer = nullptr;
    QTimer* m_pongTimer = nullptr;
    QTimer* m_connectTimer = nullptr;
    QTimer* m_reconnectTimer = nullptr;

    bool m_running = false;
    bool m_ready = false;
    bool m_deliberateStop = false;
    int m_backoffMs = kMinBackoffMs;
    qint64 m_lastPingSentMs = 0;
    qint64 m_lastRttMs = -1;
};

} // namespace nimbus
