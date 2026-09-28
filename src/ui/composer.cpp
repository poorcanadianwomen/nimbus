#include "composer.h"

#include "avatar_cache.h"
#include "core/store.h"
#include "emoji_picker.h"

#include "icons.h"
#include "theme.h"

#include <QtGui/QIcon>
#include <QtGui/QKeyEvent>
#include <QtGui/QTextCursor>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>

namespace nimbus {

namespace {

// One small bar. The height is the text line plus a few pixels of breathing room
// and nothing else, so the input does not read as a panel bolted to the bottom of
// the window.
constexpr int kBarHeight = 22;
constexpr int kSendSize = 20;
constexpr int kRowGap = 4;
// Big enough to read at the bar's own height without crowding the caret.
constexpr int kIconSize = 14;
// The bar is a row of four bare glyphs, so the ends are inset to keep the arrow and
// the plus off the rounded corners of the field.
constexpr int kEdgeInset = 2;

// The bar stops where the transcript's text does. The delegate caps message content
// at 780, so a bar of the same width lines the two up on one margin instead of
// running the full width of the window with a caret in the far corner.
constexpr int kMaxBarWidth = 780;
constexpr int kMinInputWidth = 80;

// Reports the cap as its *preferred* width. A maximum width alone would leave the
// field at QLineEdit's very small size hint, and setFixedWidth would stop it
// shrinking at all -- it sets the minimum too, so a narrow pane gets clamped back up
// and the bar overflows. A size hint is what a layout is allowed to shrink from.
class BarInput : public QLineEdit {
public:
    using QLineEdit::QLineEdit;

    QSize sizeHint() const override {
        // The field's share of the cap: the cap is for the whole row, and the row is
        // the field plus four controls and the gaps between them.
        return QSize(kMaxBarWidth - 4 * kSendSize - 5 * kRowGap - 2 * kEdgeInset, kBarHeight);
    }
};

} // namespace

Composer::Composer(QWidget* parent)
    : QWidget(parent) {
    m_theme = new ThemeManager(this);
    m_icons = new IconLoader(this);
    const Theme& theme = m_theme->theme();

    // Sunken, borderless, on the transcript surface. The focus state is a lighter
    // fill rather than an outline, which is the only focus treatment anywhere else
    // in this client.
    m_input = new BarInput(this);
    m_input->setObjectName(QStringLiteral("composerInput"));
    // Starts on the no-channel prompt, because that is the state a fresh window is
    // in until something is opened. setChannel switches it.
    m_input->setPlaceholderText(QStringLiteral("Open a channel to send a message"));
    m_input->setMinimumHeight(kBarHeight);
    m_input->setMinimumWidth(kMinInputWidth);
    // border: none is not decoration, it is the standing rule -- selection and state
    // are fills, never an outline. The focus fill is one step up from the resting one
    // rather than a ring around the field.
    m_input->setStyleSheet(
        QStringLiteral("QLineEdit { background-color: %1; color: %2; border: none;"
                       " border-radius: 4px; padding: 0 8px; selection-background-color: %3; }"
                       "QLineEdit:focus { background-color: %4; }"
                       "QLineEdit:disabled { color: %5; }")
            .arg(theme.sunken.name(), theme.text.name(), theme.highlight.name(),
                 theme.raised.name(), theme.mutedText.name()));
    // A placeholder set on the field itself rather than as a separate label: one
    // control, and it disappears the moment there is text.
    m_placeholder = nullptr;

    // The send control is a filled mark on a filled button, per the standing rule
    // that a filled control takes a filled glyph. An outline arrow at 16px on a
    // solid fill loses its stroke to antialiasing.
    // Just the arrow. A filled plate behind it was a second box competing with the
    // field for the same 22 pixels, and it left a light square sitting in the corner
    // of every window. No background, no border, and no hover fill either: the glyph
    // brightening is the whole hover treatment.
    m_send = new QToolButton(this);
    m_send->setObjectName(QStringLiteral("composerSend"));
    m_send->setFixedSize(kSendSize, kSendSize);
    m_send->setCursor(Qt::PointingHandCursor);
    m_send->setToolTip(QStringLiteral("Send"));
    m_send->setStyleSheet(
        QStringLiteral("QToolButton { background: transparent; border: none; }"));
    m_send->setIcon(m_icons->icon(QStringLiteral(":/icons/material/send.svg"), theme.text,
                                  QSize(kIconSize, kIconSize)));

    // The rest of the row is the same control three more times: a bare glyph, no
    // plate. They are built by one factory so they cannot drift apart.
    const auto bareButton = [&](const QString& iconPath, const QString& tip,
                                const QString& name) {
        auto* button = new QToolButton(this);
        // Named, because the row now holds four interchangeable QToolButtons and
        // findChild<QToolButton*>() hands them back in creation order, which is not
        // something a test should have to guess at.
        button->setObjectName(name);
        button->setFixedSize(kSendSize, kSendSize);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tip);
        button->setStyleSheet(
            QStringLiteral("QToolButton { background: transparent; border: none; }"));
        button->setIcon(m_icons->icon(iconPath, theme.mutedText, QSize(kIconSize, kIconSize)));
        return button;
    };

    m_attach = bareButton(QStringLiteral(":/icons/tabler/plus.svg"),
                          QStringLiteral("Attach a file"), QStringLiteral("composerAttach"));
    m_emoji = bareButton(QStringLiteral(":/icons/tabler/mood-smile.svg"),
                         QStringLiteral("Emoji - not available yet"),
                         QStringLiteral("composerEmoji"));
    m_gif = bareButton(QStringLiteral(":/icons/tabler/photo.svg"),
                       QStringLiteral("GIF - not available yet"),
                       QStringLiteral("composerGif"));

    // Only present while editing. A persistent bar would imply editing is always
    // available, which it is not.
    m_editBar = new QWidget(this);
    auto* editLayout = new QVBoxLayout(m_editBar);
    editLayout->setContentsMargins(0, 0, 0, 0);
    editLayout->setSpacing(2);
    auto* editRow = new QHBoxLayout;
    editRow->setContentsMargins(0, 0, 0, 0);
    editRow->setSpacing(6);
    auto* editLabel = new QLabel(QStringLiteral("Editing message"), m_editBar);
    editLabel->setFont(theme.font);
    editLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                 .arg(theme.mutedText.name()));
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), m_editBar);
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setFixedHeight(20);
    cancel->setFont(theme.font);
    cancel->setStyleSheet(QStringLiteral("QPushButton { background: transparent; color: %1;"
                                        " border: none; padding: 0 4px; }"
                                        "QPushButton:hover { color: %2; }")
                              .arg(theme.mutedText.name(), theme.brightText.name()));
    editRow->addWidget(editLabel);
    editRow->addStretch(1);
    editRow->addWidget(cancel);
    editLayout->addLayout(editRow);
    m_editBar->hide();

    connect(cancel, &QPushButton::clicked, this, [this] {
        m_editId.clear();
        m_input->clear();
        setEditing(false);
        emit cancelEdit();
    });

    // +, field, emoji, gif, send. The plus is on the opposite end from the arrow so
    // the two things you press most -- attach and send -- are not adjacent, which is
    // how a mis-click sends the wrong thing.
    auto* row = new QHBoxLayout;
    row->setContentsMargins(kEdgeInset, 0, kEdgeInset, 0);
    row->setSpacing(kRowGap);
    row->addWidget(m_attach, 0, Qt::AlignVCenter);
    row->addWidget(m_input, 1);
    row->addWidget(m_emoji, 0, Qt::AlignVCenter);
    row->addWidget(m_gif, 0, Qt::AlignVCenter);
    row->addWidget(m_send, 0, Qt::AlignVCenter);

    // Centred rather than stretched: the stretches either side of the inner group
    // hold it at its natural width, and resizeEvent is what sets that width.
    auto* inner = new QVBoxLayout;
    inner->setContentsMargins(0, 0, 0, 0);
    inner->setSpacing(4);
    inner->addWidget(m_editBar);
    inner->addLayout(row);

    auto* centred = new QHBoxLayout;
    centred->setContentsMargins(0, 0, 0, 0);
    centred->addStretch(1);
    centred->addLayout(inner);
    centred->addStretch(1);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);
    column->addLayout(centred);
    setLayout(column);
    setStyleSheet(QStringLiteral("QWidget#composer { background-color: %1; }")
                      .arg(theme.raised.name()));
    setObjectName(QStringLiteral("composer"));

    m_input->installEventFilter(this);
    connect(m_send, &QToolButton::clicked, this, &Composer::submit);
    connect(m_attach, &QToolButton::clicked, this, &Composer::attachRequested);
    connect(m_emoji, &QToolButton::clicked, this, &Composer::openEmojiPicker);
    connect(m_gif, &QToolButton::clicked, this, &Composer::gifRequested);
    connect(m_input, &QLineEdit::returnPressed, this, &Composer::submit);
    connect(m_input, &QLineEdit::textChanged, this, &Composer::updateSendState);

    m_input->show();
    m_send->show();
    m_attach->show();
    m_emoji->show();
    m_gif->show();
    m_editBar->hide();
    // A minimum height, because with no channel open every child is hidden and the
    // widget's own size hint collapses to nothing -- which is a zero-height strip
    // across the bottom of the window rather than a usable one.
    setMinimumHeight(kBarHeight + 6);
    setEnabled(false);
    m_input->setPlaceholderText(QStringLiteral("Open a channel to send a message"));
}

Composer::~Composer() = default;

void Composer::setChannel(const QString& channelId) {
    m_channelId = channelId;
    m_editId.clear();
    m_input->clear();
    setEditing(false);

    // Leaving a channel abandons an in-progress edit rather than carrying it into
    // whatever is opened next.
    updateSendState();

    const bool open = !channelId.isEmpty();
    setEnabled(open);
    // The bar is always on screen. Hiding it left a bare strip across the bottom of
    // the window, so the client looked like it had no message input at all.
    m_input->setPlaceholderText(open ? QStringLiteral("Enter text here")
                                     : QStringLiteral("Open a channel to send a message"));
    if (open) m_input->setFocus();
}

void Composer::setStore(Store* store) {
    m_store = store;
    if (m_picker) m_picker->setStore(store);
}

void Composer::setAvatarCache(AvatarCache* cache) {
    m_avatars = cache;
    if (m_picker) m_picker->setAvatarCache(cache);
}

void Composer::setCdnUrl(const QString& url) {
    m_cdnUrl = url;
    if (m_picker) m_picker->setCdnUrl(url);
}

void Composer::setServerId(const QString& serverId) {
    m_serverId = serverId;
    if (m_picker) m_picker->setServerId(serverId);
}

// Opens under the button rather than at the pointer: a popup that appears under the
// cursor is ambiguous about which of several it belongs to.
void Composer::openEmojiPicker() {
    if (!m_emoji->isEnabled()) return;

    if (!m_picker) {
        m_picker = new EmojiPicker(this);
        m_picker->setStore(m_store);
        m_picker->setAvatarCache(m_avatars);
        m_picker->setCdnUrl(m_cdnUrl);
        m_picker->setServerId(m_serverId);
        connect(m_picker, &EmojiPicker::emojiChosen, this, [this](const QString& name) {
            // The shortcode, at the caret, and the field keeps focus so the next one
            // can be typed straight after.
            m_input->setFocus();
            m_input->insert(name);
        });
    } else {
        m_picker->setServerId(m_serverId);
    }

    const QPoint below = mapToGlobal(QPoint(m_emoji->x(), m_emoji->y() + m_emoji->height() + 2));
    m_picker->move(below);
    m_picker->show();
    m_picker->raise();
    m_picker->setFocus();
}

void Composer::clear() {
    m_input->clear();
    m_editId.clear();
    setEditing(false);
    updateSendState();
}

QString Composer::content() const {
    return m_input->text().trimmed();
}

void Composer::beginEdit(const QString& messageId, const QString& content) {
    if (messageId.isEmpty() || m_channelId.isEmpty()) return;
    m_editId = messageId;
    m_input->setText(content);
    setEditing(true);
    m_input->setFocus();
    // The whole message selected, so typing replaces it -- the behaviour every
    // other editor has, and the reason double-click-then-type is not destructive.
    m_input->selectAll();
    updateSendState();
}

void Composer::setEditing(bool editing) {
    m_editBar->setVisible(editing);
    m_send->setToolTip(editing ? QStringLiteral("Save") : QStringLiteral("Send"));
}

void Composer::updateSendState() {
    const bool usable = !m_channelId.isEmpty() && !content().isEmpty();
    m_send->setEnabled(usable);

    // With no plate behind it the glyph is tinted for the background it sits on, so
    // the disabled state has to be recoloured rather than dimmed by a fill.
    const Theme& theme = m_theme->theme();
    m_send->setIcon(m_icons->icon(QStringLiteral(":/icons/material/send.svg"),
                                  usable ? theme.text : theme.timestamp,
                                  QSize(kIconSize, kIconSize)));

}

void Composer::submit() {
    if (!m_send->isEnabled()) return;

    const QString text = content();
    if (text.isEmpty()) return;

    // Emitted first, then cleared: a handler that opens something must not find the
    // text still in the box if the user comes back to it.
    if (!m_editId.isEmpty()) {
        const QString id = m_editId;
        m_editId.clear();
        setEditing(false);
        m_input->clear();
        emit editMessage(id, text);
    } else {
        m_input->clear();
        emit sendMessage(text);
    }
    m_input->setFocus();
}

bool Composer::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_input && event->type() == QEvent::KeyPress && !m_editId.isEmpty()) {
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            m_editId.clear();
            m_input->clear();
            setEditing(false);
            emit cancelEdit();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace nimbus
