#pragma once

#include "models.h"

#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>

namespace nimbus {

// In-memory cache of everything Ready gave us plus everything that arrived since.
// All access is main-thread only; the transport and REST calls are async, so
// nothing needs locking and no signal is ever emitted from another thread.
class Store : public QObject {
    Q_OBJECT
public:
    explicit Store(QObject* parent = nullptr);

    QString selfId() const { return m_selfId; }
    void setSelfId(const QString& id) { m_selfId = id; }

    // The CDN host, needed to turn an emoji id into an image url. Held here rather
    // than passed to each delegate because the store is what the models read.
    const CdnConfig& cdn() const { return m_cdn; }
    void setCdn(const CdnConfig& cdn) { m_cdn = cdn; }

    // --- servers ---
    void upsertServer(const Server& server);
    void removeServer(const QString& id);
    Server* server(const QString& id);
    const Server* server(const QString& id) const;
    // Insertion order, which is the order Ready listed them in and the only
    // ordering the API offers.
    QList<Server*> servers();

    // --- channels ---
    void upsertChannel(const Channel& channel);
    void removeChannel(const QString& id);
    Channel* channel(const QString& id);
    const Channel* channel(const QString& id) const;
    QList<Channel*> channelsForServer(const QString& serverId);
    QList<Channel*> directChannels();
    // SavedMessages is the account's private notepad and belongs in the server
    // rail as a home entry, not in the DM list, so it is fetched separately.
    QList<Channel*> channelsOfType(ChannelType type);

    // --- users & members ---
    void upsertUser(const User& user);
    const User* user(const QString& id) const;
    void upsertMember(const Member& member);
    const Member* member(const QString& serverId, const QString& userId) const;

    // --- messages ---
    // Stored oldest-first, which is the order a transcript reads in and the
    // order a Qt list model indexes without a reversal at paint time.
    void upsertMessage(const Message& message);
    void removeMessage(const QString& channelId, const QString& messageId);
    const Message* message(const QString& channelId, const QString& messageId) const;
    QVector<const Message*> messages(const QString& channelId) const;
    int messageCount(const QString& channelId) const;
    // Newest message id, used as the pagination cursor. Empty when the channel
    // has never been loaded.
    QString oldestMessageId(const QString& channelId) const;

    // Replaces the pending row that carries `nonce`, if any.
    bool resolvePending(const QString& channelId, const QString& nonce, const Message& confirmed);

    // Replaces the user set for one emoji, removing the emoji entirely when the set
    // is empty. The reaction paths know the complete set and must not route through
    // upsertMessage: that merges partially, and it reads an empty map as "field not
    // supplied", so un-reacting the last person would keep the pill forever.
    void setReactions(const QString& channelId, const QString& messageId, const QString& emojiId,
                      const QStringList& users);

    // --- emoji ---
    // Reactions arrive as emoji ids, so a pill cannot be drawn without this.
    void upsertEmoji(const Emoji& emoji);
    const Emoji* emoji(const QString& id) const;
    // Sorted by name, so the picker does not reshuffle between openings.
    QList<Emoji> emojisForServer(const QString& serverId);
    int emojiCount() const { return m_emojis.size(); }

    // --- unreads ---
    void upsertUnread(const ChannelUnread& unread);
    const ChannelUnread* unread(const QString& channelId) const;
    int mentionCount(const QString& channelId) const;
    void acknowledge(const QString& channelId);

    void clear();

signals:
    void serverAdded(const QString& serverId);
    void serverUpdated(const QString& serverId);
    void serverRemoved(const QString& serverId);
    void channelAdded(const QString& channelId);
    void channelUpdated(const QString& channelId);
    void channelRemoved(const QString& channelId);
    void messageAdded(const QString& channelId, const QString& messageId);
    void messageUpdated(const QString& channelId, const QString& messageId);
    void messageRemoved(const QString& channelId, const QString& messageId);
    void unreadChanged(const QString& channelId);
    // A user can arrive after the channel that names them -- a DM whose counterpart
    // is not in Ready -- and the rail and the DM list both have to re-derive the
    // name and avatar they were showing as a truncated id.
    void userChanged(const QString& userId);
    void emojisChanged(const QString& serverId);

private:
    struct ChannelData {
        Channel channel;
        QVector<Message> messages;
    };

    // ULIDs sort chronologically as text, so ordering is a string comparison and
    // pagination is "everything before this id" with no date maths.
    static int compareIds(const QString& a, const QString& b) { return a.compare(b); }

    void trimMessages(ChannelData& data);

    QHash<QString, Server> m_servers;
    QStringList m_serverOrder;
    QHash<QString, ChannelData> m_channels;
    QHash<QString, User> m_users;
    QHash<QString, QHash<QString, Member>> m_members; // serverId -> userId -> member
    QHash<QString, ChannelUnread> m_unreads;
    QHash<QString, Emoji> m_emojis; // id -> emoji
    QString m_selfId;
    CdnConfig m_cdn;

    // The API states no cap, but an unbounded transcript is a memory leak with a
    // slow onset: a busy channel grows without bound for the life of the process.
    static const int kMaxMessagesPerChannel = 500;
};

} // namespace nimbus
