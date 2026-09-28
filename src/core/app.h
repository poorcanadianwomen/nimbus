#pragma once

#include "events.h"
#include "models.h"
#include "rest.h"
#include "session.h"
#include "store.h"

#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QStringList>

#include <functional>

namespace nimbus {

// Yields the new attachment id, or an empty id plus the reason it failed.
using UploadCallback = std::function<void(const QString& id, const QString& error)>;

enum class AuthState {
    Idle,
    Authenticating,
    Authenticated,
    MfaRequired,
    AccountDisabled,
    Failed,
};

// Owns the transport, the REST client, the cache and the session. The UI binds to
// this and nothing below it.
class App : public QObject {
    Q_OBJECT
public:
    explicit App(QObject* parent = nullptr);
    ~App() override;

    // GET / on the API host. The websocket URL and CDN host come from this, so a
    // self-hosted instance needs no rebuild. Must succeed before connect().
    void discoverInstance(const QString& apiBaseUrl = {});

    void login(const QString& email, const QString& password,
               const QString& friendlyName = QStringLiteral("nimbus"));
    // Completes a login that came back as MfaRequired.
    void submitMfa(const QString& ticket, const QJsonObject& mfaResponse);
    void logout();

    // Loads the stored session and confirms it against the server. Emits
    // authenticated() on success, authFailed() with a reason otherwise.
    void restoreSession();

    // Opens the event stream. Requires a discovered instance and a session.
    // Named openStream/closeStream rather than connect/disconnect because those
    // names shadow QObject's own and silently break every connect() in this file.
    void openStream();
    void closeStream();

    Store* store() { return &m_store; }
    RestClient* rest() { return &m_rest; }
    Events* events() { return &m_events; }

    const InstanceConfig& config() const { return m_config; }
    const Session& session() const { return m_session; }
    AuthState authState() const { return m_authState; }
    bool isReady() const { return m_events.isReady(); }

    // --- actions ---
    void sendMessage(const QString& channelId, const QString& content,
                     const QString& replyToId = {});
    void sendMessage(const QString& channelId, const QString& content, const QString& replyToId,
                     const QStringList& attachmentIds);

    // Uploads to the CDN host from the instance root, not to an API route. The id
    // it returns is what DataMessageSend.attachments expects.
    void uploadAttachment(const QString& filename, const QByteArray& data,
                          const QString& contentType, UploadCallback cb);
    void editMessage(const QString& channelId, const QString& messageId, const QString& content);
    void deleteMessage(const QString& channelId, const QString& messageId);
    void setPinned(const QString& channelId, const QString& messageId, bool pinned);
    void addReaction(const QString& channelId, const QString& messageId, const QString& emojiId);
    void removeReaction(const QString& channelId, const QString& messageId, const QString& emojiId,
                        const QString& userId = {});
    void acknowledge(const QString& channelId, const QString& messageId);

    // History. `before` paginates backwards from the oldest message already held.
    void fetchMessages(const QString& channelId, int limit = 50);
    void fetchOlderMessages(const QString& channelId, int limit = 50);

    void fetchDms();
    void fetchUnreads();
    // Ready lists server channels as ids only, so the objects are fetched once
    // per server on first connect.
    void fetchServerChannels(const QString& serverId);
    void fetchEmojis(const QString& serverId);

    // The single entry point for gateway frames. Public because it is the seam the
    // core tests drive: every frame handler is reachable from here and from nowhere
    // else, and a handler that crashes on a frame the client has not seen yet -- a
    // reaction to a message outside the transcript window -- is the kind of defect
    // that only a live account ever produces.
    void dispatch(const QString& type, const QJsonObject& frame);

signals:
    void instanceDiscovered(const nimbus::InstanceConfig& config);
    void authStateChanged(nimbus::AuthState state, const QString& message);
    void authenticated(const QString& userId);
    void loggedOut();
    void ready();
    void disconnected(const QString& reason);
    void error(const QString& message);
    void rateLimited(const QString& route, int retryAfterMs);
    // Push-only: the server announces typing but never revokes it on a timer, so
    // the receiver owns the expiry.
    void typingChanged(const QString& channelId, const QString& userId, bool typing);

    // Emitted when a send fails so the provisional row can be withdrawn rather
    // than left in the transcript looking delivered.
    void sendFailed(const QString& channelId, const QString& nonce, const QString& reason);

private:
    void setAuthState(AuthState state, const QString& message = {});
    void applyReady(const QJsonObject& payload);
    void handleBulk(const QJsonArray& events);
    void handleMessageUpdate(const QJsonObject& frame);
    void ingestMessage(const QJsonObject& object);
    void ingestChannel(const QJsonObject& object);
    void ingestServer(const QJsonObject& object);
    void ingestUser(const QJsonObject& object);
    void ingestMember(const QJsonObject& object);
    void absorbBulkPayload(const RestResponse& response, const QString& channelId);

    InstanceConfig m_config;
    Session m_session;
    AuthState m_authState = AuthState::Idle;
    QString m_pendingMfaTicket;
    QStringList m_fetchedServerChannels;
    QSet<QString> m_fetchedServerEmojis;

    Store m_store;
    RestClient m_rest;
    Events m_events;
};

} // namespace nimbus
