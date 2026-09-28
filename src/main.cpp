#include "core/app.h"
#include "core/json.h"
#include "core/log.h"
#include "core/session.h"

#include "ui/chat_window.h"
#include "ui/login_window.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QJsonDocument>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>
#include <QtGui/QIcon>
#include <QtGui/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtCore/QPointer>
#include <QtWidgets/QSystemTrayIcon>

#include <cstdio>
#include <functional>
#include <memory>

#ifdef Q_OS_UNIX
#include <termios.h>
#include <unistd.h>
#endif

namespace {

QTextStream& out() {
    static QTextStream stream(stdout);
    return stream;
}

// A password echoed to a terminal is a password in the scrollback and in every
// screen recording of the session.
QString readSecret(const QString& prompt) {
    out() << prompt << Qt::flush;
#ifdef Q_OS_UNIX
    termios original{};
    const bool haveTty = tcgetattr(STDIN_FILENO, &original) == 0;
    if (haveTty) {
        termios quiet = original;
        quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet);
    }
#endif
    QTextStream in(stdin);
    const QString value = in.readLine();
#ifdef Q_OS_UNIX
    if (haveTty) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
    }
#endif
    out() << Qt::endl;
    return value;
}

QString readLine(const QString& prompt) {
    out() << prompt << Qt::flush;
    QTextStream in(stdin);
    return in.readLine().trimmed();
}

int runLogin(nimbus::App& app) {
    QEventLoop loop;

    QObject::connect(&app, &nimbus::App::authStateChanged, &loop,
                     [&loop](nimbus::AuthState state, const QString& message) {
                         switch (state) {
                             case nimbus::AuthState::Authenticated:
                                 out() << "logged in and saved to " << nimbus::SessionStore::path()
                                       << Qt::endl;
                                 loop.quit();
                                 break;
                             case nimbus::AuthState::MfaRequired:
                                 out() << "account requires MFA, which this client does not implement yet"
                                       << Qt::endl;
                                 loop.quit();
                                 break;
                             case nimbus::AuthState::AccountDisabled:
                                 out() << "account disabled: " << message << Qt::endl;
                                 loop.quit();
                                 break;
                             case nimbus::AuthState::Failed:
                                 out() << "login failed: " << message << Qt::endl;
                                 loop.quit();
                                 break;
                             default:
                                 break;
                         }
                     });

    QObject::connect(&app, &nimbus::App::instanceDiscovered, &loop, [&](const nimbus::InstanceConfig& cfg) {
        out() << "instance: api " << cfg.apiVersion << "  ws " << cfg.wsUrl << Qt::endl;
        const QString email = readLine("email: ");
        if (email.isEmpty()) {
            out() << "no email given" << Qt::endl;
            loop.quit();
            return;
        }
        const QString password = readSecret("password: ");
        if (password.isEmpty()) {
            out() << "no password given" << Qt::endl;
            loop.quit();
            return;
        }
        app.login(email, password);
    });

    app.discoverInstance();
    loop.exec();
    return 0;
}

int runWatch(nimbus::App& app, int seconds, bool dump) {
    QEventLoop loop;
    QTimer::singleShot(seconds * 1000, &loop, &QEventLoop::quit);

    // Ready hydrates servers and DMs immediately, but each server's channels need
    // one request apiece, so a summary taken on the ready signal alone would
    // report every server as empty.
    const auto printSummary = [&app] {
        nimbus::Store* store = app.store();
        const auto servers = store->servers();
        const auto dms = store->directChannels();

        int totalChannels = 0;
        for (nimbus::Server* server : servers) {
            totalChannels += int(store->channelsForServer(server->id).size());
        }

        out() << Qt::endl
              << "self        " << store->selfId() << Qt::endl
              << "servers     " << servers.size() << Qt::endl
              << "channels    " << totalChannels << " across servers" << Qt::endl
              << "direct      " << dms.size() << Qt::endl
              << Qt::endl;

        for (nimbus::Server* server : servers) {
            const auto channels = store->channelsForServer(server->id);
            out() << "  " << server->name << "  [" << server->id << "]  " << channels.size()
                  << " channels, " << server->approximateMemberCount << " members" << Qt::endl;
            for (nimbus::Channel* channel : channels) {
                const QString category = channel->categoryId.isEmpty()
                                              ? QStringLiteral("-")
                                              : channel->categoryId;
                out() << "      " << channel->name << "  [" << channel->id << "]  cat=" << category
                      << "  voice=" << (channel->voice ? "y" : "n") << Qt::endl;
            }
        }
        for (nimbus::Channel* channel : dms) {
            out() << "  dm [" << channel->id << "] recipients=" << channel->recipients.size()
                  << " last=" << channel->lastMessageId << Qt::endl;
        }
    };

    // One request per server for channels and one for emoji, all fired at once,
    // so the store keeps filling after the ready signal. Poll it to quiescence
    // instead of guessing a delay: a fixed wait reports a half-hydrated client as
    // an empty one.
    int settledFor = 0;
    auto settle = std::make_shared<std::function<void()>>();
    *settle = [&loop, &settledFor, &app, printSummary, dump, settle]() {
        const auto servers = app.store()->servers();
        int hydrated = 0;
        for (nimbus::Server* server : servers) {
            if (!app.store()->channelsForServer(server->id).isEmpty()) ++hydrated;
        }
        // Reset only after comparing, otherwise a partially hydrated client can
        // never reach the threshold and the summary is never printed at all.
        if (++settledFor >= 4) {
            settledFor = 0;
            out() << "(" << hydrated << "/" << servers.size() << " servers hydrated)" << Qt::endl;
            printSummary();
            if (!dump) loop.quit();
            return;
        }
        QTimer::singleShot(400, &loop, *settle);
    };

    QObject::connect(&app, &nimbus::App::ready, &loop, [&]() {
        out() << "store hydrated, waiting for per-server channel fetches..." << Qt::endl;
        QTimer::singleShot(400, &loop, *settle);
    });

    QObject::connect(&app, &nimbus::App::error, &loop,
                     [](const QString& msg) { out() << "error: " << msg << Qt::endl; });
    QObject::connect(&app, &nimbus::App::disconnected, &loop,
                     [](const QString& reason) { out() << "disconnected: " << reason << Qt::endl; });

    // The stream cannot open until the stored session has been confirmed with the
    // server, because the websocket URL has to carry a token that is known good.
    QObject::connect(&app, &nimbus::App::instanceDiscovered, &loop,
                     [&app]() { app.restoreSession(); });

    QObject::connect(&app, &nimbus::App::authenticated, &loop,
                     [&app](const QString& userId) {
                         out() << "session accepted for user " << userId << Qt::endl;
                         app.openStream();
                     });

    QObject::connect(&app, &nimbus::App::authStateChanged, &loop,
                     [&loop](nimbus::AuthState state, const QString& message) {
                         // Idle here means no stored session, which is a usage
                         // error rather than a failure worth a stack trace.
                         if (state == nimbus::AuthState::Idle || state == nimbus::AuthState::Failed) {
                             out() << (message.isEmpty() ? QStringLiteral("no stored session; run --login")
                                                         : QStringLiteral("auth: ") + message)
                                   << Qt::endl;
                             loop.quit();
                         }
                     });

    app.discoverInstance();
    loop.exec();
    app.closeStream();
    return 0;
}

// The client proper: pick a window based on whether a session exists, and move
// between them as the auth state resolves. One function rather than logic inlined
// in main() because the transitions are the interesting part and they read badly
// scattered across a flag check.
int runClient(nimbus::App& client, const QIcon& appIcon) {
    auto* login = new nimbus::LoginWindow;
    login->setRememberedEmail(nimbus::SessionStore::lastEmail());

    nimbus::ChatWindow* chat = nullptr;
    // The window the tray raises. Starts as the login form and becomes the chat
    // window once the client is up; see openChat for the hand-off.
    QPointer<QWidget> trayWindow;
    trayWindow = login;

    // Brings the client up on the main window, tearing the login form down. Done
    // with a queued call because it runs from inside an authStateChanged emission,
    // and closing a window underneath its own signal handler is how you get a
    // double-free.
    const auto openChat = [&] {
        QMetaObject::invokeMethod(
            login,
            [login, &chat, &client, &trayWindow] {
                chat = new nimbus::ChatWindow;
                chat->setApp(&client);
                chat->show();
                // The tray has to follow, or a click on it raises a login form that
                // was closed the moment the client came up.
                trayWindow = chat;
                login->close();
            },
            Qt::QueuedConnection);
    };

    QObject::connect(login, &nimbus::LoginWindow::loginRequested, &client,
                     [&client, login](const QString& email, const QString& password) {
                         login->setBusy(true);
                         client.login(email, password);
                     });

    QObject::connect(&client, &nimbus::App::authStateChanged, &client,
                     [&](nimbus::AuthState state, const QString& message) {
                         switch (state) {
                             case nimbus::AuthState::Authenticated:
                                 nimbus::SessionStore::rememberEmail(login->email());
                                 openChat();
                                 break;

                             case nimbus::AuthState::MfaRequired:
                                 login->setBusy(false);
                                 login->showMessage(
                                     QStringLiteral(
                                         "This account requires a code we do not ask for yet. "
                                         "Run 'nimbus --login' to see whether the server will "
                                         "issue a session without one."),
                                     false);
                                 break;

                             case nimbus::AuthState::AccountDisabled:
                                 login->setBusy(false);
                                 login->showMessage(
                                     message.isEmpty() ? QStringLiteral("That account is disabled.")
                                                       : message,
                                     true);
                                 break;

                             case nimbus::AuthState::Failed:
                                 login->setBusy(false);
                                 login->showMessage(
                                     message.isEmpty() ? QStringLiteral("Sign in failed.") : message,
                                     true);
                                 break;

                             case nimbus::AuthState::Idle:
                                 // Only meaningful if the login form is what is on screen:
                                 // a rejected stored session with the client already open
                                 // belongs to ChatWindow, not here.
                                 if (!chat) {
                                     login->setBusy(false);
                                     login->showMessage(message, false);
                                 }
                                 break;

                             default:
                                 break;
                         }
                     });

    QObject::connect(&client, &nimbus::App::instanceDiscovered, &client,
                     [&client, &chat]() { client.restoreSession(); });

    QObject::connect(&client, &nimbus::App::authenticated, &client,
                     [&client](const QString&) { client.openStream(); });

    // A stored session that the server rejects must land the user back on the form
    // with the reason, not on a blank client that silently has nothing to show.
    QObject::connect(&client, &nimbus::App::authStateChanged, &client,
                     [&](nimbus::AuthState state, const QString& message) {
                         // Only a chat window that is actually open can be torn down.
                         if (state != nimbus::AuthState::Idle || !chat || message.isEmpty()) return;
                         chat->close();
                         delete chat;
                         chat = nullptr;
                         login->showMessage(message, false);
                         login->show();
                         login->raise();
                         login->activateWindow();
                     });

    // The tray. Parented to the application so it outlives every window: with no
    // window of its own, closing the client with the X used to be the only way out,
    // and on a tray setup that leaves a process nothing can reach.
    auto* tray = new QSystemTrayIcon(appIcon, qApp);
    tray->setToolTip(QStringLiteral("nimbus"));

    auto* menu = new QMenu;
    QAction* show = menu->addAction(QStringLiteral("Show nimbus"));
    menu->addSeparator();
    QAction* quit = menu->addAction(QStringLiteral("Quit"));
    tray->setContextMenu(menu);

    QObject::connect(show, &QAction::triggered, qApp, [&trayWindow] {
        if (!trayWindow) return;
        trayWindow->show();
        trayWindow->raise();
        trayWindow->activateWindow();
    });
    QObject::connect(quit, &QAction::triggered, qApp, [] { qApp->quit(); });
    QObject::connect(tray, &QSystemTrayIcon::activated, qApp,
                     [&trayWindow](QSystemTrayIcon::ActivationReason reason) {
                         // A left click is a request to see the client; a context
                         // click is a request for the menu, which the platform has
                         // already shown.
                         if (reason != QSystemTrayIcon::Trigger) return;
                         if (!trayWindow) return;
                         trayWindow->show();
                         trayWindow->raise();
                         trayWindow->activateWindow();
                     });


    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray->show();
    } else {
        // No tray on this desktop. The client still works; it just has no icon for it.
        qInfo() << "no system tray available; running without one";
    }

    login->show();
    client.discoverInstance();
    const int code = QCoreApplication::exec();
    // Shut the tray down before the app goes: leaving it up outlives the process on
    // some desktops and leaves an icon that opens nothing.
    tray->hide();
    return code;
}

} // namespace

// End-to-end write check. The message deliberately goes to the account's own
// SavedMessages channel rather than a real server: it exercises the full send
// path -- optimistic row, upload, nonce correlation, websocket echo -- without
// posting anything where other people can see it.
int runSelfTest(nimbus::App& app) {
    QEventLoop loop;
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);

    const auto fail = [&loop](const QString& why) {
        out() << "FAIL: " << why << Qt::endl;
        loop.quit();
    };

    QObject::connect(&app, &nimbus::App::instanceDiscovered, &loop, [&app]() {
        out() << "instance: api " << app.config().apiVersion << "  ws " << app.config().wsUrl
              << "  cdn " << app.config().cdn.url << Qt::endl;
        app.restoreSession();
    });

    QObject::connect(&app, &nimbus::App::authenticated, &loop,
                     [&app](const QString& userId) {
                         out() << "session accepted for " << userId << Qt::endl;
                         app.openStream();
                     });

    QObject::connect(&app, &nimbus::App::authStateChanged, &loop,
                     [&loop](nimbus::AuthState state, const QString& message) {
                         if (state != nimbus::AuthState::Failed && state != nimbus::AuthState::Idle) {
                             return;
                         }
                         // Idle with no reason means nothing is stored, which is a
                         // usage error rather than a failure and needs its own words.
                         if (state == nimbus::AuthState::Idle && message.isEmpty()) {
                             out() << "no stored session; run ./build/bin/nimbus --login first"
                                   << Qt::endl;
                         } else {
                             out() << "FAIL: auth: " << message << Qt::endl;
                         }
                         loop.quit();
                     });

    QObject::connect(&app, &nimbus::App::sendFailed, &loop,
                     [&fail](const QString&, const QString&, const QString& reason) {
                         fail("send rejected: " + reason);
                     });

    QObject::connect(&app, &nimbus::App::ready, &loop, [&app, &loop, &fail]() {
        nimbus::Store* store = app.store();

        // A real 1x1 PNG, so the upload exercises the image path rather than an
        // opaque blob. Built from hex rather than written as \x escapes: C++
        // consumes every following hex digit into one escape, so "\x9cc" is
        // 0x9cc and not 0x9c followed by 'c'. A PNG written that way is corrupt,
        // and the server answers a corrupt image with InternalError rather than
        // anything that points at the upload.
        static const QByteArray png = QByteArray::fromHex(
            "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
            "0000000d49444154789c63f8cfc0c00000040101004706cade0000000049454e44"
            "ae426082");

out() << "store hydrated: " << store->servers().size() << " servers, "
              << store->directChannels().size() << " direct channels" << Qt::endl;

        app.uploadAttachment(QStringLiteral("nimbus-selftest.png"), png,
                             QStringLiteral("image/png"),
                             [&app, &loop, &fail, store](const QString& id, const QString& error) {
                                 if (id.isEmpty()) {
                                     fail("upload: " + error);
                                     return;
                                 }
                                 out() << "OK  upload returned id " << id << Qt::endl;

                                 const auto notes =
                                     store->channelsOfType(nimbus::ChannelType::SavedMessages);
                                 if (notes.isEmpty()) {
                                     out() << "SKIP send: no SavedMessages channel in this account"
                                           << Qt::endl;
                                     loop.quit();
                                     return;
                                 }
                                 nimbus::Channel* notesChannel = notes.first();
                                 out() << "sending to SavedMessages [" << notesChannel->id << "]"
                                       << Qt::endl;
                                 // Polled rather than signal-driven: the pending
                                 // row is re-keyed when the echo arrives, so that
                                 // path emits messageUpdated and not messageAdded.
                                 // A listener bound only to additions would never
                                 // see an optimistic send resolve.
                                 const QString targetId = notesChannel->id;
                                 auto poll = std::make_shared<std::function<void(int)>>();
                                 *poll = [&loop, store, targetId, poll](int attempt) {
                                     for (const nimbus::Message* m : store->messages(targetId)) {
                                         if (m->pending) continue;
                                         if (m->content != QLatin1String("nimbus self-test")) continue;
                                         out() << "OK  send confirmed by event echo, id " << m->id
                                               << " attachments=" << m->attachments.size() << Qt::endl;
                                         loop.quit();
                                         return;
                                     }
                                     if (attempt > 40) {
                                         out() << "FAIL: no echo for the sent message" << Qt::endl;
                                         loop.quit();
                                         return;
                                     }
                                     QTimer::singleShot(250, &loop, [poll, attempt] { (*poll)(attempt + 1); });
                                 };
                                 QTimer::singleShot(250, &loop, [poll] { (*poll)(1); });
                                 app.sendMessage(notesChannel->id,
                                                 QStringLiteral("nimbus self-test"),
                                                 QString(), QStringList{id});
                             });

        // Confirmed by the websocket echo rather than the HTTP response, so this
        // also proves the event stream still delivers after an upload.
        QObject::connect(&app, &nimbus::App::error, &loop, [](const QString& msg) {
            qWarning() << "stream error:" << msg;
        });
    });

    app.discoverInstance();
    loop.exec();
    app.closeStream();
    return 0;
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("nimbus"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("nimbus"));
    // The window and the tray share one icon. Set on the application as well as the
    // windows, or the taskbar falls back to a generic placeholder.
    const QIcon appIcon(QStringLiteral(":/icons/app/stoat.svg"));
    app.setWindowIcon(appIcon);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("nimbus - a Stoat client\n\n"
                       "With no flags, launches the client and signs in if it has to."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption loginOpt(QStringLiteral("login"),
                                      QStringLiteral("Sign in and store the session"));
    const QCommandLineOption logoutOpt(QStringLiteral("logout"),
                                       QStringLiteral("Delete the stored session"));
    const QCommandLineOption watchOpt(QStringLiteral("watch"),
                                      QStringLiteral("Connect, report state, then exit"));
    const QCommandLineOption dumpOpt(QStringLiteral("dump"),
                                     QStringLiteral("With --watch, keep running and print every event"));
    const QCommandLineOption apiOpt(QStringLiteral("api"),
                                    QStringLiteral("API base URL (default https://api.stoat.chat)"),
                                    QStringLiteral("url"));
    const QCommandLineOption secondsOpt(QStringLiteral("seconds"),
                                        QStringLiteral("How long --watch should run"),
                                        QStringLiteral("n"), QStringLiteral("20"));
    const QCommandLineOption selfTestOpt(QStringLiteral("selftest"),
                                         QStringLiteral("Verify upload and send against SavedMessages"));
    const QCommandLineOption verboseOpt(QStringLiteral("verbose"),
                                        QStringLiteral("Log debug messages"));
    const QCommandLineOption traceOpt(QStringLiteral("trace"),
                                      QStringLiteral("Log frame-level protocol detail"));

    parser.addOption(loginOpt);
    parser.addOption(logoutOpt);
    parser.addOption(watchOpt);
    parser.addOption(selfTestOpt);
    parser.addOption(dumpOpt);
    parser.addOption(apiOpt);
    parser.addOption(secondsOpt);
    parser.addOption(verboseOpt);
    parser.addOption(traceOpt);
    parser.process(app);

    nimbus::installLogHandler(parser.isSet(traceOpt)    ? nimbus::LogLevel::Trace
                                : parser.isSet(verboseOpt) ? nimbus::LogLevel::Debug
                                                           : nimbus::LogLevel::Info);

    if (parser.isSet(logoutOpt)) {
        const bool removed = nimbus::SessionStore::remove();
        out() << (removed ? "session removed" : "nothing to remove") << " ("
              << nimbus::SessionStore::path() << ")" << Qt::endl;
        return 0;
    }

    nimbus::App client;

    if (parser.isSet(loginOpt)) {
        return runLogin(client);
    }

    if (parser.isSet(selfTestOpt)) {
        return runSelfTest(client);
    }

    if (parser.isSet(watchOpt)) {
        return runWatch(client, parser.value(secondsOpt).toInt(), parser.isSet(dumpOpt));
    }

    // No flag: launch the client.
    return runClient(client, appIcon);
}
