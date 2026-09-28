#include "store.h"

#include <QtCore/QStringList>

#include <algorithm>

namespace nimbus {

Store::Store(QObject* parent)
    : QObject(parent) {}

void Store::clear() {
    m_emojis.clear();
    m_servers.clear();
    m_serverOrder.clear();
    m_channels.clear();
    m_users.clear();
    m_members.clear();
    m_unreads.clear();
}

// --- servers -----------------------------------------------------------------

void Store::upsertServer(const Server& server) {
    if (server.id.isEmpty()) return;
    const bool isNew = !m_servers.contains(server.id);

    Server merged = server;
    // An update that omits the channel list must not erase the one already known,
    // or the server silently loses every channel in the sidebar.
    if (merged.channelIds.isEmpty()) {
        const auto existing = m_servers.constFind(server.id);
        if (existing != m_servers.constEnd()) merged.channelIds = existing->channelIds;
    }
    m_servers.insert(server.id, merged);

    if (isNew) {
        m_serverOrder.append(server.id);
        emit serverAdded(server.id);
    } else {
        emit serverUpdated(server.id);
    }

    // Categories partition the server's channels, so the derived field is
    // refreshed here rather than being re-derived by every reader.
    for (const QString& channelId : merged.channelIds) {
        auto it = m_channels.find(channelId);
        if (it == m_channels.end()) continue;
        const QString categoryId = merged.categoryIdFor(channelId);
        if (it->channel.categoryId != categoryId) {
            it->channel.categoryId = categoryId;
            emit channelUpdated(channelId);
        }
    }
}

void Store::removeServer(const QString& id) {
    if (!m_servers.remove(id)) return;
    m_serverOrder.removeAll(id);
    m_members.remove(id);
    for (auto it = m_channels.begin(); it != m_channels.end();) {
        if (it->channel.serverId == id) {
            it = m_channels.erase(it);
        } else {
            ++it;
        }
    }
    emit serverRemoved(id);
}

Server* Store::server(const QString& id) {
    const auto it = m_servers.find(id);
    return it == m_servers.end() ? nullptr : &it.value();
}

const Server* Store::server(const QString& id) const {
    const auto it = m_servers.constFind(id);
    return it == m_servers.constEnd() ? nullptr : &it.value();
}

QList<Server*> Store::servers() {
    QList<Server*> out;
    out.reserve(m_serverOrder.size());
    for (const QString& id : m_serverOrder) {
        if (Server* s = server(id)) out.append(s);
    }
    return out;
}

// --- channels ----------------------------------------------------------------

void Store::upsertChannel(const Channel& channel) {
    if (channel.id.isEmpty()) return;
    const bool isNew = !m_channels.contains(channel.id);
    ChannelData& data = m_channels[channel.id];
    data.channel = channel;

    // A DM or group channel carries no server, so its category is always empty;
    // a server channel resolves against the server we may not have yet.
    if (!channel.serverId.isEmpty()) {
        const Server* s = server(channel.serverId);
        if (s) data.channel.categoryId = s->categoryIdFor(channel.id);
    }

    if (isNew) {
        emit channelAdded(channel.id);
    } else {
        emit channelUpdated(channel.id);
    }
}

void Store::removeChannel(const QString& id) {
    if (!m_channels.remove(id)) return;
    m_unreads.remove(id);
    emit channelRemoved(id);
}

Channel* Store::channel(const QString& id) {
    const auto it = m_channels.find(id);
    return it == m_channels.end() ? nullptr : &it->channel;
}

const Channel* Store::channel(const QString& id) const {
    const auto it = m_channels.constFind(id);
    return it == m_channels.constEnd() ? nullptr : &it->channel;
}

QList<Channel*> Store::channelsForServer(const QString& serverId) {
    QList<Channel*> out;
    const Server* s = server(serverId);
    if (!s) return out;

    // Server.channelIds is authoritative for both membership and order, so a
    // channel that exists but is not listed is not shown.
    for (const QString& channelId : s->channelIds) {
        const auto it = m_channels.find(channelId);
        if (it != m_channels.end()) out.append(&it->channel);
    }
    return out;
}

QList<Channel*> Store::directChannels() {
    QList<Channel*> out;
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        if (it->channel.isDirect() || it->channel.isGroup()) {
            out.append(&it->channel);
        }
    }
    // Newest activity first. The id tiebreak is not cosmetic: channels that have
    // never had a message all compare equal on lastMessageId, and without it their
    // order came from QHash iteration, which is unspecified and changed between
    // rebuilds -- so the DM list visibly reshuffled on every store update.
    std::sort(out.begin(), out.end(), [](const Channel* a, const Channel* b) {
        if (a->lastMessageId != b->lastMessageId) return a->lastMessageId > b->lastMessageId;
        return a->id < b->id;
    });
    return out;
}

QList<Channel*> Store::channelsOfType(ChannelType type) {
    QList<Channel*> out;
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        if (it->channel.type == type) out.append(&it->channel);
    }
    return out;
}

// --- users & members ---------------------------------------------------------

void Store::upsertUser(const User& user) {
    if (user.id.isEmpty()) return;
    // Ready omits most user fields and message payloads carry a trimmed copy, so
    // an upsert must not blank fields the previous, fuller payload had.
    const auto it = m_users.constFind(user.id);
    if (it != m_users.constEnd()) {
        User merged = *it;
        if (!user.username.isEmpty()) merged.username = user.username;
        if (!user.discriminator.isEmpty()) merged.discriminator = user.discriminator;
        if (!user.displayName.isEmpty()) merged.displayName = user.displayName;
        if (!user.pronouns.isEmpty()) merged.pronouns = user.pronouns;
        if (!user.avatar.id.isEmpty()) merged.avatar = user.avatar;
        if (user.bot) merged.bot = true;
        merged.online = user.online;
        if (user.relationship != Relationship::None) merged.relationship = user.relationship;
        m_users.insert(user.id, merged);
        emit userChanged(user.id);
        return;
    }
    m_users.insert(user.id, user);
    emit userChanged(user.id);
}

const User* Store::user(const QString& id) const {
    const auto it = m_users.constFind(id);
    return it == m_users.constEnd() ? nullptr : &it.value();
}

void Store::upsertMember(const Member& member) {
    if (member.serverId.isEmpty() || member.userId.isEmpty()) return;
    m_members[member.serverId].insert(member.userId, member);
}

const Member* Store::member(const QString& serverId, const QString& userId) const {
    const auto serverIt = m_members.constFind(serverId);
    if (serverIt == m_members.constEnd()) return nullptr;
    const auto it = serverIt.value().constFind(userId);
    return it == serverIt.value().constEnd() ? nullptr : &it.value();
}

// --- messages ----------------------------------------------------------------

void Store::trimMessages(ChannelData& data) {
    if (data.messages.size() <= kMaxMessagesPerChannel) return;
    data.messages.remove(0, data.messages.size() - kMaxMessagesPerChannel);
}

void Store::upsertMessage(const Message& message) {
    if (message.channelId.isEmpty() || message.id.isEmpty()) return;
    ChannelData& data = m_channels[message.channelId];

    // An edit arrives with only the changed fields, so merge rather than replace
    // or an edited message loses its attachments and replies.
    for (Message& existing : data.messages) {
        if (existing.id != message.id) continue;
        if (!message.content.isEmpty() || message.edited.isValid()) {
            existing.content = message.content;
            existing.edited = message.edited;
        }
        if (!message.attachments.isEmpty()) existing.attachments = message.attachments;
        if (!message.embeds.isEmpty()) existing.embeds = message.embeds;
        if (!message.reactions.isEmpty()) existing.reactions = message.reactions;
        existing.pinned = message.pinned;
        if (!message.replyIds.isEmpty()) existing.replyIds = message.replyIds;
        existing.pending = false;
        emit messageUpdated(message.channelId, message.id);
        return;
    }

    const auto position = std::lower_bound(
        data.messages.begin(), data.messages.end(), message.id,
        [](const Message& m, const QString& id) { return compareIds(m.id, id) < 0; });

    // A pending row is always the newest thing in the channel, and a late-arriving
    // older message must land before it rather than after.
    const bool append = position == data.messages.end() ||
                        (position->pending && !message.pending);
    if (append) {
        data.messages.append(message);
    } else {
        data.messages.insert(position, message);
    }

    trimMessages(data);
    emit messageAdded(message.channelId, message.id);
}

void Store::upsertEmoji(const Emoji& emoji) {
    if (emoji.id.isEmpty() || emoji.name.isEmpty()) return;
    m_emojis.insert(emoji.id, emoji);
    emit emojisChanged(emoji.serverId);
}

const Emoji* Store::emoji(const QString& id) const {
    const auto it = m_emojis.constFind(id);
    return it == m_emojis.constEnd() ? nullptr : &it.value();
}

QList<Emoji> Store::emojisForServer(const QString& serverId) {
    QList<Emoji> out;
    for (const Emoji& emoji : m_emojis) {
        if (emoji.serverId == serverId) out.append(emoji);
    }
    // Sorted, because QHash iteration order is unspecified and an unsorted list
    // would give the picker a different order every time it was opened.
    std::sort(out.begin(), out.end(),
              [](const Emoji& a, const Emoji& b) { return a.name < b.name; });
    return out;
}

void Store::setReactions(const QString& channelId, const QString& messageId, const QString& emojiId,
                         const QStringList& users) {
    if (emojiId.isEmpty()) return;

    const auto channel = m_channels.find(channelId);
    if (channel == m_channels.end()) return;

    for (Message& message : channel->messages) {
        if (message.id != messageId) continue;
        if (users.isEmpty()) {
            message.reactions.remove(emojiId);
        } else {
            message.reactions.insert(emojiId, users);
        }
        emit messageUpdated(channelId, messageId);
        return;
    }
}

void Store::removeMessage(const QString& channelId, const QString& messageId) {
    const auto channelIt = m_channels.find(channelId);
    if (channelIt == m_channels.end()) return;
    for (int i = 0; i < channelIt->messages.size(); ++i) {
        if (channelIt->messages[i].id != messageId) continue;
        channelIt->messages.remove(i);
        emit messageRemoved(channelId, messageId);
        return;
    }
}

const Message* Store::message(const QString& channelId, const QString& messageId) const {
    const auto channelIt = m_channels.constFind(channelId);
    if (channelIt == m_channels.constEnd()) return nullptr;
    for (const Message& m : channelIt->messages) {
        if (m.id == messageId) return &m;
    }
    return nullptr;
}

QVector<const Message*> Store::messages(const QString& channelId) const {
    QVector<const Message*> out;
    const auto it = m_channels.constFind(channelId);
    if (it == m_channels.constEnd()) return out;
    out.reserve(it->messages.size());
    for (const Message& m : it->messages) out.append(&m);
    return out;
}

int Store::messageCount(const QString& channelId) const {
    const auto it = m_channels.constFind(channelId);
    return it == m_channels.constEnd() ? 0 : int(it->messages.size());
}

QString Store::oldestMessageId(const QString& channelId) const {
    const auto it = m_channels.constFind(channelId);
    if (it == m_channels.constEnd() || it->messages.isEmpty()) return {};
    return it->messages.first().id;
}

bool Store::resolvePending(const QString& channelId, const QString& nonce, const Message& confirmed) {
    if (nonce.isEmpty()) return false;
    const auto it = m_channels.find(channelId);
    if (it == m_channels.end()) return false;

    for (int i = 0; i < it->messages.size(); ++i) {
        if (!it->messages[i].pending || it->messages[i].nonce != nonce) continue;
        // Keep the row in place and re-key it, so the transcript does not jump
        // and no selection or scroll anchor tied to the row index is lost.
        it->messages[i].id = confirmed.id;
        it->messages[i].pending = false;
        it->messages[i].nonce = nonce;
        if (!confirmed.content.isEmpty()) it->messages[i].content = confirmed.content;
        if (confirmed.edited.isValid()) it->messages[i].edited = confirmed.edited;
        if (!confirmed.attachments.isEmpty()) it->messages[i].attachments = confirmed.attachments;
        if (!confirmed.embeds.isEmpty()) it->messages[i].embeds = confirmed.embeds;
        if (!confirmed.reactions.isEmpty()) it->messages[i].reactions = confirmed.reactions;
        emit messageUpdated(channelId, confirmed.id);
        return true;
    }
    return false;
}

// --- unreads -----------------------------------------------------------------

void Store::upsertUnread(const ChannelUnread& unread) {
    if (unread.channelId.isEmpty()) return;
    m_unreads.insert(unread.channelId, unread);
    emit unreadChanged(unread.channelId);
}

const ChannelUnread* Store::unread(const QString& channelId) const {
    const auto it = m_unreads.constFind(channelId);
    return it == m_unreads.constEnd() ? nullptr : &it.value();
}

int Store::mentionCount(const QString& channelId) const {
    const ChannelUnread* u = unread(channelId);
    return u ? int(u->mentions.size()) : 0;
}

void Store::acknowledge(const QString& channelId) {
    const auto it = m_unreads.find(channelId);
    if (it == m_unreads.end() || it->mentions.isEmpty()) return;
    it->mentions.clear();
    emit unreadChanged(channelId);
}

} // namespace nimbus
