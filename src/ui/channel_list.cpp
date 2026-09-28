#include "channel_list.h"

#include "avatar_cache.h"
#include "theme.h"

#include <QtGui/QPainter>
#include <QtWidgets/QAbstractItemView>

namespace nimbus {

namespace {

// Big enough to recognise, small enough to sit inside the compact row height
// without the row growing to match it.
constexpr int kAvatarSize = 14;

} // namespace

// --- model -------------------------------------------------------------------

ChannelListModel::ChannelListModel(Store* store, QObject* parent)
    : QAbstractListModel(parent), m_store(store) {
    if (!m_store) return;
    connect(m_store, &Store::channelAdded, this, &ChannelListModel::onStoreChanged);
    connect(m_store, &Store::channelUpdated, this, &ChannelListModel::onStoreChanged);
    connect(m_store, &Store::channelRemoved, this, &ChannelListModel::onStoreChanged);
    connect(m_store, &Store::serverUpdated, this, &ChannelListModel::onStoreChanged);
    connect(m_store, &Store::serverRemoved, this, &ChannelListModel::onStoreChanged);
    connect(m_store, &Store::unreadChanged, this, &ChannelListModel::onStoreChanged);
    connect(m_store, &Store::userChanged, this, &ChannelListModel::onStoreChanged);
    rebuild();
}

void ChannelListModel::setServerId(const QString& serverId) {
    if (m_serverId == serverId) return;
    m_serverId = serverId;
    rebuild();
}

void ChannelListModel::setChannelId(const QString& channelId) {
    if (m_channelId == channelId) return;
    m_channelId = channelId;
    rebuild();
}

void ChannelListModel::onStoreChanged() {
    rebuild();
}

void ChannelListModel::setDirectsOnly(bool directsOnly) {
    if (m_directsOnly == directsOnly) return;
    m_directsOnly = directsOnly;
    rebuild();
}

void ChannelListModel::rebuild() {
    QVector<ChannelRow> next;
    const CdnConfig cdn{m_cdnUrl, !m_cdnUrl.isEmpty()};

    if (m_store && m_directsOnly) {
        // A heading, for the same reason the server view has them: it stops the
        // first row sitting flush against the top of the pane with nothing to say
        // what the list is. Direct messages are not categorised, so this is the
        // only one.
        ChannelRow heading;
        heading.kind = ChannelRowKind::Category;
        heading.id = QStringLiteral("directs");
        heading.name = QStringLiteral("Direct Messages");
        next.append(heading);

        // Newest first, which is the order directChannels() already returns, and the
        // order a DM list is read in.
        for (const Channel* channel : m_store->directChannels()) {
            ChannelRow row;
            row.kind = ChannelRowKind::Channel;
            row.direct = true;
            row.id = channel->id;
            row.mentions = m_store->mentionCount(channel->id);
            row.unread = row.mentions > 0;
            row.active = channel->id == m_channelId;

            if (channel->recipients.size() == 1) {
                // The account's own notes channel, which is a one-recipient direct.
                row.name = QStringLiteral("Notes");
            } else if (channel->recipients.size() == 2) {
                const QString self = m_store->selfId();
                const QString other = channel->recipients.value(0) == self ? channel->recipients.value(1)
                                                                          : channel->recipients.value(0);
                if (const User* user = m_store->user(other)) {
                    row.name = user->displayName.isEmpty() ? user->username : user->displayName;
                    row.iconUrl = user->avatar.url(cdn);
                } else {
                    // Unknown counterpart: a truncated id beats an empty row that
                    // looks like a rendering failure.
                    row.name = other.left(8);
                }
            } else {
                row.name = channel->name;
            }
            next.append(row);
        }
    } else if (m_store && !m_serverId.isEmpty()) {
        const Server* server = m_store->server(m_serverId);
        const auto channels = m_store->channelsForServer(m_serverId);

        // Channels are emitted in server order, which is the only ordering the API
        // offers, and categories are interleaved where their first channel falls.
        // That keeps a channel in the same visual place as it is in the data
        // rather than sorting it alphabetically under a heading it may not be in.
        QHash<QString, int> categoryRow;
        for (const Channel* channel : channels) {
            const QString categoryId = server ? server->categoryIdFor(channel->id) : QString();
            if (!categoryId.isEmpty() && !categoryRow.contains(categoryId)) {
                QString title = categoryId;
                if (server) {
                    for (const Category& category : server->categories) {
                        if (category.id == categoryId) {
                            title = category.title;
                            break;
                        }
                    }
                }
                ChannelRow heading;
                heading.kind = ChannelRowKind::Category;
                heading.id = categoryId;
                heading.name = title;
                categoryRow.insert(categoryId, next.size());
                next.append(heading);
            }

            ChannelRow row;
            row.kind = ChannelRowKind::Channel;
            row.id = channel->id;
            row.name = channel->name;
            row.categoryId = categoryId;
            row.voice = channel->voice;
            row.mentions = m_store->mentionCount(channel->id);
            row.unread = row.mentions > 0;
            row.active = channel->id == m_channelId;
            next.append(row);
        }
    }

    if (next == m_rows) return; // nothing moved; skip the reset

    beginResetModel();
    m_rows = next;
    endResetModel();
}

int ChannelListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ChannelListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const ChannelRow& row = m_rows.at(index.row());
    switch (role) {
        case KindRole: return int(row.kind);
        case IdRole: return row.id;
        case NameRole: return row.name;
        case VoiceRole: return row.voice;
        case UnreadRole: return row.unread;
        case MentionsRole: return row.mentions;
        case ActiveRole: return row.active;
        case DirectRole: return row.direct;
        case IconUrlRole: return row.iconUrl;
        default: return {};
    }
}

QHash<int, QByteArray> ChannelListModel::roleNames() const {
    return {
        {KindRole, "kind"},       {IdRole, "id"},
        {NameRole, "name"},       {VoiceRole, "voice"},
        {UnreadRole, "unread"},   {MentionsRole, "mentions"},
        {ActiveRole, "active"},   {DirectRole, "direct"},
        {IconUrlRole, "iconUrl"},
    };
}

// --- delegate ----------------------------------------------------------------

ChannelListDelegate::ChannelListDelegate(ThemeManager* theme, QObject* parent)
    : QStyledItemDelegate(parent), m_theme(theme) {}

void ChannelListDelegate::setAvatarCache(AvatarCache* cache) {
    m_avatar = [cache](const QString& url, int size) {
        return cache ? cache->image(url, size) : QPixmap();
    };
}

void ChannelListDelegate::setAvatarProvider(
    const std::function<QPixmap(const QString& url, int size)>& provider) {
    m_avatar = provider;
}

void ChannelListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    const Theme& theme = m_theme->theme();
    const auto kind = ChannelRowKind(index.data(ChannelListModel::KindRole).toInt());

    painter->save();
    painter->setClipRect(option.rect);

    if (kind == ChannelRowKind::Category) {
        // Headings are small, muted and uppercase; they are labels, not rows.
        QFont font = theme.font;
        font.setPointSizeF(qMax(7.0, font.pointSizeF() - 1.0));
        font.setCapitalization(QFont::AllUppercase);
        font.setWeight(QFont::DemiBold);
        painter->setFont(font);
        painter->setPen(theme.mutedText);
        painter->drawText(option.rect.adjusted(theme.channelIndent + 4, 0, -6, 0),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          index.data(ChannelListModel::NameRole).toString());
        painter->restore();
        return;
    }

    const bool active = index.data(ChannelListModel::ActiveRole).toBool();
    const bool hover = option.state & QStyle::State_MouseOver;
    const int mentions = index.data(ChannelListModel::MentionsRole).toInt();

    if (active) {
        painter->fillRect(option.rect, theme.highlight);
    } else if (hover) {
        painter->fillRect(option.rect, theme.raised);
    }

    const QColor textColour = active ? theme.highlightText : theme.text;
    painter->setPen(textColour);
    painter->setFont(active ? theme.fontBold : theme.font);

    // The glyph is drawn rather than loaded from the icon set: at this size a
    // vector hash would be two antialiased strokes fighting the text baseline. A
    // direct message gets the counterpart's face in the same slot instead, because
    // "@name" is a worse identifier than a face when the list is all people.
    const bool direct = index.data(ChannelListModel::DirectRole).toBool();
    const int glyphWidth = 14;
    const QRect glyph(option.rect.left() + theme.channelIndent, option.rect.top(),
                      glyphWidth, option.rect.height());

    const QString iconUrl = index.data(ChannelListModel::IconUrlRole).toString();
    QPixmap avatar;
    if (direct && m_avatar && !iconUrl.isEmpty()) avatar = m_avatar(iconUrl, kAvatarSize);

    if (!avatar.isNull()) {
        // Clipped to a squircle so a non-square source cannot show corners.
        const QRect target(glyph.left() + (glyphWidth - kAvatarSize) / 2,
                           glyph.top() + (glyph.height() - kAvatarSize) / 2,
                           kAvatarSize, kAvatarSize);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->drawPixmap(target, avatar);
    } else {
        painter->drawText(glyph, Qt::AlignCenter,
                          direct ? QStringLiteral("@")
                                 : (index.data(ChannelListModel::VoiceRole).toBool()
                                        ? QStringLiteral("~")
                                        : QStringLiteral("#")));
    }

    const int badgeWidth = mentions > 0 ? 18 : 0;
    const QRect text(option.rect.left() + theme.channelIndent + glyphWidth,
                     option.rect.top(),
                     option.rect.width() - theme.channelIndent - glyphWidth - badgeWidth - 4,
                     option.rect.height());
    painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(ChannelListModel::NameRole).toString());

    // The mention badge is the only alert this client draws.
    if (mentions > 0) {
        const QRect badge(option.rect.right() - badgeWidth - 4,
                          option.rect.top() + (option.rect.height() - 14) / 2, 16, 14);
        painter->setPen(Qt::NoPen);
        painter->setBrush(active ? theme.highlightText : theme.mention);
        painter->drawRoundedRect(badge, 7, 7);
        painter->setPen(active ? theme.highlight : theme.sunken);
        QFont badgeFont = theme.fontBold;
        badgeFont.setPointSizeF(qMax(6.5, badgeFont.pointSizeF() - 1.5));
        painter->setFont(badgeFont);
        painter->drawText(badge, Qt::AlignCenter,
                          mentions > 9 ? QStringLiteral("9+") : QString::number(mentions));
    }

    painter->restore();
}

QSize ChannelListDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const {
    const auto kind = ChannelRowKind(index.data(ChannelListModel::KindRole).toInt());
    const Theme& theme = m_theme->theme();
    const int height = kind == ChannelRowKind::Category ? qMax(16, theme.rowHeight - 4)
                                                        : theme.rowHeight;
    return QSize(option.rect.width(), height);
}

// --- view --------------------------------------------------------------------

ChannelList::ChannelList(QWidget* parent)
    : QListView(parent) {
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setUniformItemSizes(false);
    setMouseTracking(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    viewport()->setAttribute(Qt::WA_TranslucentBackground);

    connect(this, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        if (index.data(ChannelListModel::KindRole).toInt() != int(ChannelRowKind::Channel)) return;
        emit channelSelected(index.data(ChannelListModel::IdRole).toString());
    });
}

void ChannelList::setStore(Store* store) {
    if (m_model) m_model->deleteLater();
    m_model = new ChannelListModel(store, this);
    setModel(m_model);
}

void ChannelList::setTheme(ThemeManager* theme) {
    if (m_delegate) m_delegate->deleteLater();
    m_delegate = new ChannelListDelegate(theme, this);
    setItemDelegate(m_delegate);
    viewport()->update();
}

void ChannelList::setServerId(const QString& serverId) {
    if (m_model) m_model->setServerId(serverId);
}

void ChannelList::setChannelId(const QString& channelId) {
    if (m_model) m_model->setChannelId(channelId);
}

void ChannelList::setDirectsOnly(bool directsOnly) {
    if (m_model) m_model->setDirectsOnly(directsOnly);
}

void ChannelList::setCdnUrl(const QString& url) {
    if (m_model) m_model->setCdnUrl(url);
}

void ChannelList::setAvatarCache(AvatarCache* cache) {
    if (m_delegate) m_delegate->setAvatarCache(cache);
    if (!cache || m_attachedTo == cache) return;
    m_attachedTo = cache;
    // Without this the row keeps painting its "@" until something else happens to
    // repaint the view: the delegate draws synchronously, so a face that arrives
    // after the first paint is never picked up.
    connect(cache, &AvatarCache::imageReady, this, [this](const QString&) { viewport()->update(); });
}

} // namespace nimbus
