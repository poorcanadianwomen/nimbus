#pragma once

#include "core/models.h"
#include "core/store.h"

#include <QtCore/QAbstractListModel>
#include <QtWidgets/QListView>
#include <QtWidgets/QStyledItemDelegate>

namespace nimbus {

class AvatarCache;
class ThemeManager;

// The rail heads with the account's own avatar, then every server. Opening the
// avatar lists the direct messages; the servers below it are the rest.
enum class RailKind {
    Home,
    Server,
};

struct RailEntry {
    RailKind kind = RailKind::Home;
    QString id;
    QString name;
    QString iconUrl;
    bool unread = false;
    int mentions = 0;
};

class RailModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        KindRole = Qt::UserRole + 1,
        IdRole,
        NameRole,
        IconUrlRole,
        UnreadRole,
    };

    explicit RailModel(Store* store, QObject* parent = nullptr);

    void setCdnUrl(const QString& url) { m_cdnUrl = url; rebuild(); }
    void setSelectedId(const QString& id);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int rowForId(const QString& id) const;

private slots:
    void onStoreChanged();

private:
    void rebuild();

    Store* m_store = nullptr;
    QString m_cdnUrl;
    QString m_selectedId;
    QVector<RailEntry> m_entries;
};

// Paints one rounded square per row. There is no selected-state fill: the open
// server is marked by dimming every other icon, because the icons are already
// full-bleed rounded squares and a pill behind one fights it for the same pixels.
class RailDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit RailDelegate(ThemeManager* theme, QObject* parent = nullptr);

    void setAvatarCache(AvatarCache* cache) { m_avatars = cache; }
    void setSelectedId(const QString& id) { m_selectedId = id; }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    ThemeManager* m_theme = nullptr;
    AvatarCache* m_avatars = nullptr;
    QString m_selectedId;

    static constexpr int kDimOpacity = 140; // 55% of 255, as a standing preference
};

class ServerRail : public QListView {
    Q_OBJECT
public:
    explicit ServerRail(QWidget* parent = nullptr);

    void setStore(Store* store);
    void setTheme(ThemeManager* theme);
    void setAvatarCache(AvatarCache* cache);
    void setCdnUrl(const QString& url);
    void selectId(const QString& id);

signals:
    void serverSelected(const QString& serverId);
    // The account avatar was opened: show the direct messages rather than a server.
    void homeSelected();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    RailModel* m_model = nullptr;
    RailDelegate* m_delegate = nullptr;
    AvatarCache* m_attachedTo = nullptr;
};

} // namespace nimbus
