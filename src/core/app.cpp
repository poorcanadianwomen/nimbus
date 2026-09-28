#include "app.h"

#include "json.h"

#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QJsonArray>
#include <QtCore/QRandomGenerator>
#include <QtCore/QSet>
#include <QtCore/QUrlQuery>
#include <QtCore/QUuid>

namespace nimbus {

namespace {

QString makeNonce() {
    // Any unique string works; the server echoes it back on the created message
    // so a provisional row can be matched without guessing at the real id.
    return QStringLiteral("%1-%2")
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(QUuid::createUuid().toString(QUuid::Id128));
}

QString messagesPath(const QString& channelId, int limit, const QString& before, bool latest) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    if (!before.isEmpty()) {
        query.addQueryItem(QStringLiteral("before"), before);
    } else if (latest) {
        query.addQueryItem(QStringLiteral("sort"), QStringLiteral("Latest"));
    }
    query.addQueryItem(QStringLiteral("include_users"), QStringLiteral("true"));
    return QStringLiteral("/channels/%1/messages?%2").arg(channelId, query.toString(QUrl::FullyEncoded));
}

} // namespace

App::App(QObject* parent)
    : QObject(parent) {
    connect(&m_events, &Events::ready, this, &App::applyReady);
    connect(&m_events, &Events::event, this, &App::dispatch);
    connect(&m_events, &Events::disconnected, this,
            [this](int, const QString& reason) { emit disconnected(reason); });
    connect(&m_events, &Events::error, this, [this](const QString& msg) {
        if (m_authState == AuthState::Authenticated) {
            // A stream error after login means the session died server-side; the
            // next request will 401 and reauthenticate. Reporting it as an auth
            // failure here would send the user back to a login form they do not
            // need.
            emit error(msg);
        } else {
            setAuthState(AuthState::Failed, msg);
        }
    });

    connect(&m_rest, &RestClient::rateLimited, this, &App::rateLimited);
    connect(&m_rest, &RestClient::unauthorized, this, [this]() {
        if (m_authState == AuthState::Authenticated) {
            setAuthState(AuthState::Failed, QStringLiteral("session rejected by server"));
            m_events.stop();
        }
    });
}

App::~App() {
    m_events.stop();
}

// --- instance ----------------------------------------------------------------

void App::discoverInstance(const QString& apiBaseUrl) {
    if (!apiBaseUrl.isEmpty()) m_rest.setBaseUrl(apiBaseUrl);

    m_rest.get(QStringLiteral("/"), [this](const RestResponse& response) {
        if (!response.ok()) {
            setAuthState(AuthState::Failed,
                         QStringLiteral("instance discovery failed: ") + response.errorMessage());
            emit error(QStringLiteral("cannot reach %1: %2")
                           .arg(m_rest.baseUrl(), response.errorMessage()));
            return;
        }
        m_config = parseInstanceConfig(response.json());
        if (m_config.wsUrl.isEmpty()) {
            setAuthState(AuthState::Failed, QStringLiteral("instance advertises no websocket url"));
            return;
        }
        m_events.setUrl(m_config.wsUrl);
        emit instanceDiscovered(m_config);
    });
}

// --- auth --------------------------------------------------------------------

void App::setAuthState(AuthState state, const QString& message) {
    m_authState = state;
    emit authStateChanged(state, message);
}

void App::login(const QString& email, const QString& password, const QString& friendlyName) {
    setAuthState(AuthState::Authenticating);

    QJsonObject body;
    body["email"] = email;
    body["password"] = password;
    body["friendly_name"] = friendlyName;

    m_rest.post(QStringLiteral("/auth/session/login"), body, [this, friendlyName](const RestResponse& response) {
        const QJsonObject o = response.json();
        const QString result = jsonString(o, "result");

        if (!response.ok()) {
            setAuthState(AuthState::Failed, response.errorMessage());
            return;
        }

        if (result == QLatin1String("MFA")) {
            m_pendingMfaTicket = jsonString(o, "ticket");
            setAuthState(AuthState::MfaRequired, QString());
            return;
        }

        if (result == QLatin1String("Disabled")) {
            setAuthState(AuthState::AccountDisabled, QStringLiteral("account is disabled"));
            return;
        }

        if (result != QLatin1String("Success")) {
            setAuthState(AuthState::Failed,
                         QStringLiteral("unexpected login result: ") + response.errorMessage());
            return;
        }

        Session session;
        session.token = jsonString(o, "token");
        session.userId = jsonString(o, "user_id");
        session.name = jsonString(o, "name", friendlyName);
        session.lastSeen = QDateTime::fromString(jsonString(o, "last_seen"), Qt::ISODate);

        if (!session.isValid()) {
            setAuthState(AuthState::Failed, QStringLiteral("login response carried no session"));
            return;
        }

        m_session = session;
        m_rest.setToken(session.token);
        m_events.setToken(session.token);
        m_store.setSelfId(session.userId);
        m_store.setCdn(m_config.cdn);

        const QString saveError = SessionStore::save(session);
        if (!saveError.isEmpty()) emit error(saveError);

        setAuthState(AuthState::Authenticated);
        emit authenticated(session.userId);
    });
}

void App::submitMfa(const QString& ticket, const QJsonObject& mfaResponse) {
    if (ticket.isEmpty()) return;
    setAuthState(AuthState::Authenticating);

    QJsonObject body;
    body["mfa_ticket"] = ticket;
    body["mfa_response"] = mfaResponse;
    body["friendly_name"] = QStringLiteral("nimbus");

    m_rest.post(QStringLiteral("/auth/session/login"), body, [this](const RestResponse& response) {
        const QJsonObject o = response.json();
        if (!response.ok() || jsonString(o, "result") != QLatin1String("Success")) {
            setAuthState(AuthState::Failed, response.errorMessage());
            return;
        }
        Session session;
        session.token = jsonString(o, "token");
        session.userId = jsonString(o, "user_id");
        session.name = jsonString(o, "name", QStringLiteral("nimbus"));
        m_session = session;
        m_rest.setToken(session.token);
        m_events.setToken(session.token);
        m_store.setSelfId(session.userId);
        const QString saveError = SessionStore::save(session);
        if (!saveError.isEmpty()) emit error(saveError);
        setAuthState(AuthState::Authenticated);
        emit authenticated(session.userId);
    });
}

void App::restoreSession() {
    const Session stored = SessionStore::load();
    if (stored.token.isEmpty()) {
        setAuthState(AuthState::Idle);
        return;
    }

    m_rest.setToken(stored.token);
    m_events.setToken(stored.token);

    setAuthState(AuthState::Authenticating);
    m_rest.get(QStringLiteral("/users/@me"), [this, stored](const RestResponse& response) {
        // A rejection is reported, never acted on. Unlinking here destroyed the
        // stored credential on any transient 401 -- including the eviction the
        // server performs when one session token is used from two connections --
        // which costs the user a password re-entry for no benefit, since --login
        // overwrites the file regardless.
        if (response.statusCode == 401 || response.statusCode == 403) {
            m_rest.setToken(QString());
            m_events.setToken(QString());
            setAuthState(AuthState::Idle, QStringLiteral("session rejected (%1); run --login")
                                             .arg(response.errorMessage()));
            return;
        }
        if (!response.ok()) {
            // Status 0 is a transport failure; anything else is the server
            // answering. Either way the stored session is left alone.
            setAuthState(AuthState::Failed,
                         response.statusCode == 0
                             ? QStringLiteral("could not reach %1").arg(m_rest.baseUrl())
                             : response.errorMessage());
            return;
        }

        Session session = stored;
        const QJsonObject me = response.json();
        session.userId = jsonString(me, "_id", stored.userId);
        session.lastSeen = QDateTime::currentDateTimeUtc();
        m_session = session;
        m_store.setSelfId(session.userId);
        m_store.upsertUser(parseUser(me));

        setAuthState(AuthState::Authenticated);
        emit authenticated(session.userId);
    });
}

void App::logout() {
    if (!m_session.token.isEmpty()) {
        m_rest.post(QStringLiteral("/auth/session/logout"), {});
    }
    m_events.stop();
    m_store.clear();
    m_fetchedServerChannels.clear();
    m_fetchedServerEmojis.clear();
    SessionStore::remove();
    m_session = Session{};
    m_rest.setToken(QString());
    m_events.setToken(QString());
    setAuthState(AuthState::Idle);
    emit loggedOut();
}

// --- connection --------------------------------------------------------------

void App::openStream() {
    if (m_session.token.isEmpty()) {
        setAuthState(AuthState::Failed, QStringLiteral("no session"));
        return;
    }
    if (m_config.wsUrl.isEmpty()) {
        setAuthState(AuthState::Failed, QStringLiteral("instance not discovered"));
        return;
    }
    m_events.start();
}

void App::closeStream() {
    m_events.stop();
}

void App::applyReady(const QJsonObject& payload) {
    qDebug() << "app: ready users=" << jsonArray(payload, "users").size()
             << "servers=" << jsonArray(payload, "servers").size()
             << "channels=" << jsonArray(payload, "channels").size()
             << "members=" << jsonArray(payload, "members").size()
             << "emojis=" << jsonArray(payload, "emojis").size()
             << "unreads=" << jsonArray(payload, "channel_unreads").size();

    for (const QJsonValue& v : jsonArray(payload, "users")) {
        if (v.isObject()) ingestUser(v.toObject());
    }
    for (const QJsonValue& v : jsonArray(payload, "members")) {
        if (v.isObject()) ingestMember(v.toObject());
    }
    for (const QJsonValue& v : jsonArray(payload, "servers")) {
        if (v.isObject()) ingestServer(v.toObject());
    }
    // Ready's channels are the DMs and groups. Server channels arrive as ids on
    // the server object and are fetched per server below.
    for (const QJsonValue& v : jsonArray(payload, "channels")) {
        if (v.isObject()) ingestChannel(v.toObject());
    }
    for (const QJsonValue& v : jsonArray(payload, "channel_unreads")) {
        if (v.isObject()) m_store.upsertUnread(parseChannelUnread(v.toObject()));
    }

    // DMs are paginated separately from Ready and may not all be present.
    fetchDms();
    fetchUnreads();

    for (Server* server : m_store.servers()) {
        fetchServerChannels(server->id);
        fetchEmojis(server->id);
    }

    // Emitted last, and only here. Events::connected() fires before Events::ready(),
    // so announcing readiness from the transport reported an empty store to a UI
    // that then had nothing to draw until the next event arrived.
    emit ready();
}

void App::ingestUser(const QJsonObject& object) {
    m_store.upsertUser(parseUser(object));
}

void App::ingestMember(const QJsonObject& object) {
    m_store.upsertMember(parseMember(object));
}

void App::ingestServer(const QJsonObject& object) {
    m_store.upsertServer(parseServer(object));
}

void App::ingestChannel(const QJsonObject& object) {
    m_store.upsertChannel(parseChannel(object));
}

void App::absorbBulkPayload(const RestResponse& response, const QString& channelId) {
    // The history endpoint answers in either of two shapes: a bare array of
    // messages, or an object that also carries the users and members those
    // messages reference. Reading one when the other arrived is the classic way
    // to end up with an empty transcript and every author named "unknown".
    const QJsonDocument doc = response.document();
    QJsonArray messages;

    if (doc.isArray()) {
        messages = doc.array();
    } else if (doc.isObject()) {
        const QJsonObject payload = doc.object();
        for (const QJsonValue& v : jsonArray(payload, "users")) {
            if (v.isObject()) ingestUser(v.toObject());
        }
        for (const QJsonValue& v : jsonArray(payload, "members")) {
            if (v.isObject()) ingestMember(v.toObject());
        }
        messages = jsonArray(payload, "messages");
    }

    for (const QJsonValue& v : messages) {
        if (!v.isObject()) continue;
        QJsonObject object = v.toObject();
        if (object.value(QStringLiteral("channel")).isUndefined()) {
            object.insert(QStringLiteral("channel"), channelId);
        }
        ingestMessage(object);
    }
}

void App::ingestMessage(const QJsonObject& object) {
    // Messages can carry their author inline, which is the only way to name an
    // author before a member query has been made for that server.
    const QJsonObject inlineUser = jsonObject(object, "user");
    if (!inlineUser.isEmpty()) ingestUser(inlineUser);
    const QJsonObject inlineMember = jsonObject(object, "member");
    if (!inlineMember.isEmpty()) ingestMember(inlineMember);

    const Message message = parseMessage(object);
    if (message.channelId.isEmpty()) return;

    // A provisional row is re-keyed by nonce rather than duplicated, so the
    // newly arrived copy must not also be inserted.
    if (!message.nonce.isEmpty() && m_store.resolvePending(message.channelId, message.nonce, message)) {
        return;
    }
    m_store.upsertMessage(message);
}

// --- event dispatch ----------------------------------------------------------

void App::dispatch(const QString& type, const QJsonObject& frame) {
    if (type == QLatin1String("Bulk")) {
        handleBulk(jsonArray(frame, "v"));
        return;
    }
    if (type == QLatin1String("Message")) {
        ingestMessage(frame);
        return;
    }
    if (type == QLatin1String("MessageUpdate")) {
        handleMessageUpdate(frame);
        return;
    }
    if (type == QLatin1String("MessageDelete")) {
        m_store.removeMessage(jsonString(frame, "channel"), jsonString(frame, "id"));
        return;
    }
    if (type == QLatin1String("MessageDeleteBulk")) {
        const QString channelId = jsonString(frame, "channel");
        for (const QJsonValue& v : jsonArray(frame, "ids")) {
            m_store.removeMessage(channelId, v.toString());
        }
        return;
    }
    if (type == QLatin1String("MessageReact") || type == QLatin1String("MessageUnreact")) {
        const QString channelId = jsonString(frame, "channel");
        const QString messageId = jsonString(frame, "message");
        const QString emojiId = jsonString(frame, "emoji");
        const QString userId = jsonString(frame, "user");
        const bool add = type == QLatin1String("MessageReact");

        // Null means we do not hold that message -- a different channel, or one the
        // transcript cap trimmed -- and there is nothing to update. Test the pointer
        // itself, not the id on whatever it points at.
        const Message* stored = m_store.message(channelId, messageId);
        if (!stored) return;

        QStringList users = stored->reactions.value(emojiId);
        if (add) {
            if (!users.contains(userId)) users.append(userId);
        } else {
            users.removeAll(userId);
        }
        m_store.setReactions(channelId, messageId, emojiId, users);
        return;
    }
    if (type == QLatin1String("ChannelCreate") || type == QLatin1String("ChannelUpdate")) {
        ingestChannel(frame);
        return;
    }
    if (type == QLatin1String("ChannelDelete")) {
        m_store.removeChannel(jsonString(frame, "id"));
        return;
    }
    if (type == QLatin1String("ServerCreate") || type == QLatin1String("ServerUpdate")) {
        ingestServer(frame);
        return;
    }
    if (type == QLatin1String("ServerDelete") || type == QLatin1String("ServerLeave")) {
        m_store.removeServer(jsonString(frame, "id"));
        return;
    }
    if (type == QLatin1String("ServerMemberJoin") || type == QLatin1String("ServerMemberUpdate")) {
        const QJsonObject member = jsonObject(frame, "member");
        if (!member.isEmpty()) ingestMember(member);
        return;
    }
    if (type == QLatin1String("ServerMemberLeave")) {
        // Members are only held for name and nickname resolution, and the UI
        // re-reads them on demand, so a departure needs no cache eviction.
        return;
    }
    if (type == QLatin1String("UserUpdate")) {
        ingestUser(frame);
        return;
    }
    if (type == QLatin1String("ChannelAck")) {
        m_store.acknowledge(jsonString(frame, "channel"));
        return;
    }
    if (type == QLatin1String("ChannelStartTyping") || type == QLatin1String("ChannelStopTyping")) {
        // Typing is push-only: there is no REST endpoint to announce it, and no
        // timeout hint in the payload, so the UI has to expire these itself.
        const bool start = type == QLatin1String("ChannelStartTyping");
        emit typingChanged(jsonString(frame, "channel"), jsonString(frame, "user"), start);
        return;
    }
    if (type == QLatin1String("PolicyChanges") || type == QLatin1String("UserSettingsUpdate")) {
        return;
    }

    // Unhandled events are normal: this protocol grows, and a client that logged
    // every unknown type would bury the ones that matter.
    qDebug() << "app: unhandled event" << type;
}

void App::handleBulk(const QJsonArray& events) {
    for (const QJsonValue& v : events) {
        if (!v.isObject()) continue;
        const QJsonObject object = v.toObject();
        dispatch(jsonString(object, "type"), object);
    }
}

void App::handleMessageUpdate(const QJsonObject& frame) {
    // Partial update: `data` holds only the changed fields, and `clear` names the
    // fields to blank. Merging naively would drop attachments on every edit.
    const QString channelId = jsonString(frame, "channel");
    const QString messageId = jsonString(frame, "id");
    const QJsonObject data = jsonObject(frame, "data");

    QJsonObject merged = data;
    merged.insert(QStringLiteral("_id"), messageId);
    merged.insert(QStringLiteral("channel"), channelId);
    for (const QJsonValue& v : jsonArray(frame, "clear")) {
        merged.insert(v.toString(), QJsonValue::Null);
    }
    ingestMessage(merged);
}

// --- actions -----------------------------------------------------------------

void App::sendMessage(const QString& channelId, const QString& content, const QString& replyToId) {
    sendMessage(channelId, content, replyToId, {});
}

void App::sendMessage(const QString& channelId, const QString& content, const QString& replyToId,
                      const QStringList& attachmentIds) {
    const QString nonce = makeNonce();

    QJsonObject body;
    body["content"] = content;
    body["nonce"] = nonce;
    if (!attachmentIds.isEmpty()) {
        body["attachments"] = QJsonArray::fromStringList(attachmentIds);
    }
    if (!replyToId.isEmpty()) {
        QJsonObject reply;
        reply["id"] = replyToId;
        reply["mention"] = false;
        // A reply target that vanished (deleted between opening the composer and
        // hitting send) must not discard the message the user actually wrote.
        reply["fail_if_not_exists"] = false;
        body["replies"] = QJsonArray{reply};
    }

    // Show it immediately. The row is keyed on the nonce and re-keyed when the
    // server's copy arrives, so it never appears twice and never jumps.
    Message pending;
    pending.id = QStringLiteral("pending:%1").arg(nonce);
    pending.channelId = channelId;
    pending.authorId = m_store.selfId();
    pending.nonce = nonce;
    pending.content = content;
    pending.replyIds = replyToId.isEmpty() ? QStringList{} : QStringList{replyToId};
    pending.pending = true;
    m_store.upsertMessage(pending);

    m_rest.post(QStringLiteral("/channels/%1/messages").arg(channelId), body,
                [this, channelId, nonce](const RestResponse& response) {
                    if (!response.ok()) {
                        m_store.removeMessage(channelId, QStringLiteral("pending:%1").arg(nonce));
                        emit sendFailed(channelId, nonce, response.errorMessage());
                        return;
                    }
                    ingestMessage(response.json());
                });
}

void App::uploadAttachment(const QString& filename, const QByteArray& data,
                           const QString& contentType, UploadCallback cb) {
    if (data.size() > m_config.maxAttachmentBytes) {
        // Checked here rather than letting the server reject it, so the message
        // names the limit instead of reporting a bare 413.
        cb(QString(), QStringLiteral("file is %1 bytes, limit is %2")
                            .arg(data.size())
                            .arg(m_config.maxAttachmentBytes));
        return;
    }

    const QString url = m_config.cdn.url + QStringLiteral("/attachments");
    m_rest.postFile(QUrl(url), QStringLiteral("file"), filename,
                    contentType.isEmpty() ? QStringLiteral("application/octet-stream") : contentType,
                    data, [this, cb](const RestResponse& response) {
                        if (!response.ok()) {
                            cb(QString(), response.errorMessage());
                            return;
                        }
                        // The upload response keys the id as "id", not "_id" as
                        // every other object in this API does. Accept both rather
                        // than trusting one spelling of a field the server has
                        // already been inconsistent about.
                        const QJsonObject o = response.json();
                        const QString id = jsonString(o, "id", jsonString(o, "_id"));
                        if (id.isEmpty()) {
                            cb(QString(), QStringLiteral("upload returned no attachment id"));
                            return;
                        }
                        cb(id, QString());
                    });
}

void App::editMessage(const QString& channelId, const QString& messageId, const QString& content) {
    QJsonObject body;
    body["content"] = content;
    m_rest.patch(QStringLiteral("/channels/%1/messages/%2").arg(channelId, messageId), body,
                 [this, channelId, messageId](const RestResponse& response) {
                     if (!response.ok()) {
                         emit error(QStringLiteral("edit failed: ") + response.errorMessage());
                         return;
                     }
                     ingestMessage(response.json());
                     Q_UNUSED(channelId);
                     Q_UNUSED(messageId);
                 });
}

void App::deleteMessage(const QString& channelId, const QString& messageId) {
    m_rest.del(QStringLiteral("/channels/%1/messages/%2").arg(channelId, messageId),
               [this, channelId, messageId](const RestResponse& response) {
                   if (!response.ok()) {
                       emit error(QStringLiteral("delete failed: ") + response.errorMessage());
                       return;
                   }
                   m_store.removeMessage(channelId, messageId);
               });
}

void App::setPinned(const QString& channelId, const QString& messageId, bool pinned) {
    const QString path = QStringLiteral("/channels/%1/messages/%2/pin").arg(channelId, messageId);
    auto cb = [this, channelId, messageId, pinned](const RestResponse& response) {
        if (!response.ok()) {
            emit error(QStringLiteral("pin failed: ") + response.errorMessage());
            return;
        }
        const Message* stored = m_store.message(channelId, messageId);
        if (!stored) return;
        Message message = *stored;
        message.pinned = pinned;
        m_store.upsertMessage(message);
    };

    if (pinned) {
        m_rest.post(path, {}, cb);
    } else {
        m_rest.del(path, cb);
    }
}

void App::addReaction(const QString& channelId, const QString& messageId, const QString& emojiId) {
    m_rest.put(QStringLiteral("/channels/%1/messages/%2/reactions/%3")
                   .arg(channelId, messageId, RestClient::encode(emojiId)),
               {}, [this, channelId, messageId, emojiId](const RestResponse& response) {
                   if (!response.ok()) {
                       emit error(QStringLiteral("react failed: ") + response.errorMessage());
                       return;
                   }
                    const QString self = m_store.selfId();
                    if (!m_store.message(channelId, messageId)) return;
                    QStringList users = m_store.message(channelId, messageId)->reactions.value(emojiId);
                    if (!users.contains(self)) users.append(self);
                    m_store.setReactions(channelId, messageId, emojiId, users);
                });
}


void App::removeReaction(const QString& channelId, const QString& messageId, const QString& emojiId,
                         const QString& userId) {
    QString path = QStringLiteral("/channels/%1/messages/%2/reactions/%3")
                       .arg(channelId, messageId, RestClient::encode(emojiId));
    if (!userId.isEmpty()) {
        path += QStringLiteral("?user_id=") + RestClient::encode(userId);
    }

    m_rest.del(path, [this, channelId, messageId, emojiId, userId](const RestResponse& response) {
        if (!response.ok()) {
            emit error(QStringLiteral("unreact failed: ") + response.errorMessage());
            return;
        }
        const QString target = userId.isEmpty() ? m_store.selfId() : userId;
        const Message* stored = m_store.message(channelId, messageId);
        if (!stored) return;

        QStringList users = stored->reactions.value(emojiId);
        users.removeAll(target);
        m_store.setReactions(channelId, messageId, emojiId, users);
    });
}

void App::acknowledge(const QString& channelId, const QString& messageId) {
    if (messageId.isEmpty()) return;
    m_rest.put(QStringLiteral("/channels/%1/ack/%2").arg(channelId, messageId), {});
    m_store.acknowledge(channelId);
}

// --- fetching ----------------------------------------------------------------

void App::fetchMessages(const QString& channelId, int limit) {
    m_rest.get(messagesPath(channelId, limit, {}, true),
               [this, channelId](const RestResponse& response) {
                   if (!response.ok()) {
                       emit error(QStringLiteral("history failed: ") + response.errorMessage());
                       return;
                   }
                   absorbBulkPayload(response, channelId);
               });
}

void App::fetchOlderMessages(const QString& channelId, int limit) {
    const QString before = m_store.oldestMessageId(channelId);
    if (before.isEmpty()) {
        fetchMessages(channelId, limit);
        return;
    }
    m_rest.get(messagesPath(channelId, limit, before, false),
               [this, channelId](const RestResponse& response) {
                   if (!response.ok()) {
                       emit error(QStringLiteral("history failed: ") + response.errorMessage());
                       return;
                   }
                   absorbBulkPayload(response, channelId);
               });
}

void App::fetchDms() {
    m_rest.get(QStringLiteral("/users/dms"), [this](const RestResponse& response) {
        if (!response.ok()) {
            if (response.statusCode != 401) {
                emit error(QStringLiteral("dms failed: ") + response.errorMessage());
            }
            return;
        }
        for (const QJsonValue& v : response.array()) {
            if (v.isObject()) ingestChannel(v.toObject());
        }
    });
}

void App::fetchUnreads() {
    m_rest.get(QStringLiteral("/sync/unreads"), [this](const RestResponse& response) {
        if (!response.ok()) return;
        for (const QJsonValue& v : response.array()) {
            if (v.isObject()) m_store.upsertUnread(parseChannelUnread(v.toObject()));
        }
    });
}

void App::fetchServerChannels(const QString& serverId) {
    if (m_fetchedServerChannels.contains(serverId)) return;
    m_fetchedServerChannels.append(serverId);

    m_rest.get(QStringLiteral("/servers/%1?include_channels=true").arg(serverId),
               [this, serverId](const RestResponse& response) {
                   if (!response.ok()) {
                       // Allow a retry: a transient failure here leaves the server
                       // permanently empty in the sidebar otherwise.
                       m_fetchedServerChannels.removeAll(serverId);
                       emit error(QStringLiteral("server fetch failed: ") + response.errorMessage());
                       return;
                   }
                   const QJsonObject o = response.json();
                   for (const QJsonValue& v : jsonArray(o, "channels")) {
                       if (v.isObject()) ingestChannel(v.toObject());
                   }
                   // The server object itself is refetched so categories, icon and
                   // member count are current rather than whatever Ready carried.
                   ingestServer(jsonObject(o, "server").isEmpty() ? o : jsonObject(o, "server"));
                   Q_UNUSED(serverId);
               });
}

void App::fetchEmojis(const QString& serverId) {
    // Reactions in this API are server emoji ids, so the set is required before a
    // reaction can be chosen at all. Fetched once per server.
    if (m_fetchedServerEmojis.contains(serverId)) return;
    m_fetchedServerEmojis.insert(serverId);
    m_rest.get(QStringLiteral("/servers/%1/emojis").arg(serverId),
               [this, serverId](const RestResponse& response) {
        if (!response.ok()) {
            qDebug() << "app: emoji fetch failed:" << response.errorMessage();
            return;
        }
        // Stored, not discarded: a reaction pill is drawn from an emoji id, so without
        // this there is nothing to resolve one against.
        int kept = 0;
        for (const QJsonValue& value : response.array()) {
            if (!value.isObject()) continue;
            const Emoji emoji = parseEmoji(value.toObject());
            if (emoji.id.isEmpty() || emoji.name.isEmpty()) continue;
            m_store.upsertEmoji(emoji);
            ++kept;
        }
        qDebug() << "app: emoji for server" << serverId << kept;
    });
}

} // namespace nimbus
