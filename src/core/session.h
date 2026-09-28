#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QString>

namespace nimbus {

// A Stoat session, as returned by POST /auth/session/login. The token is a
// bearer credential equivalent to a password: anyone holding it acts as the
// account. It is stored 0600 and never logged.
struct Session {
    QString token;
    QString userId;
    QString name;
    QDateTime lastSeen;

    bool isValid() const { return !token.isEmpty() && !userId.isEmpty(); }
};

// A Stoat session token is opaque: there is no format to check, so validity is
// established by asking the server (GET /users/@me) rather than by inspecting
// the string. Inferring shape from a credential is how a client ends up treating
// a revoked session as merely malformed.
class SessionStore {
public:
    // $NIMBUS_SESSION wins over the file, matching how the launcher injects
    // credentials so a throwaway session can be tested without touching disk.
    static Session load();

    // Returns an empty string on success, otherwise the reason it failed.
    static QString save(const Session& session);

    static bool remove();

    // $NIMBUS_SESSION_FILE wins over the default, so a test -- or a second profile
    // -- can point somewhere else without the default path being reachable.
    static QString path();

    // The address last signed in from, so the login form can prefill it. An
    // address is not a credential, so it is kept in its own file next to the
    // session rather than inside it, and is never treated as one.
    static QString lastEmail();
    static void rememberEmail(const QString& email);

private:
    SessionStore() = delete;
};

} // namespace nimbus
