#include "chat_window.h"

#include "core/app.h"
#include "avatar_cache.h"
#include "channel_list.h"
#include "composer.h"
#include "message_delegate.h"
#include "message_list.h"
#include "message_model.h"
#include "server_rail.h"
#include "icons.h"
#include "theme.h"

#include <QtCore/QFileInfo>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QVBoxLayout>

namespace nimbus {

namespace {
constexpr int kRailWidth = 54;
constexpr int kChannelWidth = 208;
}

ChatWindow::ChatWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowFlags(Qt::Window);
    setWindowTitle(QStringLiteral("nimbus"));
    resize(1120, 720);
    setMinimumSize(720, 480);

    m_theme = new ThemeManager(this);
    m_avatars = new AvatarCache(this);
    m_rail = new ServerRail(this);
    m_channels = new ChannelList(this);
    m_transcript = new MessageList(this);
    m_transcriptModel = new MessageModel(this);

    m_rail->setTheme(m_theme);
    m_rail->setAvatarCache(m_avatars);
    m_rail->setFixedWidth(kRailWidth);
    m_rail->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_theme->theme().surface.name()));

    m_channels->setTheme(m_theme);
    m_channels->setFixedWidth(kChannelWidth);
    m_channels->setAvatarCache(m_avatars);
    m_channels->setStyleSheet(
        QStringLiteral("background-color: %1;").arg(m_theme->theme().surface.name()));

    m_transcript->setModel(m_transcriptModel);
    auto* delegate = new MessageDelegate(m_theme, m_transcript);
    // The reply arrow. Loaded through the same IconLoader the rest of the window
    // uses, so the glyph is tinted with the theme rather than baked into an asset.
    m_icons = new IconLoader(this);
    delegate->setIconProvider([this](const QString& path, const QColor& colour, const QSize& size) {
        return m_icons->pixmap(path, colour, size);
    });
    // Emote images for the reaction pills, through the same authenticated cache as
    // the avatars. The provider takes a url, not a user id, so one seam serves both.
    delegate->setAvatarProvider([this](const QString& url, int size) {
        return m_avatars->image(url, size);
    });
    m_transcript->setItemDelegate(delegate);
    m_transcript->setStore(nullptr);
    m_transcript->setStyleSheet(
        QStringLiteral("background-color: %1;").arg(m_theme->theme().base.name()));

    auto* channelPane = new QWidget(this);
    auto* channelLayout = new QVBoxLayout(channelPane);
    channelLayout->setContentsMargins(0, 0, 0, 0);
    channelLayout->setSpacing(0);
    channelLayout->addWidget(m_channels);

    m_composer = new Composer(this);

    auto* composerPane = new QWidget(this);
    composerPane->setLayout(new QVBoxLayout);
    composerPane->layout()->setContentsMargins(8, 4, 8, 8);
    composerPane->layout()->addWidget(m_composer);
    // After the composer exists. Wiring this next to the rail and channel list above
    // is a null pointer, because the composer is built later in this function.
    m_composer->setAvatarCache(m_avatars);
    // Base, not the platform default: an unset background here is a bright band
    // under a dark client, and it is the most visible surface on the window.
    composerPane->setStyleSheet(
        QStringLiteral("background-color: %1;").arg(m_theme->theme().base.name()));

    auto* main = new QWidget(this);
    auto* layout = new QVBoxLayout(main);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_transcript, 1);
    layout->addWidget(composerPane);

    auto* body = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(m_rail);
    bodyLayout->addWidget(channelPane);
    bodyLayout->addWidget(main, 1);

    setCentralWidget(body);

    connect(m_rail, &ServerRail::serverSelected, this, &ChatWindow::openServer);
    connect(m_rail, &ServerRail::homeSelected, this, &ChatWindow::openHome);
    connect(m_channels, &ChannelList::channelSelected, this, &ChatWindow::openChannel);

    // The composer talks to the App, not to the transport: ChatWindow owns the
    // channel currently open, so it is the only thing that knows where a send
    // should go.
    connect(m_composer, &Composer::sendMessage, this, [this](const QString& content) {
        if (m_app && !m_channelId.isEmpty()) m_app->sendMessage(m_channelId, content);
    });
    connect(m_composer, &Composer::editMessage, this,
            [this](const QString& messageId, const QString& content) {
                if (m_app && !m_channelId.isEmpty()) {
                    m_app->editMessage(m_channelId, messageId, content);
                }
            });
    connect(m_composer, &Composer::cancelEdit, this, [this] { m_transcript->viewport()->update(); });
    connect(m_composer, &Composer::attachRequested, this, &ChatWindow::attachFiles);

    connect(m_avatars, &AvatarCache::imageReady, this,
            [this](const QString&) { m_transcript->viewport()->update(); });
}

ChatWindow::~ChatWindow() = default;

void ChatWindow::setApp(App* app) {
    if (m_app == app) return; // connecting twice would duplicate every reaction
    m_app = app;
    if (!app) return;

    setStore(app->store());
    m_avatars->setRestClient(app->rest());

    connect(app, &App::instanceDiscovered, this, [this](const InstanceConfig& config) {
        // Both hosts come from the root document, so a self-hosted instance needs
        // no rebuild and the rail never builds a url against the API host.
        m_avatars->setCdnUrl(config.cdn.url);
        m_rail->setCdnUrl(config.cdn.url);
        m_channels->setCdnUrl(config.cdn.url);
        m_composer->setCdnUrl(config.cdn.url);
    });

    // Applied up front as well as on the signal. With a stored session this window
    // is created in response to the auth result, which is strictly *after* the
    // instance was discovered, so the one-shot signal has already fired and a
    // listener-only window never learns the CDN host. Every avatar url is then
    // built against an empty base and comes out relative, which is why the rail and
    // the DM list showed monograms and "@" for every row on a working session.
    if (app->config().cdn.enabled) {
        m_avatars->setCdnUrl(app->config().cdn.url);
        m_rail->setCdnUrl(app->config().cdn.url);
        m_channels->setCdnUrl(app->config().cdn.url);
        m_composer->setCdnUrl(app->config().cdn.url);
    }

    connect(app, &App::ready, this, [this, app]() {
        setWindowTitle(
            QStringLiteral("nimbus - %1 servers").arg(app->store()->servers().size()));
    });
}

void ChatWindow::setStore(Store* store) {
    m_store = store;
    m_rail->setStore(store);
    m_channels->setStore(store);
    m_transcriptModel->setStore(store);
    // The delegate resolves author names, reply previews, reaction shortcodes and
    // the self-reaction highlight from the store. Without this it prints "unknown"
    // for every author and a truncated id on every reaction.
    m_transcript->setStore(store);
    m_composer->setStore(store);
    m_composer->setServerId(m_serverId);
}

// The + control. The dialog lives here rather than in the composer so the composer
// stays a set of string emitters that a test can drive without a modal.
//
// Each file is uploaded on its own and sent as its own message, in the order the
// dialog returned them. Batching them into one message would be tidier on screen but
// the API's per-message attachment list is a cap rather than a promise, and one
// failed upload would then take the rest of the batch down with it.
void ChatWindow::attachFiles() {
    if (!m_app || m_channelId.isEmpty()) return;

    const QStringList paths =
        QFileDialog::getOpenFileNames(this, QStringLiteral("Attach files"), QString());
    if (paths.isEmpty()) return;

    for (const QString& path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            emit m_app->error(QStringLiteral("cannot read %1").arg(path));
            continue;
        }
        const QByteArray bytes = file.readAll();
        file.close();
        if (bytes.isEmpty()) continue;

        const QFileInfo info(path);
        m_app->uploadAttachment(
            info.fileName(), bytes, QStringLiteral("application/octet-stream"),
            [this](const QString& attachmentId, const QString& error) {
                if (!error.isEmpty() || attachmentId.isEmpty()) {
                    emit m_app->error(error.isEmpty() ? QStringLiteral("upload failed")
                                                      : error);
                    return;
                }
                if (m_app && !m_channelId.isEmpty()) {
                    m_app->sendMessage(m_channelId, QString(), QString(),
                                       QStringList{attachmentId});
                }
            });
    }
}

void ChatWindow::openServer(const QString& serverId) {
    m_serverId = serverId;
    // The emoji button lists this server's emoji, so it has to follow the selection.
    m_composer->setServerId(serverId);
    m_channels->setDirectsOnly(false);
    m_channels->setServerId(serverId);
    m_rail->selectId(serverId);

    const Server* server = m_store ? m_store->server(serverId) : nullptr;
    if (server && !server->name.isEmpty()) setWindowTitle(server->name);

    // Land on the first readable channel rather than an empty transcript.
    const auto channels = m_store ? m_store->channelsForServer(serverId) : QList<Channel*>();
    if (!channels.isEmpty()) {
        const QString first = channels.first()->id;
        m_channels->setChannelId(first);
        openChannel(first);
    }
}

// The account avatar. Lists the direct messages in the channel pane and leaves the
// transcript empty rather than showing whatever channel was open a moment ago --
// there is no single conversation this view is "about", and a stale transcript under
// a list of DMs reads as though the DMs all belong to that conversation.
void ChatWindow::openHome() {
    m_serverId.clear();
    m_composer->setServerId(QString());
    m_channelId.clear();
    m_channels->setServerId({});
    m_channels->setDirectsOnly(true);
    m_channels->setChannelId({});
    m_transcriptModel->setChannel({});
    m_composer->setChannel({});
    m_rail->selectId(m_store ? m_store->selfId() : QString());

    QString who = QStringLiteral("Direct Messages");
    if (m_store) {
        if (const User* self = m_store->user(m_store->selfId())) {
            who = self->displayName.isEmpty() ? self->username : self->displayName;
        }
    }
    setWindowTitle(who);
}

void ChatWindow::openChannel(const QString& channelId) {
    m_channelId = channelId;
    m_channels->setChannelId(channelId);
    m_transcriptModel->setChannel(channelId);
    m_composer->setChannel(channelId);

    const Channel* channel = m_store ? m_store->channel(channelId) : nullptr;
    if (channel && !channel->name.isEmpty()) {
        // The native title bar carries this now, so the marker is spelled out
        // rather than drawn: "#general" is unambiguous where the row's "#" glyph
        // was not, and a taskbar tooltip or window list has no glyph to draw it.
        const QString marker =
            channel->type == ChannelType::TextChannel ? QStringLiteral("#") : QString();
        setWindowTitle(marker + channel->name);
    }

    if (m_app) {
        m_app->fetchMessages(channelId);
        const QString last = channel ? channel->lastMessageId : QString();
        if (!last.isEmpty()) m_app->acknowledge(channelId, last);
    }
}

} // namespace nimbus
