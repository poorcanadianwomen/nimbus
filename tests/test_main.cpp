#include "../src/core/app.h"
#include "../src/core/events.h"
#include "../src/core/json.h"
#include "../src/core/log.h"
#include "../src/core/models.h"
#include "../src/core/session.h"
#include "../src/core/store.h"
#include "../src/core/ws.h"

#include <QtCore/QJsonArray>
#include <QtCore/QTemporaryDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrlQuery>
#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

#include <cmath>

// Ids in this API are 26-character ULIDs. They are used verbatim throughout so a
// failing assertion points at a plausible payload rather than at "0001".
static QString ulid(char seed) {
    QString id(26, QLatin1Char('0'));
    id[0] = QLatin1Char('0');
    for (int i = 1; i < 26; ++i) id[i] = seed;
    return id;
}

static QJsonObject sampleReady() {
    QJsonObject user;
    user["_id"] = ulid('u');
    user["username"] = "lexiear";
    user["display_name"] = "lex";

    QJsonObject dm;
    dm["_id"] = ulid('d');
    dm["channel_type"] = QStringLiteral("DirectMessage");
    dm["recipients"] = QJsonArray{ulid('u'), ulid('f')};
    dm["last_message_id"] = ulid('m');

    QJsonObject textChannel;
    textChannel["_id"] = ulid('c');
    textChannel["channel_type"] = QStringLiteral("TextChannel");
    textChannel["server"] = ulid('s');
    textChannel["name"] = QStringLiteral("general");

    QJsonObject server;
    server["_id"] = ulid('s');
    server["owner"] = ulid('u');
    server["name"] = QStringLiteral("Stoat HQ");
    server["channels"] = QJsonArray{ulid('c')};
    server["approximate_member_count"] = 42;

    QJsonObject category;
    category["id"] = QStringLiteral("text");
    category["title"] = QStringLiteral("Text channels");
    category["channels"] = QJsonArray{ulid('c')};
    server["categories"] = QJsonArray{category};

    QJsonObject unread;
    unread["_id"] = QJsonObject{{"channel", ulid('c')}, {"user", ulid('u')}};
    unread["last_id"] = ulid('m');
    unread["mentions"] = QJsonArray{ulid('u')};

    return QJsonObject{
        {"users", QJsonArray{user}},
        {"servers", QJsonArray{server}},
        {"channels", QJsonArray{dm}},
        {"channel_unreads", QJsonArray{unread}},
    };
}

class TestCore : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();

    void test_json_helpers();
    void test_parse_user();
    void test_parse_channel_types();
    void test_parse_message_reactions();
    void test_parse_message_merges_are_partial_safe();
    void test_parse_instance_config();
    void test_parse_server_accepts_both_channel_shapes();
    void test_server_category_resolution();

    void test_store_server_and_channels();
    void test_store_message_ordering();
    void test_store_cap();
    void test_store_edit_keeps_attachments();
    void test_store_pending_resolution();
    void test_store_unreads();

    void test_session_roundtrip();
    void test_events_url_building();
    void test_ws_frame_lengths();

    void test_direct_channel_order_is_stable();
    void test_parse_emoji();
    void test_store_emoji_by_server_is_sorted();
    void test_reaction_for_an_unheld_message_is_ignored();
    void test_reaction_for_a_held_message_is_applied();
};

// --- json --------------------------------------------------------------------

void TestCore::initTestCase() {
    nimbus::installLogHandler(nimbus::LogLevel::Warn);
}

void TestCore::test_json_helpers() {
    const QJsonObject o{{"s", QStringLiteral("x")}, {"i", 7.0}, {"b", true}};
    QCOMPARE(nimbus::jsonString(o, "s"), QStringLiteral("x"));
    QCOMPARE(nimbus::jsonString(o, "missing", QStringLiteral("d")), QStringLiteral("d"));
    QCOMPARE(nimbus::jsonInt(o, "i"), 7);
    QVERIFY(nimbus::jsonBool(o, "b"));
    // A field of the wrong type must fall back rather than coerce, or a null
    // becomes a silent empty string that reads like a real value.
    QCOMPARE(nimbus::jsonString(QJsonObject{{"s", 1.0}}, "s", QStringLiteral("d")),
             QStringLiteral("d"));
}

// --- parsing -----------------------------------------------------------------

void TestCore::test_parse_user() {
    const QJsonObject o{
        {"_id", ulid('u')},
        {"username", QStringLiteral("lexiear")},
        {"display_name", QStringLiteral("lex")},
        {"discriminator", QStringLiteral("0")},
        {"relationship", QStringLiteral("Friend")},
        {"online", true},
        {"avatar", QJsonObject{{"_id", ulid('a')}, {"tag", QStringLiteral("abc")}}},
    };
    const nimbus::User u = nimbus::parseUser(o);
    QCOMPARE(u.id, ulid('u'));
    QCOMPARE(u.username, QStringLiteral("lexiear"));
    QCOMPARE(u.displayName, QStringLiteral("lex"));
    QCOMPARE(u.relationship, nimbus::Relationship::Friend);
    QVERIFY(u.online);
    QCOMPARE(u.avatar.id, ulid('a'));
    QCOMPARE(u.avatar.tag, QStringLiteral("abc"));
    QCOMPARE(u.monogram(), QStringLiteral("L"));
}

void TestCore::test_parse_channel_types() {
    const auto type = [](const char* name) {
        return nimbus::parseChannel(QJsonObject{{"channel_type", QString::fromLatin1(name)}}).type;
    };
    QCOMPARE(type("TextChannel"), nimbus::ChannelType::TextChannel);
    QCOMPARE(type("DirectMessage"), nimbus::ChannelType::DirectMessage);
    QCOMPARE(type("Group"), nimbus::ChannelType::Group);
    QCOMPARE(type("SavedMessages"), nimbus::ChannelType::SavedMessages);
    // An unrecognised type must stay Unknown rather than defaulting to something
    // renderable; SavedMessages is not a chat and accepting messages in it would
    // post into a private notepad.
    QCOMPARE(type("SomethingNewIn2027"), nimbus::ChannelType::Unknown);
    QVERIFY(!nimbus::parseChannel(QJsonObject{{"channel_type", QStringLiteral("SavedMessages")}}).acceptsMessages());
    QVERIFY(nimbus::parseChannel(QJsonObject{{"channel_type", QStringLiteral("TextChannel")}}).acceptsMessages());

    const nimbus::Channel dm = nimbus::parseChannel(QJsonObject{
        {"_id", ulid('d')},
        {"channel_type", QStringLiteral("DirectMessage")},
        {"recipients", QJsonArray{ulid('u'), ulid('f')}},
    });
    QCOMPARE(dm.recipients.size(), 2);
    QVERIFY(dm.isDirect());
}

void TestCore::test_parse_message_reactions() {
    const nimbus::Message m = nimbus::parseMessage(QJsonObject{
        {"_id", ulid('m')},
        {"channel", ulid('c')},
        {"author", ulid('u')},
        {"content", QStringLiteral("hi")},
        {"nonce", QStringLiteral("n-1")},
        // The wire shape is emoji id -> user ids, so both the count and whether
        // the current user reacted are answerable without a second request.
        {"reactions", QJsonObject{{ulid('e'), QJsonArray{ulid('u'), ulid('f')}}}},
        {"replies", QJsonArray{ulid('m')}},
        {"pinned", true},
    });

    QCOMPARE(m.id, ulid('m'));
    QCOMPARE(m.channelId, ulid('c'));
    QCOMPARE(m.nonce, QStringLiteral("n-1"));
    QCOMPARE(m.reactionCount(ulid('e')), 2);
    QVERIFY(m.reactedBy(ulid('e'), ulid('u')));
    QVERIFY(!m.reactedBy(ulid('e'), ulid('x')));
    QCOMPARE(m.replyIds.size(), 1);
    QVERIFY(m.pinned);
    QVERIFY(!m.isSystem());
}

void TestCore::test_parse_message_merges_are_partial_safe() {
    // An edit carries only content, so parsing it must not imply the message
    // lost its attachments: that is the store's job, and only if it merges.
    const nimbus::Message partial = nimbus::parseMessage(QJsonObject{
        {"_id", ulid('m')},
        {"channel", ulid('c')},
        {"content", QStringLiteral("edited")},
    });
    QVERIFY(partial.attachments.isEmpty());
    QCOMPARE(partial.content, QStringLiteral("edited"));
}

void TestCore::test_parse_instance_config() {
    // Captured verbatim from GET https://api.stoat.chat/ on 2026-09-28.
    const nimbus::InstanceConfig cfg = nimbus::parseInstanceConfig(QJsonDocument::fromJson(R"({
        "revolt": "0.15.7",
        "ws": "wss://events.stoat.chat",
        "app": "https://stoat.chat",
        "features": {
            "autumn": { "enabled": true, "url": "https://cdn.stoatusercontent.com" },
            "january": { "enabled": true, "url": "https://proxy.stoatusercontent.com" },
            "limits": {
                "default": {
                    "message_length": 2000,
                    "message_attachments": 5,
                    "file_upload_size_limits": { "attachments": 20000000 }
                }
            }
        }
    })").object());

    QCOMPARE(cfg.apiVersion, QStringLiteral("0.15.7"));
    QCOMPARE(cfg.wsUrl, QStringLiteral("wss://events.stoat.chat"));
    // The CDN host is not the API host and not the historical autumn.revolt.chat;
    // hardcoding either would 404 every avatar.
    QCOMPARE(cfg.cdn.url, QStringLiteral("https://cdn.stoatusercontent.com"));
    QCOMPARE(cfg.maxMessageLength, 2000);
    QCOMPARE(cfg.maxAttachmentsPerMessage, 5);
    QCOMPARE(cfg.maxAttachmentBytes, 20000000);

    // The tag is the path variant, not a file extension: the CDN's own error for an
    // unknown one lists them all -- attachments, avatars, backgrounds, icons,
    // banners, emojis. Verified against the live host.
    const nimbus::File f{ulid('a'), QStringLiteral("avatars"), QStringLiteral("x.png"),
                         QStringLiteral("image/png"), 12, 4, 4, false, false};
    QCOMPARE(f.url(cfg.cdn),
             QStringLiteral("https://cdn.stoatusercontent.com/avatars/") + ulid('a'));

    // No tag, no url: guessing a variant would produce a 404 at paint time instead
    // of an honest fallback to a monogram.
    const nimbus::File untagged{ulid('a'), {}, QStringLiteral("x.png"),
                                QStringLiteral("image/png"), 12, 4, 4, false, false};
    QVERIFY(untagged.url(cfg.cdn).isEmpty());
}

void TestCore::test_parse_server_accepts_both_channel_shapes() {
    // Ready lists a server's channels as ids...
    const nimbus::Server fromReady = nimbus::parseServer(QJsonObject{
        {"_id", ulid('s')},
        {"name", QStringLiteral("HQ")},
        {"channels", QJsonArray{ulid('a'), ulid('b')}},
    });
    QCOMPARE(fromReady.channelIds, QStringList({ulid('a'), ulid('b')}));

    // ...while GET /servers/{id}?include_channels=true returns the same field as
    // whole channel objects, on a flat object with no nested "server" key.
    // Reading that with toString() yields empty strings, and the server then
    // matches no channels at all while still looking fully populated.
    const nimbus::Server fromFetch = nimbus::parseServer(QJsonObject{
        {"_id", ulid('s')},
        {"name", QStringLiteral("HQ")},
        {"channels", QJsonArray{
            QJsonObject{{"_id", ulid('a')},
                        {"channel_type", QStringLiteral("TextChannel")},
                        {"server", ulid('s')},
                        {"name", QStringLiteral("general")}},
            QJsonObject{{"_id", ulid('b')},
                        {"channel_type", QStringLiteral("TextChannel")},
                        {"server", ulid('s')},
                        {"name", QStringLiteral("random")}},
        }},
    });
    QCOMPARE(fromFetch.channelIds, QStringList({ulid('a'), ulid('b')}));
    QVERIFY(!fromFetch.channelIds.contains(QString()));

    // End to end: the two shapes must resolve to the same visible channel list.
    nimbus::Store store;
    for (const QJsonValue& v : QJsonArray{
             QJsonObject{{"_id", ulid('a')}, {"channel_type", QStringLiteral("TextChannel")},
                         {"server", ulid('s')}, {"name", QStringLiteral("general")}},
             QJsonObject{{"_id", ulid('b')}, {"channel_type", QStringLiteral("TextChannel")},
                         {"server", ulid('s')}, {"name", QStringLiteral("random")}}}) {
        store.upsertChannel(nimbus::parseChannel(v.toObject()));
    }
    store.upsertServer(fromReady);
    QCOMPARE(store.channelsForServer(ulid('s')).size(), 2);
    store.upsertServer(fromFetch);
    QCOMPARE(store.channelsForServer(ulid('s')).size(), 2);

    // An update carrying no channel list must not erase the known one.
    nimbus::Server bare;
    bare.id = ulid('s');
    bare.name = QStringLiteral("HQ renamed");
    store.upsertServer(bare);
    QCOMPARE(store.channelsForServer(ulid('s')).size(), 2);
    QCOMPARE(store.server(ulid('s'))->name, QStringLiteral("HQ renamed"));
}

void TestCore::test_server_category_resolution() {
    const nimbus::Server s = nimbus::parseServer(QJsonObject{
        {"_id", ulid('s')},
        {"owner", ulid('u')},
        {"name", QStringLiteral("HQ")},
        {"channels", QJsonArray{ulid('c'), ulid('d')}},
        {"categories", QJsonArray{
            QJsonObject{{"id", QStringLiteral("text")},
                        {"title", QStringLiteral("Text")},
                        {"channels", QJsonArray{ulid('c')}}}}},
    });
    QCOMPARE(s.categoryIdFor(ulid('c')), QStringLiteral("text"));
    // A channel absent from every category is uncategorised, not lost.
    QVERIFY(s.categoryIdFor(ulid('d')).isEmpty());
}

// --- store -------------------------------------------------------------------

void TestCore::test_store_server_and_channels() {
    nimbus::Store store;
    const nimbus::Server server = nimbus::parseServer(sampleReady().value("servers").toArray().first().toObject());
    const nimbus::Channel dm = nimbus::parseChannel(sampleReady().value("channels").toArray().first().toObject());

    QSignalSpy added(&store, &nimbus::Store::channelAdded);
    store.upsertChannel(dm);
    store.upsertServer(server);
    QCOMPARE(added.count(), 1);
    QCOMPARE(store.servers().size(), 1);
    QCOMPARE(store.directChannels().size(), 1);
    // The server's own channel list is a different channel id, so it resolves to
    // nothing rather than accidentally matching the DM.
    QVERIFY(store.channelsForServer(server.id).isEmpty());
}

void TestCore::test_store_message_ordering() {
    nimbus::Store store;
    nimbus::Channel c;
    c.id = ulid('c');
    c.type = nimbus::ChannelType::TextChannel;
    store.upsertChannel(c);

    // Inserted newest-first on purpose: ordering must come from the ids, not from
    // arrival order, or a backfilled page lands in the wrong place.
    nimbus::Message third; third.id = ulid('3'); third.channelId = c.id;
    nimbus::Message first; first.id = ulid('1'); first.channelId = c.id;
    nimbus::Message second; second.id = ulid('2'); second.channelId = c.id;
    store.upsertMessage(third);
    store.upsertMessage(first);
    store.upsertMessage(second);

    const auto messages = store.messages(c.id);
    QCOMPARE(messages.size(), 3);
    QCOMPARE(messages[0]->id, ulid('1'));
    QCOMPARE(messages[1]->id, ulid('2'));
    QCOMPARE(messages[2]->id, ulid('3'));
    QCOMPARE(store.oldestMessageId(c.id), ulid('1'));
}

void TestCore::test_store_cap() {
    nimbus::Store store;
    nimbus::Channel c;
    c.id = ulid('c');
    store.upsertChannel(c);

    for (int i = 0; i < 600; ++i) {
        nimbus::Message m;
        m.id = ulid('a');
        m.id[1] = QLatin1Char('0' + (i / 100) % 10);
        m.id[2] = QLatin1Char('0' + (i / 10) % 10);
        m.id[3] = QLatin1Char('0' + i % 10);
        m.channelId = c.id;
        store.upsertMessage(m);
    }
    // An unbounded transcript is a slow leak: 600 in, 500 stay, oldest 100 evicted.
    QCOMPARE(store.messageCount(c.id), 500);
    QVERIFY(store.message(c.id, ulid('a')) == nullptr);
}

void TestCore::test_store_edit_keeps_attachments() {
    nimbus::Store store;
    nimbus::Channel c;
    c.id = ulid('c');
    store.upsertChannel(c);

    nimbus::File file{ulid('a'), QStringLiteral("t"), QStringLiteral("x.png"),
                      QStringLiteral("image/png"), 1, 2, 2, false, false};
    nimbus::Message original;
    original.id = ulid('m');
    original.channelId = c.id;
    original.content = QStringLiteral("first");
    original.attachments = {file};
    original.reactions.insert(ulid('e'), QStringList{ulid('u')});
    store.upsertMessage(original);

    // Exactly what MessageUpdate carries: id, channel, and only the changed field.
    nimbus::Message edit;
    edit.id = ulid('m');
    edit.channelId = c.id;
    edit.content = QStringLiteral("second");
    store.upsertMessage(edit);

    const nimbus::Message* stored = store.message(c.id, ulid('m'));
    QVERIFY(stored != nullptr);
    QCOMPARE(stored->content, QStringLiteral("second"));
    QCOMPARE(stored->attachments.size(), 1);
    QCOMPARE(stored->reactionCount(ulid('e')), 1);
}

void TestCore::test_store_pending_resolution() {
    nimbus::Store store;
    nimbus::Channel c;
    c.id = ulid('c');
    store.upsertChannel(c);

    nimbus::Message pending;
    pending.id = QStringLiteral("pending:n-1");
    pending.channelId = c.id;
    pending.nonce = QStringLiteral("n-1");
    pending.content = QStringLiteral("sent");
    pending.pending = true;
    store.upsertMessage(pending);
    QCOMPARE(store.messageCount(c.id), 1);

    nimbus::Message confirmed;
    confirmed.id = ulid('m');
    confirmed.channelId = c.id;
    confirmed.nonce = QStringLiteral("n-1");
    confirmed.content = QStringLiteral("sent");

    QVERIFY(store.resolvePending(c.id, QStringLiteral("n-1"), confirmed));
    // Re-keyed in place: the row count must not change or the message appears twice.
    QCOMPARE(store.messageCount(c.id), 1);
    const nimbus::Message* stored = store.message(c.id, ulid('m'));
    QVERIFY(stored != nullptr);
    QVERIFY(!stored->pending);
    QVERIFY(store.message(c.id, QStringLiteral("pending:n-1")) == nullptr);
}

void TestCore::test_store_unreads() {
    nimbus::Store store;
    const nimbus::ChannelUnread u = nimbus::parseChannelUnread(
        sampleReady().value("channel_unreads").toArray().first().toObject());
    QCOMPARE(u.channelId, ulid('c'));
    QCOMPARE(u.mentions.size(), 1);

    store.upsertUnread(u);
    QCOMPARE(store.mentionCount(ulid('c')), 1);

    store.acknowledge(ulid('c'));
    QCOMPARE(store.mentionCount(ulid('c')), 0);
}

// --- session -----------------------------------------------------------------

void TestCore::test_session_roundtrip() {
    // The credential file this test deletes and rewrites must not be the one the
    // user actually logs in with. This ran against the real path until it did not:
    // every test run logged the client out.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("NIMBUS_SESSION_FILE", QFile::encodeName(dir.filePath(QStringLiteral("session"))));

    // If the override ever stops being honoured, fail here rather than quietly
    // eating the live credential again.
    QVERIFY2(nimbus::SessionStore::path() != QDir::homePath() + QStringLiteral("/.config/nimbus/session"),
             "NIMBUS_SESSION_FILE was ignored: this test would delete the real session");

    nimbus::Session session;
    session.token = QStringLiteral("tok");
    session.userId = ulid('u');
    session.name = QStringLiteral("nimbus");

    QCOMPARE(nimbus::SessionStore::save(session), QString());
    QCOMPARE(QFile(nimbus::SessionStore::path()).permissions() & QFileDevice::ReadOwner,
             QFileDevice::ReadOwner);
    QCOMPARE(QFile(nimbus::SessionStore::path()).permissions() & QFileDevice::ReadGroup, QFileDevice::Permissions());

    const nimbus::Session loaded = nimbus::SessionStore::load();
    QCOMPARE(loaded.token, session.token);
    QCOMPARE(loaded.userId, session.userId);
    QVERIFY(loaded.isValid());

    QVERIFY(nimbus::SessionStore::remove());
    QVERIFY(!nimbus::SessionStore::load().isValid());

    qunsetenv("NIMBUS_SESSION_FILE");
}

// --- transport ---------------------------------------------------------------

void TestCore::test_events_url_building() {
    const QUrl url = nimbus::Events::socketUrl(QStringLiteral("wss://events.stoat.chat"),
                                               QStringLiteral("tok"));
    const QUrlQuery query(url);
    QCOMPARE(query.queryItemValue(QStringLiteral("version")), QStringLiteral("1"));
    QCOMPARE(query.queryItemValue(QStringLiteral("format")), QStringLiteral("json"));
    // Authentication is a query parameter here. There is no authenticate frame in
    // this protocol, so omitting the token fails as a 1008 at connect time.
    QCOMPARE(query.queryItemValue(QStringLiteral("token")), QStringLiteral("tok"));

    // A ws url that already carries a query must keep it rather than have it
    // replaced, which is what a self-hosted instance behind a path prefix needs.
    const QUrl withQuery = nimbus::Events::socketUrl(
        QStringLiteral("wss://chat.example/?tenant=acme"), QStringLiteral("tok"));
    const QUrlQuery preserved(withQuery);
    QCOMPARE(preserved.queryItemValue(QStringLiteral("tenant")), QStringLiteral("acme"));
    QCOMPARE(preserved.queryItemValue(QStringLiteral("token")), QStringLiteral("tok"));

    // No token means no token parameter, not an empty one.
    const QUrl anonymous = nimbus::Events::socketUrl(QStringLiteral("wss://events.stoat.chat"), {});
    QVERIFY(!anonymous.query().contains(QStringLiteral("token")));
}

void TestCore::test_ws_frame_lengths() {
    const QByteArray key = QByteArray::fromHex("01020304");

    // Every client frame must declare the MASK bit in byte two. Asserting only
    // `byte & 0x7F == len` hides exactly this failure, and a frame that omits the
    // bit while carrying a key and masked bytes still has a correct length, a
    // correct payload and a mask that round-trips -- it is only rejected by the
    // peer, which reads the key as the first four bytes of the message.
    const auto secondByte = [](const QByteArray& f) { return quint8(f.at(1)); };

    const QByteArray small = nimbus::encodeWebSocketFrame(0x81, QByteArray(125, 'a'), key);
    QCOMPARE(small.size(), 2 + 4 + 125);
    QCOMPARE(secondByte(small) & 0x80, quint8(0x80)); // MASK must be set
    QCOMPARE(secondByte(small) & 0x7F, quint8(125));

    // 16-bit boundary: 126 must switch encoding rather than be truncated.
    const QByteArray medium = nimbus::encodeWebSocketFrame(0x81, QByteArray(126, 'a'), key);
    QCOMPARE(secondByte(medium) & 0x80, quint8(0x80));
    QCOMPARE(secondByte(medium) & 0x7F, quint8(126));
    QCOMPARE((quint8(medium.at(2)) << 8) | quint8(medium.at(3)), 126);
    QCOMPARE(medium.size(), 4 + 4 + 126);

    // The largest payload that still fits in 16 bits.
    const QByteArray large = nimbus::encodeWebSocketFrame(0x81, QByteArray(65535, 'a'), key);
    QCOMPARE(secondByte(large) & 0x80, quint8(0x80));
    QCOMPARE(secondByte(large) & 0x7F, quint8(126));
    QCOMPARE((quint8(large.at(2)) << 8) | quint8(large.at(3)), 65535);

    // One byte more must switch to the 64-bit form, still masked.
    const QByteArray huge = nimbus::encodeWebSocketFrame(0x81, QByteArray(65536, 'a'), key);
    QCOMPARE(secondByte(huge) & 0x80, quint8(0x80));
    QCOMPARE(secondByte(huge) & 0x7F, quint8(127));
    quint64 length = 0;
    for (int i = 0; i < 8; ++i) length = (length << 8) | quint8(huge.at(2 + i));
    QCOMPARE(length, quint64(65536));

    // The payload the peer will recover must equal the payload that went in.
    const QByteArray payload = QByteArrayLiteral("{\"type\":\"Ping\"}");
    const QByteArray masked = nimbus::encodeWebSocketFrame(0x81, payload, key);
    const QByteArray recovered =
        nimbus::maskFramePayload(masked.mid(6), key.mid(0, 4)); // skip header + key
    QCOMPARE(recovered, payload);

    // Masking is its own inverse, which is the property that lets the same
    // routine both apply and remove the mask.
    QCOMPARE(nimbus::maskFramePayload(nimbus::maskFramePayload(payload, key), key), payload);
    QVERIFY(nimbus::maskFramePayload(payload, key) != payload);

    // An empty key means the frame is sent unmasked, which only a server may do.
    const QByteArray unmasked = nimbus::encodeWebSocketFrame(0x88, QByteArray(2, '\0'), {});
    QCOMPARE(unmasked.size(), 4);
    QCOMPARE(secondByte(unmasked) & 0x80, quint8(0));
}

// Synthetic ULIDs, 26 characters each so they pass the id checks. An earlier
// version of this test carried a real emote and server id, taken from a live
// account while verifying the CDN path, which does not belong in a public file.
static const QString kEmojiId = QStringLiteral("01BBBBBBBBBBBBBBBBBBBBBB");
static const QString kServerId = QStringLiteral("01CCCCCCCCCCCCCCCCCCCCCC");

// The shape the endpoint returns: _id, creator_id, name, parent, and an animated
// flag. No file -- the image is addressed by id under the "emojis" variant.
void TestCore::test_parse_emoji() {
    const nimbus::Emoji emoji = nimbus::parseEmoji(QJsonObject{
        {"_id", kEmojiId},
        {"creator_id", "01JNX18KGWQ1E0TR49J1138D6D"},
        {"name", "debian"},
        {"animated", true},
        {"parent", QJsonObject{{"id", kServerId}, {"type", "Server"}}}});

    QCOMPARE(emoji.id, kEmojiId);
    QCOMPARE(emoji.name, QStringLiteral("debian"));
    QCOMPARE(emoji.serverId, kServerId);
    // The name arrives bare; a message has to read ":debian:".
    QCOMPARE(emoji.shortcode(), QStringLiteral(":debian:"));
    QVERIFY(emoji.animated);

    // The image is addressed by id under the "emojis" variant, with no file in the
    // payload; the CDN redirects to the real filename. Verified against the live host:
    // /emojis/01H34AE0A... 308s to a 96x96 animated gif.
    const nimbus::CdnConfig cdn{QStringLiteral("https://cdn.stoatusercontent.com"), true};
    QCOMPARE(emoji.url(cdn), QStringLiteral("https://cdn.stoatusercontent.com/emojis/") + kEmojiId);

    // A blank host, no url. A default-constructed CdnConfig already carries the
    // production host, so the case that matters is the one before discovery: the
    // store starts with no host and a picker with nothing to ask the cache for.
    QCOMPARE(emoji.url(nimbus::CdnConfig{QString(), false}), QString());

    // A nameless entry is unusable in a picker and must not be stored as one.
    const nimbus::Emoji nameless = nimbus::parseEmoji(QJsonObject{{"_id", "01X"}});
    QVERIFY(nameless.shortcode().isEmpty());
}

void TestCore::test_store_emoji_by_server_is_sorted() {
    nimbus::Store store;
    const QString server = "01SERVER";
    const QString other = "01OTHER";

    // Inserted out of order, and on two servers, because QHash iteration is
    // unspecified and the picker must not reshuffle between openings.
    store.upsertEmoji(nimbus::Emoji{"01C", "zebra", server});
    store.upsertEmoji(nimbus::Emoji{"01A", "apple", server});
    store.upsertEmoji(nimbus::Emoji{"01B", "moss", server});
    store.upsertEmoji(nimbus::Emoji{"01D", "other-server-only", other});

    QCOMPARE(store.emojiCount(), 4);

    const QList<nimbus::Emoji> mine = store.emojisForServer(server);
    QCOMPARE(mine.size(), 3);
    QCOMPARE(mine.at(0).name, QStringLiteral("apple"));
    QCOMPARE(mine.at(1).name, QStringLiteral("moss"));
    QCOMPARE(mine.at(2).name, QStringLiteral("zebra"));

    // The other server's emoji are not offered in this server's picker.
    QCOMPARE(store.emojisForServer(other).size(), 1);

    QVERIFY(store.emoji("01A"));
    QCOMPARE(store.emoji("01A")->name, QStringLiteral("apple"));
    QVERIFY(!store.emoji("01MISSING"));

    // A nameless payload is ignored rather than stored, so a partial frame cannot
    // blank a name the client already has and leave a pill with nothing to show.
    store.upsertEmoji(nimbus::Emoji{"01A", {}, server});
    QVERIFY(store.emoji("01A"));
    QCOMPARE(store.emoji("01A")->name, QStringLiteral("apple"));
    QCOMPARE(store.emojisForServer(server).size(), 3);
}

// Two DMs that have never had a message compare equal on lastMessageId, and the
// order came from QHash iteration, which is unspecified: the DM list reshuffled
// between rebuilds of the same data. Caught by rendering the list twice.
//
// Checked against the id-ascending order over 40 channels rather than by comparing
// two insertion orders. Comparing two orders only failed about one run in five,
// because the hash order sometimes happens to agree; asserting the exact expected
// sequence over enough channels fails reliably when the tiebreak is missing.
void TestCore::test_direct_channel_order_is_stable() {
    nimbus::Store store;

    constexpr int kChannels = 40;
    for (int i = 0; i < kChannels; ++i) {
        nimbus::Channel dm;
        QString id = ulid('d');
        id[23] = QLatin1Char('0' + (i / 10) % 10);
        id[24] = QLatin1Char('0' + i % 10);
        dm.id = id;
        dm.type = nimbus::ChannelType::DirectMessage;
        dm.recipients = {ulid('u'), ulid('v')};
        store.upsertChannel(dm);
    }

    const QList<nimbus::Channel*> directs = store.directChannels();
    QCOMPARE(directs.size(), kChannels);

    QStringList expected;
    for (int i = 0; i < kChannels; ++i) {
        expected << directs.at(i)->id;
    }
    QStringList ascending = expected;
    ascending.sort();

    // No channel has a lastMessageId, so the whole list is one tie and the id
    // tiebreak alone decides the order.
    QCOMPARE(expected, ascending);
}

// --- gateway frames ----------------------------------------------------------

// The client segfaulted on launch because a reaction frame for a message it did
// not hold was copied out of the store through a null pointer. The guard meant to
// catch it tested the copied message's id, which is a dereference of the null and
// so cannot run. These are the two halves: the unheld message must be a no-op, and
// the held one must still update.
void TestCore::test_reaction_for_an_unheld_message_is_ignored() {
    nimbus::App app;
    app.store()->setSelfId(QStringLiteral("01SELF"));

    QJsonObject frame;
    frame["channel"] = QStringLiteral("01CHANNEL");
    frame["message"] = QStringLiteral("01MISSING");
    frame["emoji"] = QStringLiteral("01EMOJI");
    frame["user"] = QStringLiteral("01OTHER");

    app.dispatch(QStringLiteral("MessageReact"), frame);
    app.dispatch(QStringLiteral("MessageUnreact"), frame);

    // No row invented, and nothing written anywhere.
    QCOMPARE(app.store()->messageCount(QStringLiteral("01CHANNEL")), 0);
}

void TestCore::test_reaction_for_a_held_message_is_applied() {
    nimbus::App app;
    app.store()->setSelfId(QStringLiteral("01SELF"));

    nimbus::Message held;
    held.id = QStringLiteral("01HELD");
    held.channelId = QStringLiteral("01CHANNEL");
    held.authorId = QStringLiteral("01OTHER");
    held.content = QStringLiteral("hello");
    app.store()->upsertMessage(held);

    QJsonObject frame;
    frame["channel"] = QStringLiteral("01CHANNEL");
    frame["message"] = QStringLiteral("01HELD");
    frame["emoji"] = QStringLiteral("01EMOJI");
    frame["user"] = QStringLiteral("01OTHER");

    app.dispatch(QStringLiteral("MessageReact"), frame);
    const nimbus::Message* after = app.store()->message(QStringLiteral("01CHANNEL"),
                                                         QStringLiteral("01HELD"));
    QVERIFY(after);
    QCOMPARE(after->reactions.value(QStringLiteral("01EMOJI")), QStringList{QStringLiteral("01OTHER")});

    app.dispatch(QStringLiteral("MessageUnreact"), frame);
    after = app.store()->message(QStringLiteral("01CHANNEL"), QStringLiteral("01HELD"));
    QVERIFY(after);
    QVERIFY(after->reactions.isEmpty());
}

QTEST_MAIN(TestCore)
#include "test_main.moc"
