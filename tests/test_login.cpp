#include "../src/ui/login_window.h"

#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtTest/QtTest>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

namespace {

// A window that rendered one flat colour proves nothing, and the fields on this
// form are separated from the surface by a fill of only a few RGB steps -- so
// "more than a couple of distinct colours" is the floor, not the bar.
int countDistinct(const QImage& image) {
    QSet<QRgb> seen;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            seen.insert(image.pixel(x, y));
            if (seen.size() > 64) return int(seen.size());
        }
    }
    return int(seen.size());
}

QLineEdit* lineEditAt(nimbus::LoginWindow* window, int index) {
    const auto fields = window->findChildren<QLineEdit*>();
    return index < fields.size() ? fields.at(index) : nullptr;
}

QPushButton* submitButton(nimbus::LoginWindow* window) {
    const auto buttons = window->findChildren<QPushButton*>();
    return buttons.isEmpty() ? nullptr : buttons.first();
}

QLabel* messageLabel(nimbus::LoginWindow* window) {
    for (QLabel* label : window->findChildren<QLabel*>()) {
        if (label->wordWrap()) return label;
    }
    return nullptr;
}

} // namespace

class TestLogin : public QObject {
    Q_OBJECT
private slots:
    void test_form_renders_something();
    void test_submit_emits_the_pair_and_clears_the_password();
    void test_empty_fields_are_refused_before_anything_is_emitted();
    void test_busy_greys_the_form();
    void test_remembered_email_prefills_and_focuses_the_password();
    void test_message_is_shown_and_hidden();
};

void TestLogin::test_form_renders_something() {
    nimbus::LoginWindow window;
    window.resize(360, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window, 2000));

    const QImage shot = window.grab().toImage();
    shot.save(QStringLiteral("/tmp/nimbus-login.png"));

    QVERIFY(countDistinct(shot) > 4);

    // Both fields and the button are on screen. A form that lost a widget in a
    // layout change still "renders", and still passes a pixel count.
    QVERIFY(lineEditAt(&window, 0));
    QVERIFY(lineEditAt(&window, 1));
    QVERIFY(submitButton(&window));

    // The password field must not echo what is typed into it.
    QVERIFY(lineEditAt(&window, 1)->echoMode() == QLineEdit::Password);
}

void TestLogin::test_submit_emits_the_pair_and_clears_the_password() {
    nimbus::LoginWindow window;

    QString gotEmail;
    QString gotPassword;
    connect(&window, &nimbus::LoginWindow::loginRequested, this,
            [&](const QString& email, const QString& password) {
                gotEmail = email;
                gotPassword = password;
            });

    QLineEdit* email = lineEditAt(&window, 0);
    QLineEdit* password = lineEditAt(&window, 1);
    QVERIFY(email && password);

    email->setText(QStringLiteral("  someone@example.com  "));
    password->setText(QStringLiteral("hunter2"));

    submitButton(&window)->click();

    // Trimmed, because a stray space in an address is a confusing rejection.
    QCOMPARE(gotEmail, QStringLiteral("someone@example.com"));
    QCOMPARE(gotPassword, QStringLiteral("hunter2"));

    // The form is not holding a copy of the password once it has been handed over.
    QVERIFY(password->text().isEmpty());
}

void TestLogin::test_empty_fields_are_refused_before_anything_is_emitted() {
    nimbus::LoginWindow window;

    int emissions = 0;
    connect(&window, &nimbus::LoginWindow::loginRequested, this,
            [&](const QString&, const QString&) { ++emissions; });

    submitButton(&window)->click();
    QCOMPARE(emissions, 0);

    QLabel* message = messageLabel(&window);
    QVERIFY(message);
    QVERIFY(!message->text().isEmpty());
    QVERIFY(message->isVisibleTo(&window));

    // An address but no password is a different complaint, and still no request.
    lineEditAt(&window, 0)->setText(QStringLiteral("someone@example.com"));
    const QString first = message->text();
    submitButton(&window)->click();
    QCOMPARE(emissions, 0);
    QVERIFY(message->text() != first);
}

void TestLogin::test_busy_greys_the_form() {
    nimbus::LoginWindow window;
    QLineEdit* email = lineEditAt(&window, 0);
    QPushButton* button = submitButton(&window);

    window.setBusy(true);
    QVERIFY(!email->isEnabled());
    QVERIFY(!button->isEnabled());
    QCOMPARE(button->text(), QStringLiteral("Signing in..."));

    window.setBusy(false);
    QVERIFY(email->isEnabled());
    QVERIFY(button->isEnabled());
    QCOMPARE(button->text(), QStringLiteral("Sign in"));
}

void TestLogin::test_remembered_email_prefills_and_focuses_the_password() {
    nimbus::LoginWindow window;

    // Nothing remembered leaves the field alone rather than clearing it.
    window.setRememberedEmail(QString());
    QVERIFY(lineEditAt(&window, 0)->text().isEmpty());

    window.setRememberedEmail(QStringLiteral("someone@example.com"));
    QCOMPARE(window.email(), QStringLiteral("someone@example.com"));
    QCOMPARE(lineEditAt(&window, 0)->text(), QStringLiteral("someone@example.com"));

    // The window is not on screen yet, which is the whole reason the focus request
    // has to be deferred rather than made at this point.
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window, 2000));
    QCOMPARE(lineEditAt(&window, 1)->hasFocus(), true);
}

void TestLogin::test_message_is_shown_and_hidden() {
    nimbus::LoginWindow window;
    QLabel* message = messageLabel(&window);
    QVERIFY(message);

    window.showMessage(QStringLiteral("session rejected"), false);
    QCOMPARE(message->text(), QStringLiteral("session rejected"));
    QVERIFY(message->isVisibleTo(&window));

    // Clearing is how a fresh attempt drops the previous complaint.
    window.showMessage(QString(), false);
    QVERIFY(message->text().isEmpty());
    QVERIFY(!message->isVisibleTo(&window));
}

QTEST_MAIN(TestLogin)
#include "test_login.moc"
