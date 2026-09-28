#pragma once

#include <QtWidgets/QMainWindow>

class QSplitter;

namespace nimbus {

class App;
class AvatarCache;
class ChannelList;
class Composer;
class MessageList;
class IconLoader;
class MessageModel;
class ServerRail;
class Store;
class ThemeManager;

// Rail, channel list, transcript, in the compact three-pane arrangement. The
// window keeps its native decoration: the title bar, its controls, the app menu
// and the desktop's own window management are the desktop's job, and a client
// that redraws them ends up with two title bars and none of the platform's
// behaviour -- snap layouts, keyboard shortcuts, middle-click-to-close.
class ChatWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ChatWindow(QWidget* parent = nullptr);
    ~ChatWindow() override;

    void setApp(App* app);

    // The panes read through a Store, not through App, so a filled-in store is
    // enough to lay the window out. setApp is the transport half of this.
    void setStore(Store* store);

private:
    void openServer(const QString& serverId);
    void openHome();
    void attachFiles();
    void openChannel(const QString& channelId);

    ThemeManager* m_theme = nullptr;
    IconLoader* m_icons = nullptr;
    AvatarCache* m_avatars = nullptr;
    ServerRail* m_rail = nullptr;
    ChannelList* m_channels = nullptr;
    MessageList* m_transcript = nullptr;
    MessageModel* m_transcriptModel = nullptr;
    Composer* m_composer = nullptr;
    App* m_app = nullptr;
    Store* m_store = nullptr;

    QString m_serverId;
    QString m_channelId;
};

} // namespace nimbus
