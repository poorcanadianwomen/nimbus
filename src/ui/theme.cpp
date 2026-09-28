#include "theme.h"

#include <QtGui/QFontDatabase>
#include <QtGui/QGuiApplication>
#include <QtWidgets/QScrollBar>

namespace nimbus {

namespace {

// Ordered by preference. Ripcord ships a proportional sans and the row rhythm
// assumes one, so the first family that exists wins and the rest are only a
// fallback for a machine missing all of them.
QStringList sansFamilies() {
    return {
        QStringLiteral("Noto Sans"),
        QStringLiteral("Inter"),
        QStringLiteral("DejaVu Sans"),
        QStringLiteral("Liberation Sans"),
        QStringLiteral("Ubuntu"),
        QStringLiteral("Cantarell"),
    };
}

QFont buildFont(int pointSize, QFont::Weight weight) {
    QFont font;
    // setFamilies rather than setFamilyName: a single name that is not installed
    // silently falls back to whatever the platform prefers.
    font.setFamilies(sansFamilies());
    font.setPointSize(pointSize);
    font.setWeight(weight);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

} // namespace

Theme::Theme()
    : font(buildFont(9, QFont::Normal)),
      fontBold(buildFont(9, QFont::DemiBold)) {}

int Theme::scaledRowHeight() const {
    return rowHeight;
}

QString themePresetName(ThemePreset preset) {
    switch (preset) {
        case ThemePreset::Carbon: return QStringLiteral("carbon");
        case ThemePreset::Custom: return QStringLiteral("custom");
    }
    return QStringLiteral("carbon");
}

ThemePreset themePresetFromName(const QString& name) {
    const QString lowered = name.trimmed().toLower();
    if (lowered == QLatin1String("custom")) return ThemePreset::Custom;
    // Everything else, including the retired "ash", resolves to the one palette.
    return ThemePreset::Carbon;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent) {}

void ThemeManager::setAccentOverride(const QColor& accent) {
    if (!accent.isValid() || m_theme.highlight == accent) return;
    m_theme.highlight = accent;
    m_theme.mention = accent;
    // An override is reported as Custom, which is not a second palette -- it is
    // how the picker says the highlight no longer matches the shipped one.
    m_preset = ThemePreset::Custom;
    emit themeChanged();
}

QString ThemeManager::styleSheet() const {
    const Theme& t = m_theme;

    // No borders anywhere. Every selection is a fill or an opacity, and every
    // rounded control gets its radius here rather than from a frame.
    return QStringLiteral(
               "* { border: none; outline: none; }"
               "QWidget { background-color: %1; color: %2; }"
               "QToolTip { background-color: %3; color: %4; border: none; padding: 2px; }"
               "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
               "QScrollBar::handle:vertical { background: %5; min-height: 24px; border-radius: 4px; }"
               "QScrollBar::handle:vertical:hover { background: %6; }"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
               "QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }"
               "QScrollBar::handle:horizontal { background: %5; min-width: 24px; border-radius: 4px; }"
               "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
               "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }")
        .arg(t.base.name(), t.text.name(), t.raised.name(), t.brightText.name(),
             t.raised.name(), t.highlight.name());
}

} // namespace nimbus
