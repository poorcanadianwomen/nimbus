#pragma once

#include "core/store.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QString>
#include <functional>
#include <QtWidgets/QListView>
#include <QtWidgets/QStyledItemDelegate>

namespace nimbus {

class ThemeManager;
class AvatarCache;

// One row is either a category heading or a channel. A flat list rather than a
// tree: categories here are headings, not collapsible containers, and a tree
// invites an expand state this client does not have.
enum class ChannelRowKind {
    Category,
    Channel,
};

struct ChannelRow {
    ChannelRowKind kind = ChannelRowKind::Channel;
    QString id;       // channel id; the category id for a heading
    QString name;
    QString categoryId;
    bool voice = false;
    bool unread = false;
    int mentions = 0;
    bool active = false;
    // A direct message or group rather than a server channel: the row is marked
    // with an @ and, when the counterpart is known, their avatar.
    bool direct = false;
    QString iconUrl;

    // Needed so a rebuild that produced identical rows can skip the reset. Without
    // it every unread badge change resets the view and loses the scroll position.
    bool operator==(const ChannelRow& other) const {
        return kind == other.kind && id == other.id && name == other.name &&
               categoryId == other.categoryId && voice == other.voice && unread == other.unread &&
               mentions == other.mentions && active == other.active && direct == other.direct &&
               iconUrl == other.iconUrl;
    }
};

class ChannelListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        KindRole = Qt::UserRole + 1,
        IdRole,
        NameRole,
        VoiceRole,
        UnreadRole,
        MentionsRole,
        ActiveRole,
        DirectRole,
        IconUrlRole,
    };

    explicit ChannelListModel(Store* store, QObject* parent = nullptr);

    void setServerId(const QString& serverId);
    void setChannelId(const QString& channelId);

    // Lists the direct messages instead of a server's channels, with no category
    // headings: a DM has no server to categorise it under.
    void setDirectsOnly(bool directsOnly);
    void setCdnUrl(const QString& url) { m_cdnUrl = url; rebuild(); }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private slots:
    void onStoreChanged();

private:
    void rebuild();

    Store* m_store = nullptr;
    QString m_serverId;
    QString m_channelId;
    QString m_cdnUrl;
    bool m_directsOnly = false;
    QVector<ChannelRow> m_rows;
};

class ChannelListDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ChannelListDelegate(ThemeManager* theme, QObject* parent = nullptr);

    // The same seam MessageDelegate has: the cache is adapted to a callable so a
    // test can paint a row with images it controls. A delegate that can only reach
    // a live AvatarCache cannot be asked what it draws when an image is present.
    void setAvatarCache(AvatarCache* cache);
    void setAvatarProvider(const std::function<QPixmap(const QString& url, int size)>& provider);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    ThemeManager* m_theme = nullptr;
    std::function<QPixmap(const QString& url, int size)> m_avatar;
};

class ChannelList : public QListView {
    Q_OBJECT
public:
    explicit ChannelList(QWidget* parent = nullptr);

    void setStore(Store* store);
    void setTheme(ThemeManager* theme);
    void setServerId(const QString& serverId);
    void setChannelId(const QString& channelId);
    void setDirectsOnly(bool directsOnly);
    void setCdnUrl(const QString& url);
    void setAvatarCache(AvatarCache* cache);

signals:
    void channelSelected(const QString& channelId);

private:
    ChannelListModel* m_model = nullptr;
    ChannelListDelegate* m_delegate = nullptr;
    // Guards the imageReady connection: setStore and setTheme both run from the
    // window constructor, and a second connect would repaint the list twice per
    // image for no benefit.
    class AvatarCache* m_attachedTo = nullptr;
};

} // namespace nimbus
