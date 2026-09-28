#include "login_window.h"

#include "theme.h"

#include <QtGui/QKeyEvent>
#include <QtGui/QShowEvent>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

namespace nimbus {

namespace {

// Ripcord's rhythm: short rows, small type, nothing decorative. The form is one
// narrow column because a login is a single task and a wide one invites filling in
// the wrong box.
constexpr int kFormWidth = 300;
constexpr int kFieldHeight = 26;
constexpr int kLabelHeight = 14;

} // namespace

LoginWindow::LoginWindow(QWidget* parent)
    : QWidget(parent) {
    setWindowFlags(Qt::Window);
    setWindowTitle(QStringLiteral("nimbus - sign in"));
    setMinimumWidth(kFormWidth + 48);

    m_theme = new ThemeManager(this);

    const Theme& theme = m_theme->theme();

    // Without this the window is the platform's default palette: a light background
    // under a near-white heading, which is a form nobody can read. The stylesheet
    // paints the base colour on QWidget, so the transparent labels and fields sit
    // on the client's own surface rather than on the desktop's idea of one.
    setStyleSheet(m_theme->styleSheet());
    setAutoFillBackground(false);

    auto* heading = new QLabel(QStringLiteral("nimbus"), this);
    QFont headingFont = theme.fontBold;
    headingFont.setPointSizeF(headingFont.pointSizeF() * 1.6);
    heading->setFont(headingFont);
    heading->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                               .arg(theme.brightText.name()));

    auto* subheading = new QLabel(QStringLiteral("Sign in to your account"), this);
    subheading->setFont(theme.font);
    subheading->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                  .arg(theme.mutedText.name()));

    // Sunken, not framed: the fill is the only thing separating the field from the
    // surface behind it. A border here would contradict every other input in the
    // client, and this stylesheet is what enforces that.
    const QString fieldStyle = QStringLiteral(
        "QLineEdit { background-color: %1; color: %2; border: none; border-radius: 4px;"
        " padding: 0 8px; selection-background-color: %3; }"
        "QLineEdit:focus { background-color: %4; }"
        "QLineEdit:disabled { color: %5; }")
                                   .arg(theme.sunken.name(), theme.text.name(), theme.highlight.name(),
                                        theme.raised.name(), theme.mutedText.name());

    const auto makeField = [&](const QString& placeholder, bool secret) {
        auto* field = new QLineEdit(this);
        field->setPlaceholderText(placeholder);
        field->setFixedHeight(kFieldHeight);
        field->setStyleSheet(fieldStyle);
        if (secret) field->setEchoMode(QLineEdit::Password);
        return field;
    };

    m_email = makeField(QStringLiteral("you@example.com"), false);
    m_password = makeField(QStringLiteral("password"), true);
    m_email->setTextMargins(0, 0, 0, 0);

    auto* emailLabel = new QLabel(QStringLiteral("Email"), this);
    auto* passwordLabel = new QLabel(QStringLiteral("Password"), this);
    for (QLabel* label : {emailLabel, passwordLabel}) {
        label->setFixedHeight(kLabelHeight);
        label->setFont(theme.font);
        label->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                 .arg(theme.mutedText.name()));
    }

    // A filled control takes a filled mark, and the highlight fill with dark text is
    // the one inverted thing in the palette, so the primary action reads as primary
    // without introducing a second accent.
    m_submit = new QPushButton(QStringLiteral("Sign in"), this);
    m_submit->setFixedHeight(kFieldHeight);
    m_submit->setCursor(Qt::PointingHandCursor);
    m_submit->setFont(theme.fontBold);
    m_submit->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: %2; border: none;"
                       " border-radius: 4px; }"
                       "QPushButton:hover { background-color: %3; }"
                       "QPushButton:pressed { background-color: %4; }"
                       "QPushButton:disabled { background-color: %5; color: %6; }")
            .arg(theme.highlight.name(), theme.highlightText.name(), theme.brightText.name(),
                 theme.raised.name(), theme.sunken.name(), theme.mutedText.name()));

    m_message = new QLabel(this);
    m_message->setWordWrap(true);
    m_message->setFont(theme.font);
    m_message->setStyleSheet(QStringLiteral("background: transparent;"));
    m_message->hide();

    auto* column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);
    column->addWidget(heading);
    column->addWidget(subheading);
    column->addSpacing(18);
    column->addWidget(emailLabel);
    column->addWidget(m_email);
    column->addSpacing(10);
    column->addWidget(passwordLabel);
    column->addWidget(m_password);
    column->addSpacing(18);
    column->addWidget(m_submit);
    column->addSpacing(10);
    column->addWidget(m_message);
    column->addStretch(1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);

    auto* form = new QWidget(this);
    form->setLayout(column);
    form->setFixedWidth(kFormWidth);
    form->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* centred = new QVBoxLayout;
    centred->setContentsMargins(0, 0, 0, 0);
    centred->addStretch(1);
    centred->addWidget(form, 0, Qt::AlignHCenter);
    centred->addStretch(1);
    outer->addLayout(centred);

    connect(m_submit, &QPushButton::clicked, this, &LoginWindow::submit);
    connect(m_email, &QLineEdit::returnPressed, this, &LoginWindow::submit);
    connect(m_password, &QLineEdit::returnPressed, this, &LoginWindow::submit);
}

LoginWindow::~LoginWindow() = default;

void LoginWindow::setRememberedEmail(const QString& email) {
    if (email.isEmpty()) return;
    m_email->setText(email);
    // The next thing anyone does is type, so put the caret there rather than making
    // them click the field they are already looking at. Applied on show, because
    // this is called before the window is visible.
    m_focusPassword = true;
    if (isVisible()) m_password->setFocus();
}

void LoginWindow::setBusy(bool busy) {
    m_email->setEnabled(!busy);
    m_password->setEnabled(!busy);
    m_submit->setEnabled(!busy);
    m_submit->setText(busy ? QStringLiteral("Signing in...") : QStringLiteral("Sign in"));
    if (busy) showMessage(QString(), false);
}

void LoginWindow::showMessage(const QString& text, bool isError) {
    // The text is always assigned, empty included: a hidden label that still holds
    // the last complaint reappears with it the next time the window is shown.
    m_message->setText(text);
    if (text.isEmpty()) {
        m_message->hide();
        return;
    }
    const Theme& theme = m_theme->theme();
    m_message->setStyleSheet(
        QStringLiteral("color: %1; background: transparent;").arg(isError ? theme.error.name()
                                                                         : theme.mutedText.name()));
    m_message->show();
}

QString LoginWindow::email() const {
    return m_email->text().trimmed();
}

void LoginWindow::submit() {
    if (!m_submit->isEnabled()) return;

    const QString address = email();
    if (address.isEmpty()) {
        showMessage(QStringLiteral("Enter your email address."), true);
        m_email->setFocus();
        return;
    }
    if (m_password->text().isEmpty()) {
        showMessage(QStringLiteral("Enter your password."), true);
        m_password->setFocus();
        return;
    }

    // Cleared as it is handed over. The field keeping a copy of a password after the
    // request has been made is a liability with no upside.
    showMessage(QString(), false);
    emit loginRequested(address, m_password->text());
    m_password->clear();
}

void LoginWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        submit();
        return;
    }
    QWidget::keyPressEvent(event);
}

void LoginWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!m_focusPassword) return;
    m_focusPassword = false;
    m_password->setFocus();
}

} // namespace nimbus
