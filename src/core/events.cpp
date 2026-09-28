#include "events.h"

#include "json.h"
#include "log.h"
#include "ws.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonDocument>
#include <QtCore/QRandomGenerator>
#include <QtCore/QTimer>
#include <QtCore/QUrlQuery>

namespace nimbus {

Events::Events(QObject* parent)
    : QObject(parent) {
    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(kPingIntervalMs);
    connect(m_pingTimer, &QTimer::timeout, this, &Events::sendHeartbeat);

    m_pongTimer = new QTimer(this);
    m_pongTimer->setSingleShot(true);
    m_pongTimer->setInterval(kPongTimeoutMs);
    connect(m_pongTimer, &QTimer::timeout, this, [this]() {
        // Silence is the failure signal: the socket can stay open long after the
        // server has stopped answering, so waiting for TCP to drop would let a
        // dead session look alive indefinitely.
        qWarning() << "events: no Pong within" << kPongTimeoutMs << "ms, reconnecting";
        m_pongTimer->stop();
        if (m_ws) m_ws->close(4000, "pong timeout");
        scheduleReconnect("pong timeout");
    });

    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    m_connectTimer->setInterval(kConnectTimeoutMs);
    connect(m_connectTimer, &QTimer::timeout, this, [this]() {
        qWarning() << "events: no Ready within" << kConnectTimeoutMs << "ms of connecting";
        if (m_ws) m_ws->close(4001, "ready timeout");
        scheduleReconnect("ready timeout");
    });

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (m_running && !m_ready) openSocket();
    });
}

Events::~Events() {
    stop();
}

QUrl Events::socketUrl(const QString& wsUrl, const QString& token) {
    QUrl url(wsUrl);
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("version"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    if (!token.isEmpty()) {
        query.addQueryItem(QStringLiteral("token"), token);
    }
    url.setQuery(query);
    return url;
}

QUrl Events::buildSocketUrl() const {
    return socketUrl(m_wsUrl, m_token);
}

void Events::start() {
    if (m_running) return;
    if (m_wsUrl.isEmpty()) {
        emit error(QStringLiteral("no websocket url; fetch the instance root first"));
        return;
    }
    m_running = true;
    m_deliberateStop = false;
    m_backoffMs = kMinBackoffMs;
    openSocket();
}

void Events::stop() {
    m_running = false;
    m_deliberateStop = true;
    m_ready = false;
    m_pingTimer->stop();
    m_pongTimer->stop();
    m_connectTimer->stop();
    m_reconnectTimer->stop();
    if (m_ws) {
        m_ws->close(1000, QStringLiteral("client closing"));
        m_ws->deleteLater();
        m_ws = nullptr;
    }
}

void Events::openSocket() {
    m_ready = false;
    m_pingTimer->stop();
    m_pongTimer->stop();
    m_connectTimer->stop();

    if (m_ws) {
        m_ws->close(1000, QStringLiteral("reconnecting"));
        m_ws->deleteLater();
        m_ws = nullptr;
    }

    emit connecting();

    m_ws = new WebSocket(buildSocketUrl(), this);
    connect(m_ws, &WebSocket::connected, this, &Events::onSocketConnected);
    connect(m_ws, &WebSocket::disconnected, this, &Events::onSocketDisconnected);
    connect(m_ws, &WebSocket::textMessageReceived, this, &Events::onTextMessage);
    connect(m_ws, &WebSocket::error, this, [this](const QString& msg) {
        emit error(QStringLiteral("socket: ") + msg);
    });
    m_ws->open();

    // Covers handshake plus Ready. Ready is what actually means "connected";
    // the TCP upgrade alone proves nothing.
    m_connectTimer->start();
}

void Events::onSocketConnected() {
    qDebug() << "events: socket open, awaiting Ready";
    // The server may send Ping before Ready; answering it is the only thing the
    // client owes at this point, since auth already rode in on the URL.
    m_pingTimer->start();
}

void Events::onSocketDisconnected(int code, const QString& reason) {
    m_pingTimer->stop();
    m_pongTimer->stop();
    m_connectTimer->stop();

    const bool wasReady = m_ready;
    m_ready = false;
    emit disconnected(code, reason);

    if (m_deliberateStop || !m_running) return;
    // A close during handshake means the URL or token was refused; retrying that
    // unchanged just burns the backoff budget.
    if (!wasReady && code == 1008) {
        qCritical() << "events: rejected by server" << code << reason;
        stop();
        return;
    }
    scheduleReconnect(reason);
}

void Events::scheduleReconnect(const QString& reason) {
    if (!m_running || m_deliberateStop) return;
    if (m_reconnectTimer->isActive()) return;

    // Jitter keeps a fleet of clients from re-arriving together after a server
    // restart and knocking it straight back over.
    const int jitter = QRandomGenerator::global()->bounded(m_backoffMs / 4 + 1);
    const int delay = qMin(m_backoffMs + jitter, kMaxBackoffMs);
    qDebug() << "events: reconnecting in" << delay << "ms:" << reason;

    m_reconnectTimer->start(delay);
    m_backoffMs = qMin(m_backoffMs * 2, kMaxBackoffMs);
}

void Events::onTextMessage(const QString& text) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit error(QStringLiteral("malformed frame: ") + parseError.errorString());
        return;
    }

    const QJsonObject frame = doc.object();
    const QString type = jsonString(frame, "type");

    if (type == QLatin1String("Ping")) {
        // Echo the client's own timestamp back untouched; the server relays it.
        QJsonObject pong;
        pong["type"] = QStringLiteral("Pong");
        pong["data"] = frame.value("data");
        if (m_ws) m_ws->sendText(QString::fromUtf8(QJsonDocument(pong).toJson(QJsonDocument::Compact)));
        return;
    }

    if (type == QLatin1String("Pong")) {
        m_pongTimer->stop();
        const qint64 sent = static_cast<qint64>(frame.value("data").toDouble());
        if (sent > 0) m_lastRttMs = QDateTime::currentMSecsSinceEpoch() - sent;
        return;
    }

    if (type == QLatin1String("Error")) {
        const QJsonObject data = jsonObject(frame, "data");
        // The wire shape is {"type": "InvalidSession"} for most failures, with a
        // human-readable "msg" only on the ContactSupport variant. Reading "msg"
        // alone yields an empty message for everything else.
        QString message = jsonString(data, "msg");
        if (message.isEmpty()) message = jsonString(data, "type");
        if (message.isEmpty()) {
            message = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
        }
        qCritical() << "events: server error" << message;
        emit error(message);
        if (m_ws) m_ws->close(4002, "server error");
        return;
    }

    if (type == QLatin1String("Authenticated")) {
        // Expected but carries nothing; Ready is the state change that matters.
        qDebug() << "events: authenticated";
        return;
    }

    if (type == QLatin1String("Ready")) {
        m_connectTimer->stop();
        m_ready = true;
        m_backoffMs = kMinBackoffMs;
        m_pingTimer->start();
        sendHeartbeat();
        emit connected();
        emit ready(frame);
        return;
    }

    if (!m_ready) {
        // Anything before Ready is out of order; dropping it is safer than
        // letting the app mutate a store it has not populated.
        qWarning() << "events: ignoring" << type << "received before Ready";
        return;
    }

    emit event(type, frame);
}

void Events::sendHeartbeat() {
    if (!m_ws) return;
    m_lastPingSentMs = QDateTime::currentMSecsSinceEpoch();

    QJsonObject ping;
    ping["type"] = QStringLiteral("Ping");
    ping["data"] = double(m_lastPingSentMs);
    m_ws->sendText(QString::fromUtf8(QJsonDocument(ping).toJson(QJsonDocument::Compact)));

    armPongWatchdog();
}

void Events::armPongWatchdog() {
    m_pongTimer->start();
}

} // namespace nimbus
