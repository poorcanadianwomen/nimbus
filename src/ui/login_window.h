#pragma once

#include <QtWidgets/QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

namespace nimbus {

class ThemeManager;

// The sign-in form. It does no networking: it collects a pair of strings and emits
// loginRequested, and whoever owns the App decides what happens next. That split is
// what lets this be laid out and asserted offscreen, with no server involved.
//
// A QWidget rather than a QDialog because it is the application's first window, not
// a modal interruption of another one.
class LoginWindow : public QWidget {
    Q_OBJECT
public:
    explicit LoginWindow(QWidget* parent = nullptr);
    ~LoginWindow() override;

    // Prefills the address. The password is never remembered anywhere.
    void setRememberedEmail(const QString& email);

    // Greys the form while a request is in flight, so a slow server cannot be
    // mistaken for a client that ignored the click.
    void setBusy(bool busy);

    // Anything the user needs to read: a rejection, a disabled account, or the MFA
    // case this client does not implement. Not an error colour unless it is one.
    void showMessage(const QString& text, bool isError);

    QString email() const;

signals:
    void loginRequested(const QString& email, const QString& password);

protected:
    // Enter signs in from either field, which is what a password field is for.
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void submit();

    ThemeManager* m_theme = nullptr;
    QLineEdit* m_email = nullptr;
    QLineEdit* m_password = nullptr;
    QPushButton* m_submit = nullptr;
    QLabel* m_message = nullptr;
    // Set by setRememberedEmail, honoured on show. Asking a hidden field for focus
    // is a no-op, so the request has to outlive the call that made it.
    bool m_focusPassword = false;
};

} // namespace nimbus
