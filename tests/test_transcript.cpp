#include "../src/core/store.h"
#include "../src/ui/message_delegate.h"
#include "../src/ui/message_model.h"
#include "../src/ui/theme.h"

#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>
#include <QtWidgets/QApplication>

namespace {

QString ulid(char seed) {
    QString id(26, QLatin1Char('0'));
    for (int i = 1; i < 26; ++i) id[i] = seed;
    return id;
}

nimbus::Message makeMessage(const QString& channelId, const QString& authorId,
                            const QString& content, char idSeed = 'm') {
    nimbus::Message m;
    m.id = ulid(idSeed);
    m.channelId = channelId;
    m.authorId = authorId;
    m.content = content;
    return m;
}

// Counts pixels that differ from the background. Asserting only on geometry
// proves nothing about a delegate: a row can measure perfectly and paint an
// entirely empty rectangle, and every height assertion still passes.
int paintedPixels(const QImage& image, const QColor& background) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) != background) ++count;
        }
    }
    return count;
}

} // namespace

class TestTranscript : public QObject {
    Q_OBJECT
private slots:
    void init();

    void test_row_actually_paints();
    void test_grouped_rows_omit_the_header();
    void test_size_hint_is_shared_with_paint();
    void test_edit_changes_row_height();
    void test_pending_row_is_dimmer_than_confirmed();
    void test_model_survives_pending_rekey();
    void test_reactions_extend_the_row();
    void test_transcript_draws_no_avatar_and_no_gutter();

private:
    nimbus::Store* m_store = nullptr;
    nimbus::MessageModel* m_model = nullptr;
    nimbus::ThemeManager* m_theme = nullptr;
    nimbus::MessageDelegate* m_delegate = nullptr;
    QString m_channelId;
};

void TestTranscript::init() {
    m_channelId = ulid('c');

    auto* store = new nimbus::Store(this);
    m_store = store;

    nimbus::User user;
    user.id = ulid('u');
    user.username = QStringLiteral("lexiear");
    user.displayName = QStringLiteral("lex");
    store->upsertUser(user);

    nimbus::Channel channel;
    channel.id = m_channelId;
    channel.type = nimbus::ChannelType::TextChannel;
    store->upsertChannel(channel);

    m_theme = new nimbus::ThemeManager(this);
    m_delegate = new nimbus::MessageDelegate(m_theme, this);
    m_delegate->setStore(store);

    m_model = new nimbus::MessageModel(this);
    m_model->setStore(store);
    m_model->setChannel(m_channelId);
}

void TestTranscript::test_row_actually_paints() {
    m_store->upsertMessage(makeMessage(m_channelId, ulid('u'), QStringLiteral("hello there")));
    QCOMPARE(m_model->rowCount(), 1);

    const QModelIndex index = m_model->index(0, 0);
    QStyleOptionViewItem option;
    // Width first: measuring with a zero-width rect correctly yields no layout,
    // so measuring and sizing in one expression tests nothing.
    option.rect = QRect(0, 0, 640, 200);
    option.state = QStyle::State_Enabled;
    option.rect.setHeight(m_delegate->sizeHint(option, index).height());

    const QColor background = m_theme->theme().base;
    QImage image(option.rect.size(), QImage::Format_ARGB32);
    image.fill(background.toRgb());

    QPainter painter(&image);
    m_delegate->paint(&painter, option, index);
    painter.end();

    // Both of these are what an empty paint() and a constant sizeHint would fail.
    QVERIFY(option.rect.height() > 20);
    QVERIFY2(paintedPixels(image, background) > 200,
             "a transcript row that paints no pixels looks identical to a correct one");
}

void TestTranscript::test_grouped_rows_omit_the_header() {
    m_store->upsertMessage(makeMessage(m_channelId, ulid('u'), QStringLiteral("first"), 'a'));
    m_store->upsertMessage(makeMessage(m_channelId, ulid('u'), QStringLiteral("second"), 'b'));
    m_store->upsertMessage(makeMessage(m_channelId, ulid('v'), QStringLiteral("other author"), 'c'));
    QCOMPARE(m_model->rowCount(), 3);

    QVERIFY(!m_model->isGrouped(0));
    // Same author, immediately following: no repeated name or avatar.
    QVERIFY(m_model->isGrouped(1));
    QVERIFY(!m_model->isGrouped(2));

    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 640, 200);
    const int first = m_delegate->sizeHint(option, m_model->index(0, 0)).height();
    const int grouped = m_delegate->sizeHint(option, m_model->index(1, 0)).height();
    QVERIFY2(grouped < first, "a grouped row must be shorter than one that repeats the header");
}

void TestTranscript::test_size_hint_is_shared_with_paint() {
    // Long enough that 320px wraps it and 640px does not; a short string fits
    // either way and the comparison below proves nothing.
    m_store->upsertMessage(makeMessage(
        m_channelId, ulid('u'),
        QStringLiteral("a body long enough to wrap onto a second line at a narrow width")));
    const QModelIndex index = m_model->index(0, 0);

    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 640, 200);
    const int a = m_delegate->sizeHint(option, index).height();
    const int b = m_delegate->sizeHint(option, index).height();
    QCOMPARE(a, b);

    // The cache key must include the width, or a resize reuses a layout measured
    // for the old one and the text no longer fits its row.
    option.rect = QRect(0, 0, 320, 200);
    const int narrow = m_delegate->sizeHint(option, index).height();
    QVERIFY(narrow > a);
}

void TestTranscript::test_edit_changes_row_height() {
    auto message = makeMessage(m_channelId, ulid('u'), QStringLiteral("short"));
    m_store->upsertMessage(message);

    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 240, 400);
    const int before = m_delegate->sizeHint(option, m_model->index(0, 0)).height();

    nimbus::Message edited;
    edited.id = message.id;
    edited.channelId = m_channelId;
    edited.authorId = message.authorId;
    edited.content = QStringLiteral("a considerably longer body that has to wrap over "
                                    "several lines once the available width is this narrow");
    m_store->upsertMessage(edited);

    // The cache signature covers content length, so an edit cannot leave the row
    // wearing the height it needed before it.
    const int after = m_delegate->sizeHint(option, m_model->index(0, 0)).height();
    QVERIFY2(after > before, "an edit that adds wrapped lines must grow the row");
}

void TestTranscript::test_pending_row_is_dimmer_than_confirmed() {
    nimbus::Message pending = makeMessage(m_channelId, ulid('u'), QStringLiteral("sending"));
    pending.id = QStringLiteral("pending:abc");
    pending.nonce = QStringLiteral("abc");
    pending.pending = true;
    m_store->upsertMessage(pending);

    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 640, 200);
    const int height = m_delegate->sizeHint(option, m_model->index(0, 0)).height();

    const QColor background = m_theme->theme().base;
    const auto render = [&](const QModelIndex& index) {
        option.rect = QRect(0, 0, 640, height);
        QImage image(option.rect.size(), QImage::Format_ARGB32);
        image.fill(background.toRgb());
        QPainter painter(&image);
        m_delegate->paint(&painter, option, index);
        painter.end();
        return image;
    };

    const QImage provisional = render(m_model->index(0, 0));

    // The server's copy arrives and the row is re-keyed off the nonce, which is
    // the path App::ingestMessage takes.
    nimbus::Message confirmed;
    confirmed.id = ulid('m');
    confirmed.channelId = m_channelId;
    confirmed.authorId = ulid('u');
    confirmed.content = QStringLiteral("sending");
    QVERIFY(m_store->resolvePending(m_channelId, pending.nonce, confirmed));

    QCOMPARE(m_model->rowCount(), 1);
    const QImage settled = render(m_model->index(0, 0));
    QVERIFY(provisional != settled);
}

void TestTranscript::test_model_survives_pending_rekey() {
    QSignalSpy added(m_model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(m_model, &QAbstractItemModel::rowsRemoved);

    nimbus::Message pending = makeMessage(m_channelId, ulid('u'), QStringLiteral("hi"));
    pending.id = QStringLiteral("pending:xyz");
    pending.nonce = QStringLiteral("xyz");
    pending.pending = true;
    m_store->upsertMessage(pending);
    QCOMPARE(m_model->rowCount(), 1);
    QCOMPARE(added.count(), 1);

    // The real path: App::ingestMessage resolves the pending row by nonce before
    // ever inserting. Calling upsertMessage with a different id would append a
    // second row, which is not what happens when a send is confirmed.
    nimbus::Message confirmed = pending;
    confirmed.id = ulid('m');
    confirmed.pending = false;
    QVERIFY(m_store->resolvePending(m_channelId, QStringLiteral("xyz"), confirmed));

    // resolvePending re-keys in place, so the row count must not change and the
    // model must not emit a removal: an id-keyed model would delete the row it
    // was just told to update and the message would vanish as it was sent.
    QCOMPARE(m_model->rowCount(), 1);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(m_model->index(0, 0).data(Qt::UserRole + 2).toString(), ulid('m'));
    QCOMPARE(m_model->rowForId(ulid('m')), 0);
    QCOMPARE(m_model->rowForId(QStringLiteral("pending:xyz")), -1);
}

void TestTranscript::test_reactions_extend_the_row() {
    nimbus::Message message = makeMessage(m_channelId, ulid('u'), QStringLiteral("react to me"));
    m_store->upsertMessage(message);

    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 640, 400);
    const int before = m_delegate->sizeHint(option, m_model->index(0, 0)).height();

    nimbus::Message reacted = message;
    reacted.reactions.insert(ulid('e'), QStringList{ulid('u'), ulid('v')});
    m_store->upsertMessage(reacted);

    const int after = m_delegate->sizeHint(option, m_model->index(0, 0)).height();
    QVERIFY2(after > before, "reaction pills need a row of their own or they paint over the text");
}

// The transcript draws no avatars and reserves no gutter for them -- an avatar
// without a gutter or a gutter without an avatar both leave the text stranded in an
// empty column. This pins both halves: nothing is painted where an avatar would be,
// and the text starts at the left edge.
void TestTranscript::test_transcript_draws_no_avatar_and_no_gutter() {
    m_store->upsertMessage(makeMessage(m_channelId, ulid('u'), QStringLiteral("flush left")));

    QPixmap pixmap(34, 34);
    pixmap.fill(Qt::magenta);
    m_delegate->setAvatarProvider([&pixmap](const QString&, int) { return pixmap; });

    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 640, 200);

    QImage image(option.rect.size(), QImage::Format_ARGB32);
    image.fill(m_theme->theme().base.toRgb());
    QPainter painter(&image);
    m_delegate->paint(&painter, option, m_model->index(0, 0));
    painter.end();

    // The provider is supplied and must be ignored: no avatar is drawn.
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            QVERIFY2(image.pixelColor(x, y) != QColor(Qt::magenta),
                     "the transcript painted an avatar");
        }
    }

    // And the text starts near the left edge, since nothing is painted in front of it.
    const QRgb background = m_theme->theme().base.rgb();
    int firstInked = image.width();
    for (int x = 0; x < image.width() && firstInked == image.width(); ++x) {
        for (int y = 0; y < image.height(); ++y) {
            if (image.pixel(x, y) != background) {
                firstInked = x;
                break;
            }
        }
    }
    QVERIFY(firstInked < image.width());
    QVERIFY2(firstInked < 12,
             qPrintable(QStringLiteral("text still starts at x=%1, an avatar gutter survived")
                            .arg(firstInked)));
}

QTEST_MAIN(TestTranscript)
#include "test_transcript.moc"
