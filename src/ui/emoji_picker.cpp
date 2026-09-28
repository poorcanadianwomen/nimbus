#include "emoji_picker.h"

#include "avatar_cache.h"
#include "core/store.h"
#include "icons.h"
#include "theme.h"

#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

namespace nimbus {

namespace {

constexpr int kEmoteSize = 36;
constexpr int kColumnWidth = 44;
constexpr int kRowHeight = 40;
constexpr int kGridColumns = 5;
constexpr int kMaxRows = 8;

} // namespace

EmojiPicker::EmojiPicker(QWidget* parent)
    : QWidget(parent) {
    setWindowFlags(Qt::Popup);
    m_theme = new ThemeManager(this);
    m_icons = new IconLoader(this);
    const Theme& theme = m_theme->theme();

    m_filter = new QLineEdit(this);
    m_filter->setObjectName(QStringLiteral("emojiFilter"));
    m_filter->setPlaceholderText(QStringLiteral("Filter emoji"));
    m_filter->setFixedHeight(24);
    m_filter->setStyleSheet(
        QStringLiteral("QLineEdit { background-color: %1; color: %2; border: none;"
                       " border-radius: 4px; padding: 0 8px; }")
            .arg(theme.sunken.name(), theme.text.name()));

    m_grid = new QWidget(this);
    m_grid->setObjectName(QStringLiteral("emojiGrid"));

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setFont(theme.font);
    m_status->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                                .arg(theme.mutedText.name()));
    m_status->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(m_filter);
    layout->addWidget(m_grid);
    layout->addWidget(m_status);

    setStyleSheet(QStringLiteral("QWidget { background-color: %1; }").arg(theme.surface.name()));

    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString&) { rebuild(); });
    rebuild();
}

EmojiPicker::~EmojiPicker() = default;

void EmojiPicker::setAvatarCache(AvatarCache* cache) {
    m_avatars = cache;
    // The delegate in a list is synchronous, so a grid painted before its emotes
    // arrive shows placeholders until something repaints it. Without this the picker
    // opens as a wall of shortcodes and stays that way for the rest of the session.
    if (cache && !m_connectedToCache) {
        m_connectedToCache = true;
        connect(cache, &AvatarCache::imageReady, this, [this](const QString&) { rebuild(); });
    }
}

void EmojiPicker::setServerId(const QString& serverId) {
    m_serverId = serverId;
    rebuild();
}

void EmojiPicker::rebuild() {
    // Rebuilt rather than filtered in place: the grid is a few hundred cells at most
    // and this runs on every keystroke of a two-letter filter, and again every time
    // an emote lands.
    //
    // The old layout is deleted *first* and on its own. Deleting the child widgets
    // leaves its items dangling, and that layout is still parented to the grid, so
    // it is activated or destroyed later against freed memory -- which is a
    // segfault rather than a visible mistake.
    if (QLayout* previous = m_grid->layout()) delete previous;
    for (QWidget* child : m_grid->findChildren<QWidget*>()) delete child;

    if (!m_store) {
        m_status->setText(QStringLiteral("Not connected."));
        m_status->show();
        return;
    }

    const Theme& theme = m_theme->theme();

    const QList<Emoji> all = m_store->emojisForServer(m_serverId);
    if (all.isEmpty()) {
        m_status->setText(m_serverId.isEmpty()
                              ? QStringLiteral("Open a server to browse its emoji.")
                              : QStringLiteral("No emoji fetched for this server yet."));
        m_status->show();
        return;
    }

    const QString needle = m_filter->text().trimmed();
    QList<Emoji> shown;
    for (const Emoji& emoji : all) {
        if (needle.isEmpty() || emoji.name.contains(needle, Qt::CaseInsensitive)) {
            shown.append(emoji);
        }
    }

    if (shown.isEmpty()) {
        m_status->setText(QStringLiteral("Nothing matches \"%1\".").arg(needle));
        m_status->show();
        return;
    }
    m_status->hide();

    const int limit = qMin(shown.size(), kGridColumns * kMaxRows);
    auto* grid = new QVBoxLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);

    for (int start = 0; start < limit; start += kGridColumns) {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(2);
        for (int i = start; i < qMin(start + kGridColumns, limit); ++i) {
            const Emoji emoji = shown.at(i);
            auto* button = new QToolButton(m_grid);
            button->setObjectName(QStringLiteral("emoji_%1").arg(emoji.name));
            button->setFixedSize(kColumnWidth, kRowHeight);
            button->setCursor(Qt::PointingHandCursor);
            button->setToolTip(emoji.shortcode());
            button->setIconSize(QSize(kEmoteSize, kEmoteSize));
            button->setStyleSheet(
                QStringLiteral("QToolButton { background: transparent; border: none;"
                               " border-radius: 4px; }"
                               "QToolButton:hover { background-color: %1; }")
                    .arg(theme.raised.name()));

            // The emote itself, at once if the cache has it and asynchronously if not.
            // The shortcode is the placeholder, so a cell is never blank.
            const CdnConfig cdn{m_cdnUrl, !m_cdnUrl.isEmpty()};
            const QString emoteUrl = emoji.url(cdn);
            QPixmap art;
            if (m_avatars && !emoteUrl.isEmpty()) art = m_avatars->image(emoteUrl, kEmoteSize);
            if (!art.isNull()) {
                button->setIcon(QIcon(art));
            } else {
                button->setText(emoji.name);
                button->setFont(theme.font);
                button->setStyleSheet(
                    QStringLiteral("QToolButton { background: transparent; border: none;"
                                   " border-radius: 4px; color: %1; }"
                                   "QToolButton:hover { background-color: %2; }")
                        .arg(theme.mutedText.name(), theme.raised.name()));
            }
            connect(button, &QToolButton::clicked, this,
                    [this, emoji]() { insertAtCursor(emoji.name); });
            row->addWidget(button);
        }
        // Keeps the last row's buttons left-aligned instead of stretched apart.
        row->addStretch(1);
        grid->addLayout(row);
    }

    m_grid->setLayout(grid);
    resize(kGridColumns * (kColumnWidth + 2) + 16,
           qMin(limit / kGridColumns + 1, kMaxRows) * (kRowHeight + 2) + 66);
}

void EmojiPicker::insertAtCursor(const QString& name) {
    emit emojiChosen(name);
}

} // namespace nimbus
