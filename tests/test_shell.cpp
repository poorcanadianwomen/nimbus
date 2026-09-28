#include "../src/ui/chat_window.h"
#include "../src/core/store.h"
#include "../src/ui/avatar_cache.h"
#include "../src/ui/channel_list.h"
#include "../src/ui/composer.h"
#include "../src/ui/emoji_picker.h"
#include "../src/ui/message_delegate.h"
#include "../src/ui/message_list.h"
#include "../src/ui/message_model.h"
#include "../src/ui/server_rail.h"
#include "../src/ui/theme.h"

#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtTest/QtTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QToolButton>

namespace {

QString ulid(char seed, int index = 0) {
    QString id(26, QLatin1Char('0'));
    for (int i = 1; i < 26; ++i) id[i] = seed;
    if (index > 0) {
        id[24] = QLatin1Char('0' + (index / 10) % 10);
        id[25] = QLatin1Char('0' + index % 10);
    }
    return id;
}

nimbus::Channel makeChannel(const QString& serverId, const QString& id, const QString& name,
                            const QString& categoryId = {}) {
    nimbus::Channel c;
    c.id = id;
    c.type = nimbus::ChannelType::TextChannel;
    c.serverId = serverId;
    c.name = name;
    c.categoryId = categoryId;
    return c;
}

// A store shaped like a real account, so the panes have something to lay out.
nimbus::Store* buildStore(QObject* parent) {
    auto* store = new nimbus::Store(parent);
    store->setSelfId(ulid('u'));

    nimbus::User lex;
    lex.id = ulid('u');
    lex.username = QStringLiteral("lexiear");
    lex.displayName = QStringLiteral("lex");
    store->upsertUser(lex);

    nimbus::User someone;
    someone.id = ulid('v');
    someone.username = QStringLiteral("stoatfan");
    someone.displayName = QStringLiteral("stoatfan");
    store->upsertUser(someone);

    nimbus::Channel notes;
    notes.id = ulid('h');
    notes.type = nimbus::ChannelType::SavedMessages;
    store->upsertChannel(notes);

    const QString serverId = ulid('s');

    nimbus::Server server;
    server.id = serverId;
    server.ownerId = lex.id;
    server.name = QStringLiteral("Stoat Discussion");
    server.channelIds = {ulid('c', 1), ulid('c', 2), ulid('c', 3)};
    server.categories = {
        nimbus::Category{QStringLiteral("text"), QStringLiteral("Text channels"),
                         {ulid('c', 1), ulid('c', 2)}},
        nimbus::Category{QStringLiteral("staff"), QStringLiteral("Staff"), {ulid('c', 3)}},
    };
    store->upsertServer(server);

    store->upsertChannel(makeChannel(serverId, ulid('c', 1), QStringLiteral("general"),
                                    QStringLiteral("text")));
    store->upsertChannel(makeChannel(serverId, ulid('c', 2), QStringLiteral("stoat-updates"),
                                    QStringLiteral("text")));
    store->upsertChannel(makeChannel(serverId, ulid('c', 3), QStringLiteral("mod-log"),
                                    QStringLiteral("staff")));

    nimbus::ChannelUnread unread;
    unread.channelId = ulid('c', 2);
    unread.mentions = {ulid('u')};
    store->upsertUnread(unread);

    nimbus::Message hello;
    hello.id = ulid('m', 1);
    hello.channelId = ulid('c', 1);
    hello.authorId = someone.id;
    hello.content = QStringLiteral("morning. the websocket fix is in, ready when you are");
    store->upsertMessage(hello);

    nimbus::Message reply;
    reply.id = ulid('m', 2);
    reply.channelId = ulid('c', 1);
    reply.authorId = lex.id;
    reply.content = QStringLiteral("shipping it now, one sec");
    reply.replyIds = {ulid('m', 1)};
    store->upsertMessage(reply);

    nimbus::Message reacted;
    reacted.id = ulid('m', 3);
    reacted.channelId = ulid('c', 1);
    reacted.authorId = someone.id;
    reacted.content = QStringLiteral("nice");
    reacted.reactions.insert(ulid('e', 1), QStringList{ulid('u')});
    reacted.reactions.insert(ulid('e', 2), QStringList{ulid('u'), ulid('v')});
    store->upsertMessage(reacted);

    return store;
}

int countDistinct(const QImage& image) {
    QSet<QRgb> seen;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            seen.insert(image.pixel(x, y));
            if (seen.size() > 32) return int(seen.size());
        }
    }
    return int(seen.size());
}

// Paints one rail row into its own image, so the pixels of the row can be
// examined without the view's own background underneath.
QImage renderRow(const nimbus::RailDelegate& delegate, const QModelIndex& index, const QSize& size,
                 const QColor& background) {
    QStyleOptionViewItem option;
    option.rect = QRect(QPoint(0, 0), size);

    QImage shot(size, QImage::Format_ARGB32);
    shot.fill(background.toRgb());
    QPainter painter(&shot);
    delegate.paint(&painter, option, index);
    painter.end();
    return shot;
}

// Mean luminance of the icon box, which is what the dim actually moves.
double meanLuma(const QImage& image, const QRect& box) {
    double total = 0;
    int count = 0;
    for (int y = box.top(); y <= box.bottom(); ++y) {
        for (int x = box.left(); x <= box.right(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            total += 0.2126 * pixel.redF() + 0.7152 * pixel.greenF() + 0.0722 * pixel.blueF();
            ++count;
        }
    }
    return count ? total / count : 0;
}

} // namespace

class TestShell : public QObject {
    Q_OBJECT
private slots:
    void test_window_renders_something();
    void test_window_uses_native_decoration();
    void test_resize_keeps_the_transcript_anchored();
    void test_long_words_wrap_instead_of_overflowing();
    void test_wrapped_text_never_overflows_its_row_at_any_width();
    void test_hover_band_spans_the_whole_viewport();
    void test_embed_cards_paint_within_their_reserved_space();
    void test_dm_rows_draw_an_avatar_instead_of_a_letter();
    void test_replies_quote_the_message_they_answer();
    void test_avatar_urls_are_absolute_once_the_cdn_host_is_known();
    void test_reaction_pills_resolve_ids_to_shortcodes();
    void test_composer_row_has_attach_and_send_at_opposite_ends();
    void test_send_control_is_a_bare_glyph();
    void test_text_bar_is_capped_and_centred();
    void test_composer_sends_and_resets();
    void test_composer_is_disabled_with_no_channel();
    void test_composer_edit_replaces_and_escapes();
    void test_home_opens_the_direct_message_list();
    void test_rail_has_home_servers_and_channels();
    void test_rail_head_is_your_own_account();
    void test_directs_only_lists_dms_and_not_channels();
    void test_channel_list_orders_by_category();
    void test_rail_dims_unselected_without_a_pill();
};

void TestShell::test_window_renders_something() {
    auto* store = buildStore(this);
    auto* window = new nimbus::ChatWindow;
    window->setStore(store);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window, 2000));

    // Drive it the way a click would, so the panes hold real content rather than
    // the empty state a fresh window shows.
    auto* rail = window->findChild<nimbus::ServerRail*>();
    QVERIFY(rail);
    QMetaObject::invokeMethod(rail, "serverSelected", Q_ARG(QString, ulid('s')));
    QTest::qWait(50);

    const QImage shot = window->grab().toImage();
    QVERIFY(shot.width() > 400);
    QVERIFY(shot.height() > 300);

    // Written to disk so the look can be inspected, not merely asserted about.
    shot.save(QStringLiteral("/tmp/nimbus-shell.png"));

    // A window that rendered one flat colour has the same "passes" problem the
    // transcript test was written to avoid.
    QVERIFY2(countDistinct(shot) > 8, "the shell rendered as a flat fill");

    // The transcript's inset is the *view's* -- a viewport margin -- so it cannot be
    // seen from a delegate painted onto a bare QImage, where the delegate always
    // starts at x=0. viewportMargins() is protected, so this measures the real thing
    // instead: the first inked column of the actual window, relative to the pane
    // edge. That is what "not all crammed against the left" means on screen.
    nimbus::ThemeManager theme;
    const QRgb base = theme.theme().base.rgb();
    const int paneEdge = 54 + 208; // rail, then the channel list
    // y starts below the pane's top seam: that 1px line is not text, and scanning
    // from y=0 finds it in every column and measures nothing.
    int firstInk = shot.width();
    for (int x = paneEdge + 2; x < shot.width() && firstInk == shot.width(); ++x) {
        for (int y = 8; y < 200; ++y) {
            if (shot.pixel(x, y) != base) {
                firstInk = x;
                break;
            }
        }
    }
    QVERIFY(firstInk < shot.width());
    QVERIFY2(firstInk - paneEdge >= 8,
             qPrintable(QStringLiteral("text starts %1px from the pane edge, with no margin")
                            .arg(firstInk - paneEdge)));

    window->hide();
    delete window;
}

// The desktop draws the title bar, the controls and the app menu. A client that
// redraws them ends up with two title bars and none of the platform's window
// management -- snap layouts, keyboard shortcuts, middle-click-to-close.
void TestShell::test_window_uses_native_decoration() {
    auto* store = buildStore(this);
    auto* window = new nimbus::ChatWindow;

    QVERIFY(!(window->windowFlags() & Qt::FramelessWindowHint));
    QVERIFY(window->windowFlags() & Qt::WindowTitleHint);
    QCOMPARE(window->windowTitle(), QStringLiteral("nimbus"));

    // The channel name lives in the window title, where a window list and a taskbar
    // tooltip can both read it.
    window->setStore(store);
    auto* rail = window->findChild<nimbus::ServerRail*>();
    QVERIFY(rail);
    QMetaObject::invokeMethod(rail, "serverSelected", Q_ARG(QString, ulid('s')));
    QCOMPARE(window->windowTitle(), QStringLiteral("#general"));

    delete window;
}

// Bottom anchoring has to survive a resize: every row re-wraps, the scrollbar's
// range changes, and the view has to stay pinned to the newest message.
//
// The re-entrancy guard in MessageList::scrollToBottom is not covered here. Qt
// delivers the geometry churn that provokes a stack overflow differently without a
// window manager, so an offscreen test passes with or without it; this pins the
// behaviour either side of the guard.
//
// 90, not 200: ulid() encodes a two-digit suffix, so an index past 99 aliases
// onto an earlier one and the store silently deduplicates.
void seedOverflow(nimbus::Store* store, const QString& channelId, const QString& authorId) {
    for (int i = 0; i < 90; ++i) {
        nimbus::Message message;
        message.id = ulid('m', 10 + i);
        message.channelId = channelId;
        message.authorId = i % 3 == 0 ? authorId : ulid('v');
        message.content = QStringLiteral("row %1 - long enough to re-wrap when the width "
                                         "changes, which is what the loop below relies on")
                              .arg(i);
        store->upsertMessage(message);
    }
}

// Bottom-anchored, so every one of these ends in scrollToBottom.
void TestShell::test_resize_keeps_the_transcript_anchored() {
    auto* store = buildStore(this);
    seedOverflow(store, ulid('c', 1), ulid('v'));

    auto* window = new nimbus::ChatWindow;
    window->setStore(store);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window, 2000));

    auto* rail = window->findChild<nimbus::ServerRail*>();
    QVERIFY(rail);
    QMetaObject::invokeMethod(rail, "serverSelected", Q_ARG(QString, ulid('s')));
    QTest::qWait(50);

    auto* transcript = window->findChild<nimbus::MessageList*>();
    QVERIFY(transcript);
    QVERIFY(transcript->model());
    QCOMPARE(transcript->model()->rowCount(), 93);

    // Precondition, asserted rather than assumed: without a scrollbar there is no
    // viewport resize and the cycle under test cannot start.
    QVERIFY(transcript->verticalScrollBar()->isVisible());
    QVERIFY(transcript->verticalScrollBar()->maximum() > 0);

    // Bottom-anchored, so every one of these ends in scrollToBottom.
    for (int width : {900, 1040, 720, 1120, 760}) {
        window->resize(width, 640);
        QTest::qWait(20);
    }

    // Still anchored, and the view survived the round trip.
    transcript->scrollToBottom();
    QTest::qWait(20);
    const QScrollBar* bar = transcript->verticalScrollBar();
    QVERIFY(bar->value() >= bar->maximum() - 2);

    window->hide();
    delete window;
}

// The account avatar opens the direct messages. Rendered rather than asserted
// through the model alone, because what this view has to get right is that the
// pane switches to people and the transcript does not keep showing the last server.
// The composer is the only way a message leaves the client, so the parts that decide
// whether it can be sent at all are asserted here rather than left to a manual try.
// A long unbroken token -- a URL is the usual one -- has no break opportunity, so
// Qt::TextWordWrap leaves it whole and it runs past the edge of the transcript. The
// wrapping has to be applied in measure and paint together: they wrap with the same
// flags, and if they disagree the row is the wrong height for its text.
void TestShell::test_long_words_wrap_instead_of_overflowing() {
    nimbus::ThemeManager theme;
    nimbus::Store store;

    const QString url = QStringLiteral("https://example.com/a/very/long/path/") +
                        QString(120, QLatin1Char('x'));

    nimbus::Message message;
    message.id = QStringLiteral("01LONG");
    message.channelId = QStringLiteral("01CHANNEL");
    message.authorId = QStringLiteral("01OTHER");
    message.content = QStringLiteral("look: %1").arg(url);
    store.upsertMessage(message);

    nimbus::MessageModel model(&store);
    model.setStore(&store);
    model.setChannel(QStringLiteral("01CHANNEL"));
    QCOMPARE(model.rowCount(), 1);

    nimbus::MessageDelegate delegate(&theme);
    delegate.setStore(&store);

    constexpr int kWidth = 520;
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, kWidth, 400);

    const int height = delegate.sizeHint(option, model.index(0, 0)).height();

    QImage image(option.rect.size(), QImage::Format_ARGB32);
    image.fill(theme.theme().base.toRgb());
    QPainter painter(&image);
    delegate.paint(&painter, option, model.index(0, 0));
    painter.end();
    image.save(QStringLiteral("/tmp/nimbus-wrap.png"));

    // The single-line height, which is what an unwrappable token produces. A wrapped
    // paragraph is taller, so this is the direct symptom.
    QVERIFY2(height > 40, qPrintable(QStringLiteral("the long url was not wrapped: %1px tall")
                                         .arg(height)));

    // Nothing is inked past the column: the overflow a word-wrap leaves behind.
    const QRgb base = theme.theme().base.rgb();
    for (int y = 0; y < height; ++y) {
        for (int x = kWidth - 3; x < kWidth; ++x) {
            if (image.pixel(x, y) != base) {
                QFAIL(qPrintable(QStringLiteral("text ran past the column at y=%1").arg(y)));
            }
        }
    }
}

// The height a row is given comes from one wrap calculation and the text is drawn
// with another, and Qt does not promise the two agree. When they disagree the row is
// the wrong height and the next message is drawn over it -- which is what "text
// breaking" looks like on screen. Checked across a spread of widths, including ones
// that land a wrap boundary in different places.
void TestShell::test_wrapped_text_never_overflows_its_row_at_any_width() {
    nimbus::ThemeManager theme;
    nimbus::Store store;

    const QString body = QStringLiteral("the quick brown fox jumps over the lazy dog ") +
                         QString(60, QLatin1Char('y')) + QStringLiteral(" and then stops, " ) +
                         QStringLiteral("followed by https://example.com/some/fairly/long/url/" ) +
                         QString(40, QLatin1Char('z'));

    nimbus::Message message;
    message.id = QStringLiteral("01WRAP");
    message.channelId = QStringLiteral("01CHANNEL");
    message.authorId = QStringLiteral("01OTHER");
    message.content = body;
    store.upsertMessage(message);

    nimbus::MessageModel model(&store);
    model.setStore(&store);
    model.setChannel(QStringLiteral("01CHANNEL"));
    QCOMPARE(model.rowCount(), 1);

    nimbus::MessageDelegate delegate(&theme);
    delegate.setStore(&store);

    const QRgb base = theme.theme().base.rgb();
    for (int width = 160; width <= 760; width += 20) {
        QStyleOptionViewItem option;
        option.rect = QRect(0, 0, width, 600);
        const QModelIndex index = model.index(0, 0);

        // A fresh delegate per width: the layout cache is keyed on width, and sharing
        // one across widths is exactly what a stale cache would do.
        nimbus::MessageDelegate fresh(&theme);
        fresh.setStore(&store);
        const int height = fresh.sizeHint(option, index).height();

        QImage image(QSize(width, 600), QImage::Format_ARGB32);
        image.fill(base);
        QPainter painter(&image);
        fresh.paint(&painter, option, index);
        painter.end();

        // Nothing below the row it was given.
        for (int y = height; y < 600; ++y) {
            for (int x = 0; x < width; ++x) {
                if (image.pixel(x, y) != base) {
                    QFAIL(qPrintable(QStringLiteral("width %1: text spilled below its row "
                                                     "(height %2) at y=%3")
                                         .arg(width)
                                         .arg(height)
                                         .arg(y)));
                }
            }
        }
        // And nothing past the column.
        for (int y = 0; y < height; ++y) {
            for (int x = width - 2; x < width; ++x) {
                if (image.pixel(x, y) != base) {
                    QFAIL(qPrintable(QStringLiteral("width %1: text past the column at y=%2")
                                         .arg(width)
                                         .arg(y)));
                }
            }
        }
    }
}

// Moving the mouse across the transcript repaints the row under it, and the row is
// drawn as a band. With the transcript's viewport margins in place the view hands the
// delegate an item rect that is 12px narrower than the viewport on each side, so a
// hover fill confined to option.rect leaves a gap down both edges -- and the gap moves
// with the pointer, which is what reads as the transcript breaking apart as the mouse
// moves over it.
//
// Reproduced the way the view does it: the item rect is the viewport minus the
// margins, and the fill is expected to cover the full width regardless.
void TestShell::test_hover_band_spans_the_whole_viewport() {
    nimbus::ThemeManager theme;
    nimbus::Store store;

    nimbus::Message message;
    message.id = QStringLiteral("01HOVER");
    message.channelId = QStringLiteral("01CHANNEL");
    message.authorId = QStringLiteral("01OTHER");
    message.content = QStringLiteral("a message to hover over");
    store.upsertMessage(message);

    nimbus::MessageModel model(&store);
    model.setStore(&store);
    model.setChannel(QStringLiteral("01CHANNEL"));
    QCOMPARE(model.rowCount(), 1);

    // A real view, because the delegate reads the viewport width off option.widget and
    // a hand-built option leaves that null.
    nimbus::MessageList list;
    list.setModel(&model);
    list.setStore(&store);
    list.resize(600, 300);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list, 2000));

    nimbus::MessageDelegate delegate(&theme);
    delegate.setStore(&store);

    constexpr int kGutter = 12;
    const int kViewport = list.viewport()->width();
    constexpr int kRow = 40;

    QStyleOptionViewItem option;
    option.initFrom(&list);
    // The view hands the delegate its viewport here, and the full width is read off the
    // parent of that. Set explicitly because initFrom on a bare list does not.
    option.widget = list.viewport();
    QVERIFY(option.widget);
    // What the view passes: the viewport less the two gutters.
    option.rect = QRect(0, 0, kViewport - 2 * kGutter, kRow);
    option.state |= QStyle::State_MouseOver;

    QImage image(kViewport, 100, QImage::Format_ARGB32);
    image.fill(theme.theme().base.toRgb());
    QPainter painter(&image);
    delegate.paint(&painter, option, model.index(0, 0));
    painter.end();

    const QRgb hoverFill = theme.theme().surface.rgb();
    const int mid = kRow / 2;

    // The band has to reach both edges of the viewport; stopping short leaves the gap
    // the pointer appears to tear through.
    QCOMPARE(image.pixel(0, mid), hoverFill);
    QCOMPARE(image.pixel(kGutter - 1, mid), hoverFill);
    QCOMPARE(image.pixel(kViewport - 1, mid), hoverFill);
    QCOMPARE(image.pixel(kViewport - kGutter, mid), hoverFill);

    // And it must not bleed past the row it belongs to.
    for (int x = 0; x < kViewport; ++x) {
        QCOMPARE(image.pixel(x, kRow + 5), theme.theme().base.rgb());
    }
}

// Embeds are cards the server sends for links and media. measure() reserved space
// for them long before anything drew one, so a message with a link preview was a
// correctly-sized blank gap. This checks the card actually paints and that its
// height matches what was reserved, since a mismatch overflows into the next row.
void TestShell::test_embed_cards_paint_within_their_reserved_space() {
    nimbus::ThemeManager theme;
    nimbus::Store store;

    nimbus::Message message;
    message.id = QStringLiteral("01EMBEDDED");
    message.channelId = QStringLiteral("01CHANNEL");
    message.authorId = QStringLiteral("01OTHER");
    message.content = QStringLiteral("look at this");

    nimbus::Embed embed;
    embed.type = QStringLiteral("Website");
    embed.siteName = QStringLiteral("stoat.chat");
    embed.title = QStringLiteral("A post about the websocket fix");
    embed.description =
        QStringLiteral("The query-string and MASK-bit bugs are both in, and the stream holds a "
                       "Ready frame without truncating it in the middle of a deflate block.");
    embed.url = QStringLiteral("https://stoat.chat/posts/1");
    embed.colour = QStringLiteral("#d0d1d4");
    message.embeds << embed;
    store.upsertMessage(message);

    nimbus::MessageModel model;
    model.setStore(&store);
    model.setChannel(QStringLiteral("01CHANNEL"));
    QCOMPARE(model.rowCount(), 1);

    nimbus::MessageDelegate delegate(&theme);
    delegate.setStore(&store);

    QVERIFY(model.messageAt(0));
    QCOMPARE(model.messageAt(0)->embeds.size(), 1);
    const QModelIndex index = model.index(0, 0);
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 720, 400);

    const QSize hinted = delegate.sizeHint(option, index);
    QVERIFY(hinted.height() > 40);

    QImage image(option.rect.size(), QImage::Format_ARGB32);
    image.fill(theme.theme().base.toRgb());
    QPainter painter(&image);
    delegate.paint(&painter, option, index);
    painter.end();
    image.save(QStringLiteral("/tmp/nimbus-embed.png"));

    // The card is a fill distinct from both the transcript background and the
    // message text, somewhere below the content line.
    const QRgb cardFill = theme.theme().sunken.rgb();
    int cardPixels = 0;
    for (int y = 40; y < hinted.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) == cardFill) ++cardPixels;
        }
    }
    QVERIFY2(cardPixels > 500, "the embed card painted no fill");

    // The accent bar, which is the one saturated thing on the card. It sits at the
    // card's left edge, and the card starts at the left edge now the transcript has
    // no avatar gutter -- so the first few columns, not a hardcoded offset.
    const QColor accent(embed.colour);
    int accentPixels = 0;
    for (int y = 40; y < hinted.height(); ++y) {
        for (int x = 0; x < 6; ++x) {
            if (image.pixel(x, y) == accent.rgb()) ++accentPixels;
        }
    }
    QVERIFY2(accentPixels > 20, "the embed accent bar was not drawn");

    // Nothing painted below the row the layout reserved.
    for (int y = hinted.height() + 2; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) != theme.theme().base.rgb()) {
                QFAIL(qPrintable(QStringLiteral("embed overflowed its row at y=%1").arg(y)));
            }
        }
    }
}

// The DM list is meant to show faces, not letters. The delegate draws
// synchronously, so a row painted before its image lands draws the "@" fallback and
// has to be repainted when the image arrives -- the assertion here is on the paint
// itself, since that is the half that decides what is on screen.
void TestShell::test_dm_rows_draw_an_avatar_instead_of_a_letter() {
    nimbus::ThemeManager theme;
    nimbus::Store store;
    store.setSelfId(QStringLiteral("01SELF"));

    nimbus::User other;
    other.id = QStringLiteral("01OTHER");
    other.username = QStringLiteral("stoatfan");
    other.displayName = QStringLiteral("stoatfan");
    other.avatar.id = QStringLiteral("01AVATAR");
    other.avatar.tag = QStringLiteral("avatars");
    store.upsertUser(other);

    nimbus::Channel dm;
    dm.id = QStringLiteral("01DM");
    dm.type = nimbus::ChannelType::DirectMessage;
    dm.recipients = {QStringLiteral("01SELF"), QStringLiteral("01OTHER")};
    store.upsertChannel(dm);

    nimbus::ChannelListModel model(&store);
    model.setDirectsOnly(true);
    // A CDN host, so File::url() yields something the delegate can ask for.
    model.setCdnUrl(QStringLiteral("https://cdn.example"));
    QCOMPARE(model.rowCount(), 2); // heading, then the DM

    const QModelIndex row = model.index(1, 0);
    QCOMPARE(row.data(nimbus::ChannelListModel::NameRole).toString(), QStringLiteral("stoatfan"));
    QVERIFY(row.data(nimbus::ChannelListModel::DirectRole).toBool());
    QVERIFY(!row.data(nimbus::ChannelListModel::IconUrlRole).toString().isEmpty());

    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 208, 40);
    nimbus::ChannelListDelegate delegate(&theme);
    const QSize size = delegate.sizeHint(option, row);
    option.rect = QRect(QPoint(0, 0), size);

    const QRgb avatarFill = QColor(Qt::magenta).rgb();
    const auto countFill = [&](const QImage& image, QRgb fill) {
        int count = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixel(x, y) == fill) ++count;
            }
        }
        return count;
    };

    // No provider: the row falls back to the "@" letter.
    QImage letter(size, QImage::Format_ARGB32);
    letter.fill(theme.theme().surface.toRgb());
    {
        QPainter painter(&letter);
        delegate.paint(&painter, option, row);
    }

    // With a provider, the same row paints a picture in the glyph slot instead.
    delegate.setAvatarProvider([avatarFill](const QString&, int) {
        QPixmap pixmap(14, 14);
        pixmap.fill(QColor::fromRgb(avatarFill));
        return pixmap;
    });
    QImage face(size, QImage::Format_ARGB32);
    face.fill(theme.theme().surface.toRgb());
    {
        QPainter painter(&face);
        delegate.paint(&painter, option, row);
    }

    QVERIFY2(countFill(letter, avatarFill) == 0, "the fallback row drew an avatar it never had");
    // 14x14 at the glyph slot, allow for the rounded clip trimming the corners.
    QVERIFY2(countFill(face, avatarFill) > 150, "the avatar was not drawn in place of the @");
    face.save(QStringLiteral("/tmp/nimbus-dm-face.png"));
}

// A reply names the message it replies to, and the quoted text is frequently not
// held at all because history is paged -- so both the found and the missing cases are
// asserted here.
void TestShell::test_replies_quote_the_message_they_answer() {
    nimbus::ThemeManager theme;
    nimbus::Store store;

    nimbus::User author;
    author.id = QStringLiteral("01OTHER");
    author.username = QStringLiteral("stoatfan");
    author.displayName = QStringLiteral("stoatfan");
    store.upsertUser(author);

    nimbus::Message quoted;
    quoted.id = QStringLiteral("01QUOTED");
    quoted.channelId = QStringLiteral("01CHANNEL");
    quoted.authorId = author.id;
    quoted.content = QStringLiteral("the websocket fix is in");
    store.upsertMessage(quoted);

    nimbus::Message reply;
    reply.id = QStringLiteral("01REPLY");
    reply.channelId = QStringLiteral("01CHANNEL");
    reply.authorId = QStringLiteral("01SELF");
    reply.content = QStringLiteral("shipping it now");
    reply.replyIds = {quoted.id};
    store.upsertMessage(reply);

    // A second reply pointing at something this client does not hold.
    nimbus::Message orphan;
    orphan.id = QStringLiteral("01ORPHAN");
    orphan.channelId = QStringLiteral("01CHANNEL");
    orphan.authorId = QStringLiteral("01SELF");
    orphan.content = QStringLiteral("what did you mean?");
    orphan.replyIds = {QStringLiteral("01NEVERLOADED")};
    store.upsertMessage(orphan);

    nimbus::MessageModel model(&store);
    model.setStore(&store);
    model.setChannel(QStringLiteral("01CHANNEL"));
    QCOMPARE(model.rowCount(), 3);

    // By id, not by row: the store orders by id and these are not real ULIDs, so
    // the index of each message is not the order they were created in.
    const auto rowOf = [&](const QString& messageId) {
        for (int row = 0; row < model.rowCount(); ++row) {
            if (model.messageAt(row) && model.messageAt(row)->id == messageId) return row;
        }
        return -1;
    };
    const int orphanRow = rowOf(orphan.id);
    const int replyRow = rowOf(reply.id);
    QVERIFY(orphanRow >= 0);
    QVERIFY(replyRow >= 0);
    QVERIFY(orphanRow != replyRow);

    nimbus::MessageDelegate delegate(&theme);
    delegate.setStore(&store);

    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 620, 400);

    const auto rowShot = [&](int row) {
        QImage image(option.rect.size(), QImage::Format_ARGB32);
        image.fill(theme.theme().base.toRgb());
        QPainter painter(&image);
        delegate.paint(&painter, option, model.index(row, 0));
        painter.end();
        return image;
    };

    QImage orphanShot = rowShot(orphanRow);
    QImage replyShot = rowShot(replyRow);

    // Distinct pixel counts: the two rows paint different text above their bodies.
    QVERIFY(countDistinct(orphanShot) > 4);
    QVERIFY(countDistinct(replyShot) > 4);
    QVERIFY(orphanShot != replyShot);

    // Both reserve a line for the quote, whether or not the quoted message was
    // loaded -- otherwise the missing case is a shorter row that overlaps the next.
    QCOMPARE(delegate.sizeHint(option, model.index(orphanRow, 0)).height(),
             delegate.sizeHint(option, model.index(replyRow, 0)).height());
    replyShot.save(QStringLiteral("/tmp/nimbus-reply.png"));
}

// Avatars only render if the model is given a CDN host: File::url() concatenates
// onto whatever base it is handed, so an unset host yields a relative path that no
// request can resolve, and every row falls back to a monogram. This is the check
// that the base actually reaches the models.
void TestShell::test_avatar_urls_are_absolute_once_the_cdn_host_is_known() {
    nimbus::Store store;
    store.setSelfId(QStringLiteral("01SELF"));

    nimbus::User self;
    self.id = QStringLiteral("01SELF");
    self.username = QStringLiteral("lexiear");
    self.displayName = QStringLiteral("lex");
    self.avatar.id = QStringLiteral("01AVATAR");
    // The tag is the path variant on this API, not a file extension: a user avatar
    // comes back with tag "avatars".
    self.avatar.tag = QStringLiteral("avatars");
    store.upsertUser(self);

    // No host yet, and the url is not merely empty: File::url concatenates onto the
    // base it is given, so the result is a bare "/avatars/..." path. That is the
    // shape that looks like a working url in a log and resolves to nothing, which is
    // exactly what every avatar fell back to on a live session.
    nimbus::RailModel withoutHost(&store);
    const QString relative = withoutHost.index(0, 0).data(nimbus::RailModel::IconUrlRole).toString();
    QVERIFY(relative.startsWith(QStringLiteral("/avatars/")));
    QVERIFY(!relative.contains(QStringLiteral("://")));

    // The tag is the path variant, so an avatar is /avatars/{id} and an emoji is
    // /emojis/{id}.
    withoutHost.setCdnUrl(QStringLiteral("https://cdn.stoatusercontent.com"));
    QCOMPARE(withoutHost.index(0, 0).data(nimbus::RailModel::IconUrlRole).toString(),
             QStringLiteral("https://cdn.stoatusercontent.com/avatars/01AVATAR"));

    // The DM list has to reach the same conclusion, from its own model.
    nimbus::Channel dm;
    dm.id = QStringLiteral("01DM");
    dm.type = nimbus::ChannelType::DirectMessage;
    dm.recipients = {QStringLiteral("01SELF"), QStringLiteral("01SELF")};
    store.upsertChannel(dm);

    nimbus::ChannelListModel directs(&store);
    directs.setCdnUrl(QStringLiteral("https://cdn.stoatusercontent.com"));
    directs.setDirectsOnly(true);
    // A one-recipient direct is the account's own notes, which carries no avatar.
    // The point here is only that the host is applied without erroring out.
    QVERIFY(directs.rowCount() >= 1);
}

// A reaction pill prints the shortcode, resolved through the server's emoji, and is
// measurably wider than an unresolved id would be because ":debian:" is longer than
// six characters of ULID -- so the width of the pill is the assertion, not a string
// comparison against text the delegate never hands back.
void TestShell::test_reaction_pills_resolve_ids_to_shortcodes() {
    const QString emojiId = QStringLiteral("01EMOJI");
    const QString authorId = QStringLiteral("01OTHER");

    int rows = 0;
    const auto pillWidth = [&](bool withEmoji) -> int {
        nimbus::ThemeManager theme;
        nimbus::Store store;

        nimbus::User author;
        author.id = authorId;
        author.username = QStringLiteral("stoatfan");
        author.displayName = QStringLiteral("stoatfan");
        store.upsertUser(author);
        store.setSelfId(QStringLiteral("01SELF"));

        if (withEmoji) {
            store.upsertEmoji(nimbus::Emoji{emojiId, QStringLiteral("debian"),
                                             QStringLiteral("01SERVER")});
        }

        nimbus::Message message;
        message.id = QStringLiteral("01MESSAGE");
        message.channelId = QStringLiteral("01CHANNEL");
        message.authorId = authorId;
        message.content = QStringLiteral("nice");
        message.reactions.insert(emojiId, QStringList{authorId});
        store.upsertMessage(message);

        nimbus::MessageModel model;
        model.setStore(&store);
        model.setChannel(QStringLiteral("01CHANNEL"));
        rows = model.rowCount();

        nimbus::MessageDelegate delegate(&theme);
        delegate.setStore(&store);

        QStyleOptionViewItem option;
        option.rect = QRect(0, 0, 600, 200);
        QImage image(option.rect.size(), QImage::Format_ARGB32);
        image.fill(theme.theme().base.toRgb());
        QPainter painter(&image);
        delegate.paint(&painter, option, model.index(0, 0));
        painter.end();

        const QRgb resting = theme.theme().raised.rgb();
        const QRgb mine = theme.theme().sunken.rgb();

        // Counted, not located by edge: antialiased body text blends through the
        // pill's fill colour on its way to the background, so a scan for "the last
        // column with that colour" finds the text instead of the pill. The text is
        // byte-identical in both runs, so the difference in area is the pill.
        int fill = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QRgb pixel = image.pixel(x, y);
                if (pixel == resting || pixel == mine) ++fill;
            }
        }
        return fill;
    };

    const int without = pillWidth(false);
    const int with = pillWidth(true);
    QCOMPARE(rows, 1);
    QVERIFY(without > 0);
    QVERIFY2(with > without,
             qPrintable(QStringLiteral("the pill did not grow when the id resolved to a "
                                       "shortcode: %1 fill pixels without, %1 with")
                            .arg(without)
                            .arg(with)));
}

// The row is +, field, emoji, gif, send. The plus and the arrow are deliberately not
// adjacent, so this pins the order as well as the wiring: a bare glyph in the wrong
// place is a mis-click, not a cosmetic difference.
void TestShell::test_composer_row_has_attach_and_send_at_opposite_ends() {
    nimbus::Composer composer;
    composer.resize(500, 28);
    composer.setChannel(QStringLiteral("01CHANNEL"));
    composer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&composer, 2000));

    auto* attach = composer.findChild<QToolButton*>(QStringLiteral("composerAttach"));
    auto* emoji = composer.findChild<QToolButton*>(QStringLiteral("composerEmoji"));
    auto* gif = composer.findChild<QToolButton*>(QStringLiteral("composerGif"));
    auto* send = composer.findChild<QToolButton*>(QStringLiteral("composerSend"));
    auto* input = composer.findChild<QLineEdit*>(QStringLiteral("composerInput"));
    QVERIFY(attach && emoji && gif && send && input);

    // Left to right: plus, field, emoji, gif, arrow.
    QVERIFY(attach->x() < input->x());
    QVERIFY(input->x() < emoji->x());
    QVERIFY(emoji->x() < gif->x());
    QVERIFY(gif->x() < send->x());

    // The plus is on the opposite end from the arrow, not beside it.
    QVERIFY2(attach->x() + attach->width() < emoji->x(),
             "the attach control is next to the other controls rather than at the end");

    // All four are bare: same size, and no plate behind any of them.
    for (QToolButton* button : {attach, emoji, gif, send}) {
        QCOMPARE(button->width(), 20);
        QCOMPARE(button->height(), 20);
    }

    // The plus asks for attachments; it does not open a dialog itself.
    int attachRequests = 0;
    connect(&composer, &nimbus::Composer::attachRequested, this, [&] { ++attachRequests; });
    attach->click();
    QCOMPARE(attachRequests, 1);

    // The emoji control opens its own picker rather than asking someone else to.
    emoji->click();
    QTest::qWait(20);
    auto* picker = composer.findChild<nimbus::EmojiPicker*>();
    QVERIFY(picker);
    QVERIFY2(picker->isVisible(), "the emoji control did not open the picker");
}

// The send control is a bare glyph: no plate, no border, no hover fill. A filled
// square behind the arrow is invisible in a size assertion and obvious on screen, so
// this checks the pixels around the icon are the composer's own background.
void TestShell::test_send_control_is_a_bare_glyph() {
    nimbus::ThemeManager theme;
    nimbus::Composer composer;
    composer.resize(500, 28);
    composer.setChannel(QStringLiteral("01CHANNEL"));
    composer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&composer, 2000));

    auto* send = composer.findChild<QToolButton*>(QStringLiteral("composerSend"));
    QVERIFY(send);
    QCOMPARE(send->width(), 20);
    QCOMPARE(send->height(), 20);

    // A text cursor in the field so the control is enabled and its glyph is drawn.
    QTest::keyClicks(composer.findChild<QLineEdit*>(QStringLiteral("composerInput")), "hi");
    QTest::qWait(20);
    QVERIFY(send->isEnabled());

    const QImage shot = composer.grab().toImage();

    // The button's own geometry, mapped into the grab.
    const QPoint origin = send->mapTo(&composer, QPoint(0, 0));
    const QRect box = QRect(origin, send->size()).intersected(shot.rect());
    QVERIFY(!box.isEmpty());

    // Count the colours inside the button. A plate is one flat fill plus the glyph,
    // so three or fewer distinct colours means it is still there; a bare glyph on the
    // composer's own background leaves only the antialiased arrow's own shades, and
    // more of them than a plate would produce.
    QSet<QRgb> inside;
    for (int y = box.top(); y <= box.bottom(); ++y) {
        for (int x = box.left(); x <= box.right(); ++x) inside.insert(shot.pixel(x, y));
    }

    // The corners are what a plate would cover first, and on a bare button they are
    // the same colour as the composer's background a few pixels outside it.
    const QRgb outside = shot.pixel(box.left() - 3, box.center().y());
    QCOMPARE(shot.pixel(box.left(), box.top()), outside);
    QCOMPARE(shot.pixel(box.right(), box.bottom()), outside);

    // And the glyph is actually drawn, or "bare" would also mean "invisible".
    QVERIFY2(inside.size() > 2, "the send glyph was not drawn");
}

// The bar is capped and centred rather than stretched across the window: one line of
// text on an 850px input is a bad target, and a full-width bar does not line up with
// the transcript's text column, which stops at the same width.
void TestShell::test_text_bar_is_capped_and_centred() {
    nimbus::Composer composer;
    composer.setChannel(QStringLiteral("01CHANNEL"));
    composer.resize(1100, 28);
    composer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&composer, 2000));

    auto* input = composer.findChild<QLineEdit*>(QStringLiteral("composerInput"));
    auto* send = composer.findChild<QToolButton*>(QStringLiteral("composerSend"));
    auto* attach = composer.findChild<QToolButton*>(QStringLiteral("composerAttach"));
    QVERIFY(input && send && attach);
    QVERIFY(input->isVisible());

    constexpr int kCap = 780;
    // The group runs from the plus to the arrow, not just the field.
    const int groupWidth = (send->x() + send->width()) - attach->x();
    QVERIFY2(groupWidth <= kCap, qPrintable(QStringLiteral("bar group is %1 wide, cap is %2")
                                               .arg(groupWidth)
                                               .arg(kCap)));

    // Centred: roughly equal margin either side of the group.
    const int left = attach->x();
    const int right = composer.width() - (send->x() + send->width());
    QVERIFY2(qAbs(left - right) <= 2,
             qPrintable(QStringLiteral("bar is off-centre: %1 left, %2 right").arg(left).arg(right)));

    // A narrow pane shrinks the bar instead of clipping it.
    composer.resize(420, 28);
    QTest::qWait(20);
    QVERIFY2((send->x() + send->width()) - attach->x() <= 420,
             "the bar did not shrink to fit a narrow pane");
    QVERIFY2(attach->x() >= 0, "the bar overflowed the left edge when narrow");
}

void TestShell::test_composer_sends_and_resets() {
    nimbus::Composer composer;
    composer.resize(400, 28);
    composer.setChannel(QStringLiteral("01CHANNEL"));

    QString sent;
    connect(&composer, &nimbus::Composer::sendMessage, this,
            [&](const QString& content) { sent = content; });

    // Nothing typed, nothing sent: the control is disabled rather than sending "".
    composer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&composer, 2000));
    auto* send = composer.findChild<QToolButton*>(QStringLiteral("composerSend"));
    QVERIFY(send);
    QVERIFY(!send->isEnabled());

    QTest::keyClicks(composer.findChild<QLineEdit*>(QStringLiteral("composerInput")),
                     "hello there");
    QTest::qWait(20);
    QVERIFY(send->isEnabled());

    send->click();
    QCOMPARE(sent, QStringLiteral("hello there"));
    // Cleared after sending, so the next message is not appended to this one.
    QCOMPARE(composer.content(), QString());
    QVERIFY(!send->isEnabled());
}

void TestShell::test_composer_is_disabled_with_no_channel() {
    nimbus::Composer composer;
    composer.resize(400, 36);
    composer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&composer, 2000));

    QVERIFY(!composer.isEnabled());
    auto* input = composer.findChild<QLineEdit*>(QStringLiteral("composerInput"));
    QVERIFY(input);
    // On screen even with nothing open. Hiding it left a bare strip and the client
    // looked like it had no input at all.
    QVERIFY(input->isVisible());
    // The bar is one control with the prompt on it, not a label beside a field.
    QCOMPARE(input->placeholderText(), QStringLiteral("Open a channel to send a message"));

    // Opening a channel is what makes it usable, and the prompt changes to the one
    // that applies to a channel you can actually post in.
    composer.setChannel(QStringLiteral("01CHANNEL"));
    QVERIFY(composer.isEnabled());
    QVERIFY(input->isVisible());
    QCOMPARE(input->placeholderText(), QStringLiteral("Enter text here"));

    // Closing the channel again leaves the bar up, greyed, saying why.
    composer.setChannel(QString());
    QVERIFY(!composer.isEnabled());
    QVERIFY(input->isVisible());
    QCOMPARE(input->placeholderText(), QStringLiteral("Open a channel to send a message"));
}

void TestShell::test_composer_edit_replaces_and_escapes() {
    nimbus::Composer composer;
    composer.resize(400, 36);
    composer.show();
    QVERIFY(QTest::qWaitForWindowExposed(&composer, 2000));
    composer.setChannel(QStringLiteral("01CHANNEL"));

    QString editedId;
    QString editedText;
    int cancelled = 0;
    connect(&composer, &nimbus::Composer::editMessage, this,
            [&](const QString& id, const QString& text) { editedId = id; editedText = text; });
    connect(&composer, &nimbus::Composer::cancelEdit, this, [&] { ++cancelled; });

    composer.beginEdit(QStringLiteral("01MESSAGE"), QStringLiteral("original"));
    QVERIFY(composer.isEditing());
    QCOMPARE(composer.content(), QStringLiteral("original"));

    // Escape abandons the edit and emits nothing to the server.
    QTest::keyClick(composer.findChild<QLineEdit*>(QStringLiteral("composerInput")),
                    Qt::Key_Escape);
    QVERIFY(!composer.isEditing());
    QVERIFY(composer.content().isEmpty());
    QCOMPARE(editedId, QString());
    QCOMPARE(cancelled, 1);
}

void TestShell::test_home_opens_the_direct_message_list() {
    auto* store = buildStore(this);

    nimbus::Channel dm;
    dm.id = ulid('d', 1);
    dm.type = nimbus::ChannelType::DirectMessage;
    dm.recipients = {ulid('u'), ulid('v')};
    store->upsertChannel(dm);

    nimbus::Channel other;
    other.id = ulid('d', 2);
    other.type = nimbus::ChannelType::DirectMessage;
    other.recipients = {ulid('u'), ulid('w')};
    store->upsertChannel(other);

    nimbus::User stranger;
    stranger.id = ulid('w');
    stranger.username = QStringLiteral("kestrel");
    stranger.displayName = QStringLiteral("kestrel");
    store->upsertUser(stranger);

    auto* window = new nimbus::ChatWindow;
    window->setStore(store);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window, 2000));

    auto* rail = window->findChild<nimbus::ServerRail*>();
    auto* channels = window->findChild<nimbus::ChannelList*>();
    QVERIFY(rail && channels);

    QMetaObject::invokeMethod(rail, "homeSelected");
    QTest::qWait(50);

    const QImage shot = window->grab().toImage();
    shot.save(QStringLiteral("/tmp/nimbus-dms.png"));
    QVERIFY(countDistinct(shot) > 8);

    // The pane is people, not channels, and the rail still marks the account open.
    QCOMPARE(rail->currentIndex().data(nimbus::RailModel::KindRole).toInt(),
             int(nimbus::RailKind::Home));
    QCOMPARE(window->windowTitle(), QStringLiteral("lex"));

    window->hide();
    delete window;
}

void TestShell::test_rail_has_home_servers_and_channels() {
    auto* store = buildStore(this);
    nimbus::RailModel model(store);
    QVERIFY(model.rowCount() >= 1);
    QCOMPARE(model.index(0, 0).data(nimbus::RailModel::KindRole).toInt(),
             int(nimbus::RailKind::Home));
}

// The rail's top entry is the account, not the notes channel: a monogram labelled
// "Notes" reads as a server called N and offers no route to the direct messages.
void TestShell::test_rail_head_is_your_own_account() {
    auto* store = buildStore(this);
    nimbus::RailModel model(store);

    QCOMPARE(model.rowCount(), 2); // the account, then one server
    QCOMPARE(model.index(0, 0).data(nimbus::RailModel::KindRole).toInt(),
             int(nimbus::RailKind::Home));
    QCOMPARE(model.index(0, 0).data(nimbus::RailModel::IdRole).toString(), store->selfId());
    QCOMPARE(model.index(0, 0).data(nimbus::RailModel::NameRole).toString(), QStringLiteral("lex"));
    QCOMPARE(model.index(1, 0).data(nimbus::RailModel::KindRole).toInt(),
             int(nimbus::RailKind::Server));

    // No Notes entry survives in its place.
    for (int row = 0; row < model.rowCount(); ++row) {
        QVERIFY(model.index(row, 0).data(nimbus::RailModel::NameRole).toString() !=
                QStringLiteral("Notes"));
    }
}

void TestShell::test_directs_only_lists_dms_and_not_channels() {
    auto* store = buildStore(this);

    nimbus::Channel dm;
    dm.id = ulid('d', 1);
    dm.type = nimbus::ChannelType::DirectMessage;
    dm.recipients = {ulid('u'), ulid('v')};
    store->upsertChannel(dm);

    nimbus::ChannelListModel model(store);
    model.setDirectsOnly(true);
    model.setServerId(ulid('s')); // must not leak the server's channels in

    // A heading, then the one DM. The server's three channels and two category
    // headings are all absent.
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.index(0, 0).data(nimbus::ChannelListModel::KindRole).toInt(),
             int(nimbus::ChannelRowKind::Category));
    QCOMPARE(model.index(0, 0).data(nimbus::ChannelListModel::NameRole).toString(),
             QStringLiteral("Direct Messages"));

    const QModelIndex index = model.index(1, 0);
    QCOMPARE(index.data(nimbus::ChannelListModel::IdRole).toString(), dm.id);
    QCOMPARE(index.data(nimbus::ChannelListModel::NameRole).toString(),
             QStringLiteral("stoatfan"));
    QVERIFY(index.data(nimbus::ChannelListModel::DirectRole).toBool());

    // Only the one heading above; no server categories leak in.
    int headings = 0;
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.index(row, 0).data(nimbus::ChannelListModel::KindRole).toInt() ==
            int(nimbus::ChannelRowKind::Category)) {
            ++headings;
        }
    }
    QCOMPARE(headings, 1);

    // Turning it off goes back to the server's channels.
    model.setDirectsOnly(false);
    QVERIFY(model.rowCount() > 1);
}

void TestShell::test_channel_list_orders_by_category() {
    auto* store = buildStore(this);
    nimbus::ChannelListModel model(store);
    model.setServerId(ulid('s'));

    QStringList headings;
    QStringList channels;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, 0);
        if (index.data(nimbus::ChannelListModel::KindRole).toInt() ==
            int(nimbus::ChannelRowKind::Category)) {
            headings << index.data(nimbus::ChannelListModel::NameRole).toString();
        } else {
            channels << index.data(nimbus::ChannelListModel::NameRole).toString();
        }
    }

    QCOMPARE(headings, QStringList({QStringLiteral("Text channels"), QStringLiteral("Staff")}));
    // Server order, not alphabetical: the API offers no other ordering.
    QCOMPARE(channels,
             QStringList({QStringLiteral("general"), QStringLiteral("stoat-updates"),
                          QStringLiteral("mod-log")}));
}

void TestShell::test_rail_dims_unselected_without_a_pill() {
    auto* store = buildStore(this);
    nimbus::ThemeManager theme;
    nimbus::RailModel model(store);

    const int row = model.rowForId(ulid('s'));
    QVERIFY(row >= 0);
    const QModelIndex index = model.index(row, 0);

    nimbus::RailDelegate delegate(&theme);
    const QSize size = delegate.sizeHint(QStyleOptionViewItem(), index);
    const int side = theme.theme().railIconSize;
    const QRect box((size.width() - side) / 2, (size.height() - side) / 2, side, side);
    const QColor background = theme.theme().surface;
    const QRgb bgRgb = background.rgb();

    // Selected: the server row itself, drawn at full strength.
    delegate.setSelectedId(ulid('s'));
    const QImage selected = renderRow(delegate, index, size, background);

    // Monogram rather than an image: with no AvatarCache the row is a rounded
    // square and a letter, so the middle of the box is never the background.
    QVERIFY(selected.pixelColor(box.center()) != background);

    // No pill, no outline, no border: every pixel outside the icon box is
    // untouched. This is the standing preference, stated as pixels. A colour
    // count cannot state it, because the letter's antialiasing alone exceeds any
    // limit low enough for a fill to stay under.
    for (int y = 0; y < selected.height(); ++y) {
        for (int x = 0; x < selected.width(); ++x) {
            if (box.contains(x, y)) continue;
            QVERIFY2(selected.pixel(x, y) == bgRgb, qPrintable(QStringLiteral("row painted at x=%1 y=%2, outside the icon box").arg(x).arg(y)));
        }
    }

    // The other half of the treatment: with the selection elsewhere, this row
    // drops back toward the background.
    delegate.setSelectedId(ulid('h')); // the notes entry, i.e. a different row
    const QImage dimmed = renderRow(delegate, index, size, background);

    QVERIFY(meanLuma(dimmed, box) < meanLuma(selected, box));
    // 55% opacity is a stated number; a test that only wants "less bright" would
    // pass against 5%.
    QVERIFY(meanLuma(dimmed, box) > meanLuma(selected, box) * 0.5);
}

QTEST_MAIN(TestShell)
#include "test_shell.moc"
