#pragma once

#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtGui/QColor>
#include <QtGui/QFont>

namespace nimbus {

// Ripcord's Carbon palette, taken from the client's own theme format rather than
// eyeballed. The values are low-saturation warm greys with a near-white
// highlight, which is the whole character of the look: nothing is blue, and the
// only bright thing on screen is the selected row or an unread badge.
struct Theme {
    // Frame behind the panes, and the pane that is not a list.
    QColor window = QColor("#3c3d40");
    // Transcript background.
    QColor base = QColor("#2e2f31");
    // Rail and channel list.
    QColor surface = QColor("#29292b");
    // Raised fills: composer, cards, hover.
    QColor raised = QColor("#4a4b50");
    // Slightly recessed fill for inputs sitting on a raised surface.
    QColor sunken = QColor("#45464b");

    QColor text = QColor("#d0d1d4");
    QColor brightText = QColor("#ffffff");
    QColor mutedText = QColor("#898a8c");
    QColor timestamp = QColor("#494b4e");

    // Selection fill and the text drawn on it. Ripcord uses the same light grey
    // for both, which is why a selected row reads as an inversion rather than a
    // tint of the accent.
    QColor highlight = QColor("#bfc7d5");
    QColor highlightText = QColor("#2d2c27");

    // Mention badge. Ripcord has no separate mention colour, so the unread badge
    // treatment is reused rather than inventing a hue.
    QColor mention = QColor("#d0d1d4");
    QColor mentionBg = QColor("#4a4b50");

    QColor error = QColor("#e06c75");
    QColor success = QColor("#98c379");

    // Proportional sans. Ripcord is not monospaced.
    QFont font;
    QFont fontBold;

    // Density. Ripcord's rows are short; these are shared so the rail, the channel
    // list and the transcript cannot drift apart.
    int rowHeight = 22;
    int railIconSize = 30;
    int channelIndent = 8;

    Theme();

    int scaledRowHeight() const;
};

enum class ThemePreset {
    Carbon,
    Custom,
};

QString themePresetName(ThemePreset preset);
// A config naming a retired preset resolves to Carbon rather than failing: an
// unknown name once meant "the settings dialog showed nothing".
ThemePreset themePresetFromName(const QString& name);

class ThemeManager : public QObject {
    Q_OBJECT
public:
    explicit ThemeManager(QObject* parent = nullptr);

    const Theme& theme() const { return m_theme; }

    // Reports an accent override without creating a second palette.
    ThemePreset preset() const { return m_preset; }
    void setAccentOverride(const QColor& accent);

    QString styleSheet() const;

signals:
    void themeChanged();

private:
    Theme m_theme;
    ThemePreset m_preset = ThemePreset::Carbon;
};

} // namespace nimbus
