#include "session.h"

#include "json.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcessEnvironment>

namespace nimbus {

namespace {

// 0600 from the moment of creation. Widening it after the fact leaves a window
// in which the file is readable, which is the window that matters.
bool writePrivate(const QString& filePath, const QByteArray& data, QString* error) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("cannot open %1: %2").arg(filePath, file.errorString());
        return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (error) *error = QStringLiteral("cannot restrict permissions on %1").arg(filePath);
        return false;
    }
    if (file.write(data) != data.size()) {
        if (error) *error = QStringLiteral("short write to %1").arg(filePath);
        return false;
    }
    file.close();
    return true;
}

Session parse(const QByteArray& raw) {
    Session s;
    const QJsonObject o = QJsonDocument::fromJson(raw).object();
    s.token = jsonString(o, "token");
    s.userId = jsonString(o, "user_id");
    s.name = jsonString(o, "name");
    const QString lastSeen = jsonString(o, "last_seen");
    if (!lastSeen.isEmpty()) s.lastSeen = QDateTime::fromString(lastSeen, Qt::ISODate);
    return s;
}

// One place that decides where nimbus keeps its state, so the test override moves
// the session and the account hint together instead of stranding one of them.
QString configDir() {
    const QString override =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("NIMBUS_SESSION_FILE"));
    if (!override.isEmpty()) return QFileInfo(override).absolutePath();
    return QDir::homePath() + QStringLiteral("/.config/nimbus");
}

QString sessionPath() {
    return configDir() + QStringLiteral("/session");
}

QString accountPath() {
    return configDir() + QStringLiteral("/account");
}

} // namespace

QString SessionStore::path() {
    // Overridable, and the test suite sets it. The round-trip test used to run
    // against this default path, which meant every `nimbus-exec test` deleted the
    // real credential and left a dummy behind -- so the client appeared to forget
    // its account at random, and the only cure was logging in again. It also gives
    // a second profile a home without touching the primary one.
    return sessionPath();
}

QString SessionStore::lastEmail() {
    QFile file(accountPath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return jsonString(QJsonDocument::fromJson(file.readAll()).object(), "email");
}

void SessionStore::rememberEmail(const QString& email) {
    // Only an address. This file exists to prefill the login form, so nothing here
    // is a credential and nothing here is treated as one.
    if (email.isEmpty()) return;

    const QFileInfo info(accountPath());
    if (!QDir().mkpath(info.absolutePath())) return;

    QJsonObject o;
    o["email"] = email;
    const QByteArray data = QJsonDocument(o).toJson(QJsonDocument::Indented);
    QFile file(accountPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(data);
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

Session SessionStore::load() {
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString fromEnv = env.value(QStringLiteral("NIMBUS_SESSION"));
    if (!fromEnv.isEmpty()) {
        // The env var is a bare token, so the user id has to come from the
        // server on the first request rather than being known up front.
        Session s;
        s.token = fromEnv;
        return s;
    }

    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) return {};
    const Session s = parse(file.readAll());
    return s.isValid() ? s : Session{};
}

QString SessionStore::save(const Session& session) {
    if (session.token.isEmpty()) return QStringLiteral("refusing to save an empty token");

    const QFileInfo info(path());
    if (!QDir().mkpath(info.absolutePath())) {
        return QStringLiteral("cannot create %1").arg(info.absolutePath());
    }

    QJsonObject o;
    o["token"] = session.token;
    o["user_id"] = session.userId;
    o["name"] = session.name;
    o["last_seen"] = session.lastSeen.isValid() ? session.lastSeen.toString(Qt::ISODate)
                                                : QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QString error;
    if (!writePrivate(path(), QJsonDocument(o).toJson(QJsonDocument::Indented), &error)) {
        return error;
    }
    return {};
}

bool SessionStore::remove() {
    QFile file(path());
    if (!file.exists()) return true;
    return file.remove();
}

} // namespace nimbus
