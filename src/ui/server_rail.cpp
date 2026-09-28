#include "server_rail.h"

#include "avatar_cache.h"
#include "theme.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtWidgets/QAbstractItemView>

namespace nimbus {

namespace {

// Ripcord rounds the server icons heavily; a light radius reads as a squircle and
// keeps the rail feeling like icons rather than buttons.
constexpr int kRadiusRatio = 3; // radius = size / 3

} // namespace

// --- model -------------------------------------------------------------------

RailModel::RailModel(Store* store, QObject* parent)
    : QAbstractListModel(parent), m_store(store) {
    if (!m_store) return;

    connect(m_store, &Store::serverAdded, this, &RailModel::onStoreChanged);
    connect(m_store, &Store::serverUpdated, this, &RailModel::onStoreChanged);
    connect(m_store, &Store::serverRemoved, this, &RailModel::onStoreChanged);
    connect(m_store, &Store::channelAdded, this, &RailModel::onStoreChanged);
    connect(m_store, &Store::channelRemoved, this, &RailModel::onStoreChanged);
    connect(m_store, &Store::unreadChanged, this, &RailModel::onStoreChanged);
    connect(m_store, &Store::userChanged, this, &RailModel::onStoreChanged);

    rebuild();
}

void RailModel::setSelectedId(const QString& id) {
    if (m_selectedId == id) return;
    m_selectedId = id;
    // The whole rail changes appearance when the selection moves, not one row.
    if (!m_entries.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_entries.size() - 1, 0));
    }
}

void RailModel::onStoreChanged() {
    rebuild();
}

void RailModel::rebuild() {
    QVector<RailEntry> next;
    if (!m_store) {
        beginResetModel();
        m_entries = next;
        endResetModel();
        return;
    }

    const CdnConfig cdn{m_cdnUrl, !m_cdnUrl.isEmpty()};

    // The account's own avatar heads the rail, and opening it is how you get to the
    // direct messages. A monogram here would read as a server called "N".
    if (m_store) {
        const QString selfId = m_store->selfId();
        const User* self = selfId.isEmpty() ? nullptr : m_store->user(selfId);
        if (self) {
            RailEntry entry;
            entry.kind = RailKind::Home;
            entry.id = self->id;
            entry.name = self->displayName.isEmpty() ? self->username : self->displayName;
            entry.iconUrl = self->avatar.url(cdn);
            next.append(entry);
        }
    }

    for (Server* server : m_store->servers()) {
        RailEntry entry;
        entry.kind = RailKind::Server;
        entry.id = server->id;
        entry.name = server->name;
        entry.iconUrl = server->icon.url(cdn);

        const auto channels = m_store->channelsForServer(server->id);
        for (const Channel* channel : channels) {
            const int mentions = m_store->mentionCount(channel->id);
            if (mentions > 0) {
                entry.mentions += mentions;
                entry.unread = true;
            }
        }
        next.append(entry);
    }

    // Direct channels are deliberately absent. They are listed by the channel pane
    // when the account avatar is opened, and having them here as well meant the
    // same conversation reachable from two places with two different selections.

    beginResetModel();
    m_entries = next;
    endResetModel();
}

int RailModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant RailModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) return {};
    const RailEntry& entry = m_entries.at(index.row());
    switch (role) {
        case KindRole: return int(entry.kind);
        case IdRole: return entry.id;
        case NameRole: return entry.name;
        case IconUrlRole: return entry.iconUrl;
        case UnreadRole: return entry.unread;
        default: return {};
    }
}

QHash<int, QByteArray> RailModel::roleNames() const {
    return {
        {KindRole, "kind"}, {IdRole, "id"},       {NameRole, "name"},
        {IconUrlRole, "iconUrl"}, {UnreadRole, "unread"},
    };
}

int RailModel::rowForId(const QString& id) const {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id) return i;
    }
    return -1;
}

// --- delegate ----------------------------------------------------------------

RailDelegate::RailDelegate(ThemeManager* theme, QObject* parent)
    : QStyledItemDelegate(parent), m_theme(theme) {}

void RailDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                         const QModelIndex& index) const {
    const Theme& theme = m_theme->theme();
    const QString name = index.data(RailModel::NameRole).toString();
    const QString iconUrl = index.data(RailModel::IconUrlRole).toString();
    const RailKind kind = RailKind(index.data(RailModel::KindRole).toInt());
    const bool selected = index.data(RailModel::IdRole).toString() == m_selectedId;

    const int size = theme.railIconSize;
    const QRect box(option.rect.left() + (option.rect.width() - size) / 2,
                    option.rect.top() + (option.rect.height() - size) / 2, size, size);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Everything except the open entry drops back. No pill, no outline.
    if (!m_selectedId.isEmpty() && !selected) {
        painter->setOpacity(kDimOpacity / 255.0);
    }

    QPixmap pixmap;
    if (m_avatars && !iconUrl.isEmpty()) pixmap = m_avatars->image(iconUrl, size);

    if (!pixmap.isNull()) {
        // Clip the fetched bitmap to the squircle rather than scaling a square,
        // so a non-square source cannot show corners.
        QPainterPath path;
        path.addRoundedRect(QRectF(box), size / qreal(kRadiusRatio), size / qreal(kRadiusRatio));
        painter->setClipPath(path);
        painter->drawPixmap(box, pixmap);
        painter->setClipping(false);
    } else {
        // Monogram while the bitmap loads, in the row's real size, so the rail
        // never reflows when an image lands.
        painter->setPen(Qt::NoPen);
        painter->setBrush(kind == RailKind::Home ? theme.raised : theme.sunken);
        painter->drawRoundedRect(box, size / qreal(kRadiusRatio), size / qreal(kRadiusRatio));

        painter->setPen(selected ? theme.highlightText : theme.text);
        QFont font = theme.fontBold;
        font.setPointSizeF(font.pointSizeF() * 1.2);
        painter->setFont(font);
        painter->drawText(box, Qt::AlignCenter, name.left(1).toUpper());
    }

    painter->restore();
}

QSize RailDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(index);
    const int size = m_theme->theme().railIconSize;
    // Ripcord's rhythm is tight: the icon plus a few pixels, nothing more.
    return QSize(qMax(size + 6, option.rect.width()), size + 6);
}

// --- view --------------------------------------------------------------------

ServerRail::ServerRail(QWidget* parent)
    : QListView(parent) {
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setUniformItemSizes(true);
    setMouseTracking(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    viewport()->setAttribute(Qt::WA_TranslucentBackground);

    connect(this, &QListView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid()) return;
        if (index.data(RailModel::KindRole).toInt() == int(RailKind::Server)) {
            emit serverSelected(index.data(RailModel::IdRole).toString());
        } else {
            emit homeSelected();
        }
    });
}

void ServerRail::setStore(Store* store) {
    if (m_model) m_model->deleteLater();
    m_model = new RailModel(store, this);
    setModel(m_model);
}

void ServerRail::setTheme(ThemeManager* theme) {
    if (m_delegate) m_delegate->deleteLater();
    m_delegate = new RailDelegate(theme, this);
    setItemDelegate(m_delegate);
    if (m_model) viewport()->update();
}

void ServerRail::setAvatarCache(AvatarCache* cache) {
    if (m_delegate) m_delegate->setAvatarCache(cache);
    if (!cache || m_attachedTo == cache) return;
    m_attachedTo = cache;
    connect(cache, &AvatarCache::imageReady, this, [this](const QString&) { viewport()->update(); });
}

void ServerRail::setCdnUrl(const QString& url) {
    if (m_model) m_model->setCdnUrl(url);
}

void ServerRail::selectId(const QString& id) {
    if (!m_model) return;
    m_model->setSelectedId(id);
    if (m_delegate) m_delegate->setSelectedId(id);
    const int row = m_model->rowForId(id);
    if (row >= 0) setCurrentIndex(m_model->index(row, 0));
    viewport()->update();
}

void ServerRail::resizeEvent(QResizeEvent* event) {
    QListView::resizeEvent(event);
    // Icons are centred within their row, so a width change is a layout change.
    doItemsLayout();
}

} // namespace nimbus
