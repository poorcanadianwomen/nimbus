#include "message_delegate.h"

#include "core/store.h"
#include "message_model.h"
#include "theme.h"

#include <QtCore/QJsonDocument>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>

namespace nimbus {

namespace {

// Roles duplicated from message_model.h rather than included, so the delegate has
// no dependency on the model implementation and can be driven by a fake in tests.
constexpr int kMessagePtrRole = Qt::UserRole + 1;
constexpr int kMessageIdRole = Qt::UserRole + 2;
constexpr int kGroupedRole = Qt::UserRole + 3;

constexpr int kMaxContentWidth = 780;
constexpr int kMaxReactionsPerRow = 6;
// Tall enough to read a 96x96 emote, small enough that a row of six is one line.
constexpr int kReactionEmoteSize = 18;

// Embeds are cards, not paragraphs: a colour bar down the leading edge, the site's
// name, a title, a description, and the image if the server sent one.
constexpr int kEmbedGap = 4;
constexpr int kEmbedPad = 8;
constexpr int kEmbedBar = 4;
constexpr int kEmbedImageMaxHeight = 220;
constexpr int kEmbedMinHeight = 28;

int wrapHeight(const QFontMetrics& fm, const QString& text, int width) {
    if (text.isEmpty()) return 0;
    return fm.boundingRect(QRect(0, 0, qMax(40, width), 10000), Qt::TextWordWrap, text).height();
}

} // namespace

MessageDelegate::MessageDelegate(ThemeManager* theme, QObject* parent)
    : QStyledItemDelegate(parent), m_theme(theme) {}

MessageDelegate::~MessageDelegate() = default;

void MessageDelegate::setStore(Store* store) {
    m_store = store;
    clearLayoutCache();
}

void MessageDelegate::setAvatarProvider(
    const std::function<QPixmap(const QString& userId, int size)>& provider) {
    m_avatar = provider;
}

void MessageDelegate::setIconProvider(
    const std::function<QPixmap(const QString& path, const QColor& colour, const QSize& size)>& provider) {
    m_icon = provider;
}

const Message* MessageDelegate::messageFor(const QModelIndex& index) const {
    if (!index.isValid()) return nullptr;
    const qulonglong raw = index.data(kMessagePtrRole).toULongLong();
    if (!raw) return nullptr;
    return reinterpret_cast<const Message*>(static_cast<quintptr>(raw));
}

QString MessageDelegate::cacheKey(const QModelIndex& index, int width) const {
    const Message* message = messageFor(index);
    if (!message) return {};
    // The signature has to cover everything measure() reads, or a cached layout
    // from before an edit survives it and the row keeps its old height.
    QString signature = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
                            .arg(message->id)
                            .arg(width)
                            .arg(message->content.size())
                            .arg(message->attachments.size())
                            .arg(message->embeds.size())
                            .arg(message->reactions.size())
                            .arg(message->edited.isValid() ? 1 : 0)
                            .arg(message->pending ? 1 : 0)
                            .arg(message->replyIds.size());
    for (const File& file : message->attachments) {
        signature += QStringLiteral("|a%1x%2").arg(file.width).arg(file.height);
    }
    return signature;
}

void MessageDelegate::invalidateLayout(const QString& messageId) {
    for (auto it = m_layoutCache.begin(); it != m_layoutCache.end();) {
        if (it.key().startsWith(messageId + QLatin1Char('|'))) {
            it = m_layoutCache.erase(it);
        } else {
            ++it;
        }
    }
}

void MessageDelegate::clearLayoutCache() {
    m_layoutCache.clear();
}

// The height of one embed card. Shared by measure and paint so the two cannot
// disagree about where the next element starts -- a mismatch here is a row whose
// painted content overflows into the row below it.
int MessageDelegate::measureEmbed(const Embed& embed, int width) const {
    const Theme& theme = m_theme->theme();
    const QFontMetrics body(theme.font);
    const QFontMetrics bold(theme.fontBold);

    const int textWidth = width - kEmbedBar - kEmbedPad * 2;

    int imageHeight = 0;
    if (!embed.imageUrl.isEmpty() && embed.height > 0) {
        // Scaled to the column, then capped: a tall screenshot should not push the
        // rest of the conversation off the bottom of the window.
        const qreal scaled = qreal(textWidth) * qreal(embed.height) / qreal(qMax(1, embed.width));
        imageHeight = int(qBound(qreal(40), scaled, qreal(kEmbedImageMaxHeight)));
    }

    int height = kEmbedMinHeight + imageHeight;
    height += wrapHeight(bold, embed.title, textWidth);
    if (!embed.description.isEmpty()) height += wrapHeight(body, embed.description, textWidth);
    if (!embed.siteName.isEmpty()) height += body.height() + 2;
    return height + kEmbedPad;
}

RowLayout MessageDelegate::measure(const Message* message, bool grouped, int width) const {
    RowLayout layout;
    layout.grouped = grouped;

    const Theme& theme = m_theme->theme();
    const QFontMetrics body(theme.font);
    const QFontMetrics header(theme.fontBold);

    // No gutter. The transcript draws no avatars, so there is nothing to indent past
    // and every row -- grouped or not -- starts at the left edge.
    const int contentWidth = qMax(120, qMin(width - kGutter, kMaxContentWidth));
    layout.contentWidth = contentWidth;

    int y = kRowPadding;

    if (!grouped) {
        layout.headerHeight = kHeaderLine;
        y += kHeaderLine;
    }

    if (!message->replyIds.isEmpty()) {
        layout.replyHeight = kHeaderLine;
        y += layout.replyHeight;
    }

    if (message->isSystem()) {
        layout.contentHeight = kHeaderLine;
        layout.contentTop = y;
        y += layout.contentHeight;
        layout.height = y + kRowPadding;
        layout.valid = true;
        return layout;
    }

    if (!message->content.isEmpty()) {
        const QRect bounds(0, 0, contentWidth, 10000);
        layout.contentHeight = body.boundingRect(bounds, Qt::TextWordWrap, message->content).height();
        layout.contentTop = y;
        y += layout.contentHeight;
    }

    if (!message->attachments.isEmpty()) {
        layout.attachmentTop = y;
        layout.attachmentHeight = kAttachmentHeight;
        y += layout.attachmentHeight;
    }

    if (!message->embeds.isEmpty()) {
        layout.embedTop = y;
        layout.embedHeights.clear();
        int total = 0;
        for (const Embed& embed : message->embeds) {
            const int height = measureEmbed(embed, contentWidth);
            layout.embedHeights.append(height);
            total += height + kEmbedGap;
        }
        layout.embedHeights.prepend(total - kEmbedGap);
        layout.attachmentTop = y;
        layout.attachmentHeight += total;
        y += total;
    }


    if (!message->reactions.isEmpty()) {
        layout.reactionTop = y + 2;
        layout.reactionHeight = kReactionHeight;
        y += layout.reactionHeight;
    }

    layout.height = y + kRowPadding;
    layout.valid = true;
    return layout;
}

RowLayout MessageDelegate::layoutFor(const QModelIndex& index, int width) const {
    const Message* message = messageFor(index);
    if (!message || width <= 0) return {};

    const QString key = cacheKey(index, width);
    const auto cached = m_layoutCache.constFind(key);
    if (cached != m_layoutCache.constEnd()) return cached.value();

    const RowLayout layout = measure(message, index.data(kGroupedRole).toBool(), width);
    m_layoutCache.insert(key, layout);
    return layout;
}

QSize MessageDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int width = option.rect.width();
    const RowLayout layout = layoutFor(index, width);
    if (!layout.valid) return {};
    return QSize(width, layout.height);
}

void MessageDelegate::paintContent(QPainter* painter, const QRect& rect, const Message* message,
                                   const RowLayout& layout) const {
    const Theme& theme = m_theme->theme();
    const QFontMetrics body(theme.font);
    const QFontMetrics header(theme.fontBold);

    int y = rect.top() + kRowPadding;
    const int left = rect.left();

    if (!layout.grouped) {
        const User* user = m_store ? m_store->user(message->authorId) : nullptr;
        QString name = user ? (user->displayName.isEmpty() ? user->username : user->displayName)
                            : QStringLiteral("unknown");
        if (message->masquerade.active && !message->masquerade.name.isEmpty()) {
            name = message->masquerade.name;
        }

        painter->setFont(theme.fontBold);
        painter->setPen(theme.text);
        painter->drawText(QRect(left, y, layout.contentWidth, kHeaderLine),
                          Qt::AlignLeft | Qt::AlignVCenter, name);
        y += kHeaderLine;

        // The server sends no per-message timestamp field, only an "edited" one,
        // so nothing is drawn here rather than inventing a time that is not there.
        painter->setFont(theme.font);
    }

    if (!message->replyIds.isEmpty()) {
        // An arrow, not the word "reply" and not a left border. The glyph is loaded
        // through the icon seam; without one the text is still drawn, indented, so
        // the row keeps its shape in a test that supplies no icons.
        const int glyph = 13;
        int textLeft = left;
        if (m_icon) {
            const QPixmap arrow = m_icon(QStringLiteral(":/icons/tabler/corner-up-left.svg"),
                                         theme.mutedText, QSize(glyph, glyph));
            if (!arrow.isNull()) {
                painter->drawPixmap(QRect(left, y + (kHeaderLine - glyph) / 2, glyph, glyph), arrow);
                textLeft = left + glyph + 4;
            } else {
                textLeft = left + glyph + 4;
            }
        }

        // The quoted line. History is paged, so the message being replied to is
        // often not held -- which is ordinary rather than exceptional and gets its
        // own words instead of a blank row.
        QString preview;
        const Message* target =
            m_store ? m_store->message(message->channelId, message->replyIds.first()) : nullptr;
        if (target) {
            const User* author = m_store->user(target->authorId);
            const QString who = author
                                    ? (author->displayName.isEmpty() ? author->username
                                                                    : author->displayName)
                                    : QStringLiteral("unknown");
            preview = who + QStringLiteral(": ") +
                      target->content.simplified().replace(QLatin1Char('\n'), QLatin1Char(' '));
        } else {
            preview = QStringLiteral("Original message not loaded");
        }

        painter->setPen(theme.mutedText);
        const int textWidth = qMax(40, left + layout.contentWidth - textLeft);
        painter->drawText(QRect(textLeft, y, textWidth, kHeaderLine),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          header.elidedText(preview, Qt::ElideRight, textWidth));
        y += kHeaderLine;
    }

    if (message->isSystem()) {
        painter->setPen(theme.mutedText);
        painter->setFont(theme.font);
        painter->drawText(QRect(left, y, layout.contentWidth, kHeaderLine),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QString::fromUtf8(QJsonDocument(message->system).toJson(QJsonDocument::Compact)));
        return;
    }

    if (layout.contentHeight > 0) {
        painter->setPen(message->pending ? theme.mutedText : theme.text);
        painter->setFont(theme.font);
        painter->drawText(QRect(left, y, layout.contentWidth, layout.contentHeight),
                          Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, message->content);
        y += layout.contentHeight;
    }

    if (!message->attachments.isEmpty()) {
        // Placeholder cards. Real thumbnails are fetched asynchronously and
        // dropped into the avatar cache; painting a box now and swapping later
        // would change the row height, so the box is the resting state.
        int x = left;
        for (const File& file : message->attachments) {
            const QRect card(x, y, 200, kAttachmentHeight);
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme.raised);
            painter->drawRoundedRect(card, 6, 6);

            painter->setPen(theme.text);
            painter->drawText(card.adjusted(10, 8, -10, -8), Qt::TextWordWrap | Qt::AlignTop,
                              file.displayName());
            x += 210;
        }
    }

    // Embed cards. y is already past the content block, so this lines up with the
    // space measure reserved.
    if (!message->embeds.isEmpty() && layout.embedHeights.size() == message->embeds.size() + 1) {
        int embedY = rect.top() + layout.embedTop;
        for (int i = 0; i < message->embeds.size(); ++i) {
            const Embed& embed = message->embeds.at(i);
            const int cardHeight = layout.embedHeights.at(i + 1);
            paintEmbed(painter, QRect(left, embedY, layout.contentWidth, cardHeight), embed);
            embedY += cardHeight + kEmbedGap;
        }
    }
}

void MessageDelegate::paintEmbed(QPainter* painter, const QRect& card, const Embed& embed) const {
    const Theme& theme = m_theme->theme();
    const QFontMetrics body(theme.font);
    const QFontMetrics bold(theme.fontBold);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Recessed, not outlined: a border here is the one thing the client does not do
    // anywhere else, and a card this large would show the frame more than the fill.
    painter->setPen(Qt::NoPen);
    painter->setBrush(theme.sunken);
    painter->drawRoundedRect(card, 6, 6);

    // The site's accent down the leading edge. Website embeds carry a CSS colour
    // string, so it is parsed rather than assumed to be packed; an unparseable one
    // falls back to the muted fill instead of drawing nothing at all.
    QColor accent = theme.mutedText;
    if (!embed.colour.isEmpty()) {
        const QColor parsed(embed.colour);
        if (parsed.isValid()) accent = parsed;
    }
    painter->setBrush(accent);
    painter->drawRoundedRect(
        QRect(card.left(), card.top(), kEmbedBar, card.height()), 2, 2);

    const int textLeft = card.left() + kEmbedBar + kEmbedPad;
    const int textWidth = card.width() - kEmbedBar - kEmbedPad * 2;
    int y = card.top() + kEmbedPad;

    if (!embed.imageUrl.isEmpty() && embed.height > 0) {
        const qreal scaled = qreal(textWidth) * qreal(embed.height) / qreal(qMax(1, embed.width));
        const int imageHeight =
            int(qBound(qreal(40), scaled, qreal(kEmbedImageMaxHeight)));
        const QRect target(textLeft, y, textWidth, imageHeight);
        // Fetched through the same authenticated CDN cache as the avatars, keyed by
        // url. A null pixmap paints nothing and the space stays reserved, so the row
        // does not change height when the image lands.
        QPixmap image;
        if (m_avatar) image = m_avatar(embed.imageUrl, imageHeight);
        if (!image.isNull()) {
            painter->drawPixmap(target, image.scaled(target.size(), Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
        }
        y += imageHeight;
    }

    if (!embed.siteName.isEmpty()) {
        painter->setFont(theme.font);
        painter->setPen(theme.mutedText);
        painter->drawText(QRect(textLeft, y, textWidth, body.height()), Qt::AlignLeft,
                          embed.siteName);
        y += body.height() + 2;
    }

    if (!embed.title.isEmpty()) {
        painter->setFont(theme.fontBold);
        painter->setPen(theme.brightText);
        const int titleHeight = wrapHeight(bold, embed.title, textWidth);
        painter->drawText(QRect(textLeft, y, textWidth, titleHeight),
                          Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, embed.title);
        y += titleHeight;
    }

    if (!embed.description.isEmpty()) {
        painter->setFont(theme.font);
        painter->setPen(theme.text);
        const int descriptionHeight = wrapHeight(body, embed.description, textWidth);
        painter->drawText(QRect(textLeft, y, textWidth, descriptionHeight),
                          Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, embed.description);
    }

    painter->restore();
}

void MessageDelegate::paintReactions(QPainter* painter, const QRect& rect, const Message* message,
                                     const RowLayout& layout, const QString& selfId) const {
    if (message->reactions.isEmpty()) return;

    const Theme& theme = m_theme->theme();
    const int left = rect.left();

    painter->setFont(theme.font);
    const QFontMetrics body(theme.font);

    int x = left;
    int shown = 0;
    for (auto it = message->reactions.constBegin(); it != message->reactions.constEnd();
         ++it, ++shown) {
        if (shown >= kMaxReactionsPerRow) break;

        const bool mine = it.value().contains(selfId);
        // The emote's own image when the server's set has arrived and the CDN is
        // reachable, and the shortcode when it has not. A row of ":name:" pills is a
        // list of identifiers; the images are the point of a server emote.
        const Emoji* emoji = m_store ? m_store->emoji(it.key()) : nullptr;
        const CdnConfig cdn = m_store ? m_store->cdn() : CdnConfig{};
        const QString emoteUrl = emoji ? emoji->url(cdn) : QString();
        QPixmap art;
        if (emoji && m_avatar && !emoteUrl.isEmpty()) art = m_avatar(emoteUrl, kReactionEmoteSize);

        // Either the emote or a text label, never both.
        QString text;
        if (art.isNull()) {
            text = emoji ? emoji->shortcode() : it.key().left(6);
            // The count is only worth the pixels when there is more than one person.
            if (it.value().size() > 1) text += QStringLiteral(" %1").arg(it.value().size());
        }
        const int textWidth = text.isEmpty() ? 0 : body.horizontalAdvance(text);
        const int width = textWidth + 14;

        const int pillHeight = qMax(layout.reactionHeight - 4, kReactionEmoteSize + 4);
        const QRect pill(x, rect.top() + layout.reactionTop, width, pillHeight);
        painter->setPen(Qt::NoPen);
        painter->setBrush(mine ? theme.sunken : theme.raised);
        painter->drawRoundedRect(pill, 9, 9);
        if (!text.isEmpty()) {
            painter->setPen(mine ? theme.text : theme.mutedText);
            painter->drawText(pill, Qt::AlignCenter, text);
        } else {
            // Scaled inside the pill rather than stretched to it, so a non-square
            // emote keeps its shape instead of filling the rounded rect.
            const QPixmap scaled =
                art.scaled(pill.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter->drawPixmap(
                QRect(pill.center().x() - scaled.width() / 2,
                      pill.center().y() - scaled.height() / 2, scaled.width(),
                      scaled.height()),
                scaled);
        }
        x += width + 4;
    }
}

void MessageDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const {
    const Message* message = messageFor(index);
    if (!message) return;

    const Theme& theme = m_theme->theme();
    const RowLayout layout = layoutFor(index, option.rect.width());
    if (!layout.valid) return;

    painter->save();
    // Deliberately not clipped to option.rect. The view already clips to its
    // viewport, and clipping again to the item rect cut the row's band off at the
    // transcript's gutters -- so the hover highlight and anything else the row draws
    // edge to edge was sliced off, and the gap moved with the pointer.

    // Row hover is a fill, never an outline: the transcript has no borders.
    if (option.state & QStyle::State_MouseOver) {
        // Across the whole viewport, not the item rect. The transcript carries left
        // and right gutters, so the rect the view hands over is that much narrower
        // than the widget -- and a band that stops 12px short of each edge appears to
        // tear apart as the pointer moves across it.
        QRect band = option.rect;
        // option.widget is the viewport, and its parent is the view. The viewport's
        // own rect is in the view's coordinates, so the width is taken from the
        // view instead -- the edges line up either way and this needs no cast.
        if (option.widget && option.widget->parentWidget()) {
            const int full = option.widget->parentWidget()->width();
            band.setLeft(0);
            band.setRight(full - 1);
        }
        painter->fillRect(band, theme.surface);
    }

    paintContent(painter, option.rect, message, layout);

    if (!message->reactions.isEmpty()) {
        paintReactions(painter, option.rect, message, layout, m_store ? m_store->selfId() : QString());
    }

    painter->restore();
}

} // namespace nimbus
