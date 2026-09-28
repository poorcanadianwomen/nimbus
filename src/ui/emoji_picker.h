#pragma once

#include <QtWidgets/QWidget>

class QLabel;
class QLineEdit;
class QToolButton;
class QWidget;

namespace nimbus {

class AvatarCache;
class IconLoader;
class Store;
class ThemeManager;

// The emoji button's popup: a filter field over the server's emoji, as a grid of the
// emote images themselves. Choosing one emits emojiChosen with the bare shortcode.
//
// The images, not the shortcode text. A server emote is a custom GIF addressed by id
// on the CDN, and a wall of ":neko_sad:" is a list of names rather than a list of
// emotes. The shortcode stays as the tooltip and as what gets inserted.
class EmojiPicker : public QWidget {
    Q_OBJECT
public:
    explicit EmojiPicker(QWidget* parent = nullptr);
    ~EmojiPicker() override;

    void setStore(Store* store) { m_store = store; }
    void setAvatarCache(AvatarCache* cache);
    void setCdnUrl(const QString& url) { m_cdnUrl = url; rebuild(); }

    // The server whose emoji are listed. An empty id, or a server with none fetched,
    // is shown as an explanation rather than an empty grid.
    void setServerId(const QString& serverId);

signals:
    void emojiChosen(const QString& name);

private:
    void rebuild();
    void insertAtCursor(const QString& name);

    Store* m_store = nullptr;
    AvatarCache* m_avatars = nullptr;
    QString m_cdnUrl;
    QString m_serverId;
    bool m_connectedToCache = false;
    ThemeManager* m_theme = nullptr;
    IconLoader* m_icons = nullptr;
    QLineEdit* m_filter = nullptr;
    QWidget* m_grid = nullptr;
    QLabel* m_status = nullptr;
};

} // namespace nimbus
