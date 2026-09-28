#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>

// Domain types for Stoat (delta). Ids are opaque strings, so every id is a
// QString and nothing is parsed numerically. Entity ids are 26-character ULIDs
// and sort chronologically as text, which is what lets message ordering and
// pagination cursors work with plain string comparison. Attachment ids are not
// ULIDs -- the upload endpoint returns a 44-character opaque string -- so nothing
// may assume a fixed id length.

namespace nimbus {

// The four variants in the Channel oneOf. Anything else is Unknown, which the
// UI renders as an unselectable row rather than guessing.
enum class ChannelType {
    Unknown,
    SavedMessages,
    DirectMessage,
    Group,
    TextChannel,
};

enum class Relationship {
    None,
    User,
    Friend,
    Outgoing,
    Incoming,
    Blocked,
    BlockedOther,
};

// Stoat's MediaProxy. Anything the server hands us as an id resolves against
// this host with /{tag}/{id}, not against the API host.
struct CdnConfig {
    QString url = "https://cdn.stoatusercontent.com";
    bool enabled = true;
};

struct File {
    QString id;
    QString tag;
    QString filename;
    QString contentType;
    qint64 size = 0;
    int width = 0;
    int height = 0;
    bool animated = false;

    bool isImage() const { return width > 0 && height > 0; }
    bool deleted = false;

    // Filename with the extension the server actually stored, which is not
    // always the one the uploader sent.
    QString displayName() const;
    QString url(const CdnConfig& cdn) const;
};

struct User {
    QString id;
    QString username;
    QString discriminator;
    QString displayName;
    QString pronouns;
    // Whole File, not just the id: the CDN path is /{tag}/{id}, so neither half
    // alone can be turned into a URL.
    File avatar;
    bool online = false;
    bool bot = false;
    Relationship relationship = Relationship::None;

    // There is no discriminator to append: the handle is just the username.
    // Displaying it is what keeps an account from looking half-registered.
    QString handle() const { return username; }
    QString monogram() const;
};

struct Member {
    QString serverId;
    QString userId;
    QString nickname;
    QString pronouns;
    File avatar;
    QStringList roles;
    QDateTime joinedAt;

    QString compositeId() const { return serverId + QLatin1Char(':') + userId; }
    bool operator==(const Member& o) const { return serverId == o.serverId && userId == o.userId; }
};

struct Category {
    QString id;
    QString title;
    QStringList channelIds;
};

struct Server {
    QString id;
    QString ownerId;
    QString name;
    QString description;
    File icon;
    File banner;
    QStringList channelIds; // authoritative order; categories re-partition it
    QVector<Category> categories;
    qint64 approximateMemberCount = 0;
    bool nsfw = false;

    // Category that owns a channel, or empty for uncategorised ones. Resolved
    // from the category list rather than stored, so a category rename does not
    // require rewriting every channel row.
    QString categoryIdFor(const QString& channelId) const;
};

struct Channel {
    QString id;
    ChannelType type = ChannelType::Unknown;
    QString serverId; // empty for DMs and groups
    QString categoryId;
    QString name;
    QString description;
    File icon;
    QString ownerId;
    QStringList recipients; // user ids; 2 for a DM
    QString lastMessageId;
    bool nsfw = false;
    qint64 slowmode = 0;
    bool voice = false; // a TextChannel may carry voice; there is no Voice type

    bool isDirect() const { return type == ChannelType::DirectMessage; }
    bool isGroup() const { return type == ChannelType::Group; }
    // Text, DM and group all accept messages; SavedMessages is a private notepad.
    bool acceptsMessages() const { return type != ChannelType::Unknown && type != ChannelType::SavedMessages; }
};

struct Embed {
    QString type; // Website, Image, Video, Audio, Text
    QString title;
    QString description;
    QString url;
    QString siteName;
    QString iconUrl;
    QString imageUrl;
    QString mediaUrl;
    QString colour; // CSS colour string, not a packed int
    int width = 0;
    int height = 0;
};

struct Emoji {
    QString id;
    // Bare shortcode, e.g. "neko_sad". Sent without colons.
    QString name;
    QString serverId;
    // Server emotes are usually animated, and the asset is a GIF.
    bool animated = false;

    // The emote is a custom image, but the payload carries no file: the CDN addresses
    // it by id alone under the "emojis" variant and redirects to the real filename.
    // Verified against the live host -- /emojis/{id} 308s to e.g.
    // a_cute_neko_thinking_yumeusa.gif, 96x96.
    QString url(const CdnConfig& cdn) const;

    // What the composer inserts.
    QString shortcode() const {
        return name.isEmpty() ? QString() : QLatin1Char(':') + name + QLatin1Char(':');
    }
};

struct Masquerade {
    QString name;
    QString avatarId;
    QString colour;
    bool active = false;
};

struct Message {
    QString id;
    QString channelId;
    QString authorId;
    QString nonce; // client-generated, echoed back for optimistic sends
    QString content;
    QDateTime edited;
    QVector<File> attachments;
    QVector<Embed> embeds;
    QStringList mentions;
    QStringList roleMentions;
    QStringList replyIds; // ids of the messages being replied to
    Masquerade masquerade;
    QJsonObject system; // non-empty for join/leave/pin notices
    QHash<QString, QStringList> reactions; // emoji id -> user ids
    bool pinned = false;
    int flags = 0;

    // A locally-composed message that the server has not acknowledged. Its id is
    // a "pending:" placeholder until the real ULID arrives and is matched by
    // nonce. Without this the transcript would either show nothing until the
    // round trip or duplicate the message once it does.
    bool pending = false;

    bool isSystem() const { return !system.isEmpty(); }
    int reactionCount(const QString& emoji) const { return reactions.value(emoji).size(); }
    bool reactedBy(const QString& emoji, const QString& userId) const {
        return reactions.value(emoji).contains(userId);
    }
};

struct ChannelUnread {
    QString channelId;
    QString lastMessageId;
    QStringList mentions;
};

// The root document served by GET / on the API host. Discovered at runtime so a
// self-hosted instance needs no recompile.
struct InstanceConfig {
    QString wsUrl;
    QString appUrl;
    QString apiVersion;
    CdnConfig cdn;
    // january is the embed proxy; absent means we render no link previews.
    QString januaryUrl;
    int maxMessageLength = 2000;
    qint64 maxAttachmentBytes = 20 * 1000 * 1000;
    int maxAttachmentsPerMessage = 5;
};

// --- parsing -----------------------------------------------------------------

User parseUser(const QJsonObject& o);
Member parseMember(const QJsonObject& o);
File parseFile(const QJsonObject& o);
Channel parseChannel(const QJsonObject& o);
Server parseServer(const QJsonObject& o);
Emoji parseEmoji(const QJsonObject& o);
Message parseMessage(const QJsonObject& o);
Embed parseEmbed(const QJsonObject& o);
ChannelUnread parseChannelUnread(const QJsonObject& o);
InstanceConfig parseInstanceConfig(const QJsonObject& o);

ChannelType channelTypeFromName(const QString& name);
QString channelTypeName(ChannelType type);
Relationship relationshipFromName(const QString& name);

} // namespace nimbus
