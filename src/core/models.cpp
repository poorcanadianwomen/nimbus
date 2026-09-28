#include "models.h"

#include "json.h"

#include <QtCore/QJsonArray>

namespace nimbus {

namespace {

// Attachment paths on the CDN are id/tag, not the filename, so a rename does not
// invalidate every cached URL.
QString cdnPath(const File& f) {
    // /{tag}/{id}, where tag is the variant: avatars, attachments, emojis, icons,
    // backgrounds, banners. Verified against the live CDN -- the server rejects an
    // unknown variant with a 400 that lists them, and any other ordering is a 404.
    if (f.id.isEmpty() || f.tag.isEmpty()) return {};
    return QStringLiteral("/%1/%2").arg(f.tag, f.id);
}

} // namespace

QString File::displayName() const {
    return filename.isEmpty() ? id : filename;
}

QString File::url(const CdnConfig& cdn) const {
    const QString path = cdnPath(*this);
    if (path.isEmpty()) return {};
    return cdn.url + path;
}

QString User::monogram() const {
    const QString source = displayName.isEmpty() ? username : displayName;
    if (source.isEmpty()) return QStringLiteral("?");
    return source.at(0).toUpper();
}

QString Server::categoryIdFor(const QString& channelId) const {
    for (const Category& c : categories) {
        if (c.channelIds.contains(channelId)) return c.id;
    }
    return {};
}

ChannelType channelTypeFromName(const QString& name) {
    if (name == QLatin1String("SavedMessages")) return ChannelType::SavedMessages;
    if (name == QLatin1String("DirectMessage")) return ChannelType::DirectMessage;
    if (name == QLatin1String("Group")) return ChannelType::Group;
    if (name == QLatin1String("TextChannel")) return ChannelType::TextChannel;
    return ChannelType::Unknown;
}

QString channelTypeName(ChannelType type) {
    switch (type) {
        case ChannelType::SavedMessages: return QStringLiteral("SavedMessages");
        case ChannelType::DirectMessage: return QStringLiteral("DirectMessage");
        case ChannelType::Group: return QStringLiteral("Group");
        case ChannelType::TextChannel: return QStringLiteral("TextChannel");
        case ChannelType::Unknown: break;
    }
    return QStringLiteral("Unknown");
}

Relationship relationshipFromName(const QString& name) {
    if (name == QLatin1String("User")) return Relationship::User;
    if (name == QLatin1String("Friend")) return Relationship::Friend;
    if (name == QLatin1String("Outgoing")) return Relationship::Outgoing;
    if (name == QLatin1String("Incoming")) return Relationship::Incoming;
    if (name == QLatin1String("Blocked")) return Relationship::Blocked;
    if (name == QLatin1String("BlockedOther")) return Relationship::BlockedOther;
    return Relationship::None;
}

// --- users -------------------------------------------------------------------

User parseUser(const QJsonObject& o) {
    User u;
    u.id = jsonString(o, "_id");
    u.username = jsonString(o, "username");
    u.discriminator = jsonString(o, "discriminator");
    u.displayName = jsonString(o, "display_name");
    u.pronouns = jsonString(o, "pronouns");
    if (!jsonObject(o, "avatar").isEmpty()) u.avatar = parseFile(jsonObject(o, "avatar"));
    u.online = jsonBool(o, "online");
    u.relationship = relationshipFromName(jsonString(o, "relationship"));
    u.bot = !jsonObject(o, "bot").isEmpty();
    return u;
}

Member parseMember(const QJsonObject& o) {
    const QJsonObject key = jsonObject(o, "_id");
    Member m;
    m.serverId = jsonString(key, "server");
    m.userId = jsonString(key, "user");
    m.nickname = jsonString(o, "nickname");
    m.pronouns = jsonString(o, "pronouns");
    if (!jsonObject(o, "avatar").isEmpty()) m.avatar = parseFile(jsonObject(o, "avatar"));
    m.joinedAt = QDateTime::fromString(jsonString(o, "joined_at"), Qt::ISODate);
    for (const QJsonValue& v : jsonArray(o, "roles")) {
        m.roles << v.toString();
    }
    return m;
}

File parseFile(const QJsonObject& o) {
    File f;
    f.id = jsonString(o, "_id");
    f.tag = jsonString(o, "tag");
    f.filename = jsonString(o, "filename");
    f.contentType = jsonString(o, "content_type");
    f.size = jsonInt(o, "size");
    f.deleted = jsonBool(o, "deleted");

    const QJsonObject md = jsonObject(o, "metadata");
    const QString type = jsonString(md, "type");
    if (type == QLatin1String("Image") || type == QLatin1String("Video")) {
        f.width = int(jsonInt(md, "width"));
        f.height = int(jsonInt(md, "height"));
        f.animated = jsonBool(md, "animated");
    }
    return f;
}

// --- channels ----------------------------------------------------------------

Channel parseChannel(const QJsonObject& o) {
    Channel c;
    c.id = jsonString(o, "_id");
    c.type = channelTypeFromName(jsonString(o, "channel_type"));
    c.serverId = jsonString(o, "server");
    c.name = jsonString(o, "name");
    c.description = jsonString(o, "description");
    if (!jsonObject(o, "icon").isEmpty()) c.icon = parseFile(jsonObject(o, "icon"));
    c.ownerId = jsonString(o, "owner");
    c.lastMessageId = jsonString(o, "last_message_id");
    c.nsfw = jsonBool(o, "nsfw");
    c.slowmode = jsonInt(o, "slowmode");
    for (const QJsonValue& v : jsonArray(o, "recipients")) {
        c.recipients << v.toString();
    }
    // Voice is a flag on a text channel in this schema, not a channel type. The
    // object is present-but-empty on channels with no voice state attached.
    c.voice = !jsonObject(o, "voice").isEmpty();
    return c;
}

Server parseServer(const QJsonObject& o) {
    Server s;
    s.id = jsonString(o, "_id");
    s.ownerId = jsonString(o, "owner");
    s.name = jsonString(o, "name");
    s.description = jsonString(o, "description");
    if (!jsonObject(o, "icon").isEmpty()) s.icon = parseFile(jsonObject(o, "icon"));
    if (!jsonObject(o, "banner").isEmpty()) s.banner = parseFile(jsonObject(o, "banner"));
    s.approximateMemberCount = jsonInt(o, "approximate_member_count");
    s.nsfw = jsonBool(o, "nsfw");
    // `channels` is an array of ids on the server object inside Ready, and an
    // array of whole channel objects on GET /servers/{id}?include_channels=true.
    // Calling toString() on the latter yields an empty string, which silently
    // produces a server whose channel list matches nothing at all.
    for (const QJsonValue& v : jsonArray(o, "channels")) {
        if (v.isString()) {
            s.channelIds << v.toString();
        } else if (v.isObject()) {
            s.channelIds << jsonString(v.toObject(), "_id");
        }
    }
    for (const QJsonValue& v : jsonArray(o, "categories")) {
        const QJsonObject cj = v.toObject();
        Category cat;
        cat.id = jsonString(cj, "id");
        cat.title = jsonString(cj, "title");
        for (const QJsonValue& cid : jsonArray(cj, "channels")) {
            cat.channelIds << cid.toString();
        }
        s.categories << cat;
    }
    return s;
}

// --- messages ----------------------------------------------------------------

QString Emoji::url(const CdnConfig& cdn) const {
    if (id.isEmpty() || cdn.url.isEmpty()) return {};
    return cdn.url + QStringLiteral("/emojis/") + id;
}

// {"_id": ..., "creator_id": ..., "name": "debian", "parent": {id, type:"Server"}}
Emoji parseEmoji(const QJsonObject& o) {
    Emoji e;
    e.id = jsonString(o, "_id");
    const QJsonObject parent = jsonObject(o, "parent");
    e.serverId = jsonString(parent, "id");
    e.name = jsonString(o, "name");
    e.animated = jsonBool(o, "animated");
    return e;
}

Message parseMessage(const QJsonObject& o) {
    Message m;
    m.id = jsonString(o, "_id");
    m.channelId = jsonString(o, "channel");
    m.authorId = jsonString(o, "author");
    m.nonce = jsonString(o, "nonce");
    m.content = jsonString(o, "content");
    m.pinned = jsonBool(o, "pinned");
    m.flags = int(jsonInt(o, "flags"));
    m.system = jsonObject(o, "system");
    const QString edited = jsonString(o, "edited");
    if (!edited.isEmpty()) m.edited = QDateTime::fromString(edited, Qt::ISODate);

    for (const QJsonValue& v : jsonArray(o, "attachments")) {
        if (v.isObject()) m.attachments << parseFile(v.toObject());
    }
    for (const QJsonValue& v : jsonArray(o, "embeds")) {
        if (v.isObject()) m.embeds << parseEmbed(v.toObject());
    }
    for (const QJsonValue& v : jsonArray(o, "mentions")) {
        m.mentions << v.toString();
    }
    for (const QJsonValue& v : jsonArray(o, "role_mentions")) {
        m.roleMentions << v.toString();
    }
    for (const QJsonValue& v : jsonArray(o, "replies")) {
        m.replyIds << v.toString();
    }

    const QJsonObject masq = jsonObject(o, "masquerade");
    if (!masq.isEmpty()) {
        m.masquerade.name = jsonString(masq, "name");
        m.masquerade.avatarId = jsonString(masq, "avatar");
        m.masquerade.colour = jsonString(masq, "colour");
        m.masquerade.active = true;
    }

    // reactions is emoji id -> user ids, so both the count and "did I react" are
    // answered by the payload and neither needs a second request.
    const QJsonObject reactions = jsonObject(o, "reactions");
    for (auto it = reactions.constBegin(); it != reactions.constEnd(); ++it) {
        QStringList users;
        for (const QJsonValue& v : it.value().toArray()) {
            users << v.toString();
        }
        m.reactions.insert(it.key(), users);
    }

    return m;
}

Embed parseEmbed(const QJsonObject& o) {
    Embed e;
    e.type = jsonString(o, "type");
    e.title = jsonString(o, "title");
    e.description = jsonString(o, "description");
    e.url = jsonString(o, "url");
    e.siteName = jsonString(o, "site_name");
    e.iconUrl = jsonString(o, "icon_url");
    e.colour = jsonString(o, "colour");

    // Website embeds nest their media one level down, media embeds are flat.
    const QJsonObject image = jsonObject(o, "image");
    const QJsonObject video = jsonObject(o, "video");
    if (e.type == QLatin1String("Website")) {
        e.imageUrl = jsonString(image, "url");
        e.width = int(jsonInt(image, "width"));
        e.height = int(jsonInt(image, "height"));
        e.mediaUrl = jsonString(video, "url");
    } else {
        e.imageUrl = jsonString(o, "url");
        e.mediaUrl = jsonString(o, "url");
        e.width = int(jsonInt(o, "width"));
        e.height = int(jsonInt(o, "height"));
    }
    return e;
}

ChannelUnread parseChannelUnread(const QJsonObject& o) {
    ChannelUnread u;
    u.channelId = jsonString(jsonObject(o, "_id"), "channel");
    u.lastMessageId = jsonString(o, "last_id");
    for (const QJsonValue& v : jsonArray(o, "mentions")) {
        u.mentions << v.toString();
    }
    return u;
}

InstanceConfig parseInstanceConfig(const QJsonObject& o) {
    InstanceConfig cfg;
    cfg.apiVersion = jsonString(o, "revolt");
    cfg.wsUrl = jsonString(o, "ws");
    cfg.appUrl = jsonString(o, "app");

    const QJsonObject features = jsonObject(o, "features");

    const QJsonObject autumn = jsonObject(features, "autumn");
    if (!autumn.isEmpty()) {
        cfg.cdn.enabled = jsonBool(autumn, "enabled", true);
        cfg.cdn.url = jsonString(autumn, "url", cfg.cdn.url);
    }

    const QJsonObject january = jsonObject(features, "january");
    if (!january.isEmpty() && jsonBool(january, "enabled", true)) {
        cfg.januaryUrl = jsonString(january, "url");
    }

    // Both tiers are served; the higher one is chosen server-side per user and
    // arrives on the user object. The default tier is the right assumption for a
    // first paint and is corrected once /users/@me lands.
    const QJsonObject defaults = jsonObject(jsonObject(features, "limits"), "default");
    cfg.maxMessageLength = int(jsonInt(defaults, "message_length", cfg.maxMessageLength));
    cfg.maxAttachmentsPerMessage =
        int(jsonInt(defaults, "message_attachments", cfg.maxAttachmentsPerMessage));
    cfg.maxAttachmentBytes =
        jsonInt(jsonObject(defaults, "file_upload_size_limits"), "attachments", cfg.maxAttachmentBytes);

    return cfg;
}

} // namespace nimbus
