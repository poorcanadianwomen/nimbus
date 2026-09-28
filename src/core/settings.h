#pragma once

#include <QtCore/QObject>
#include <QtCore/QSettings>
#include <QtCore/QVariant>

namespace nimbus {

enum class ThemePreset {
    kAsh,
    kCustom,
};

struct Theme {
    QString base = "#1e1e2e";
    QString surface = "#282838";
    QString surfaceRaised = "#303040";
    QString border = "#383848";
    QString text = "#cdd6f4";
    QString textMuted = "#7f849c";
    QString accent = "#89b4fa";
    QString accentMuted = "#2d4a6b";
    QString mention = "#fab387";
    QString mentionBg = "#3d2e1a";
    QString error = "#f38ba8";
    QString success = "#a6e3a1";

    static Theme makeAsh() { return {}; }
};

struct SettingsData {
    ThemePreset themePreset = ThemePreset::kAsh;
    Theme theme = Theme::makeAsh();
    bool reduceMotion = false;
    bool compactMode = true;
    int fontSize = 12;
    QString fontFamily = "JetBrains Mono";
};

class SettingsManager : public QObject {
    Q_OBJECT
public:
    explicit SettingsManager(QObject* parent = nullptr);

    const SettingsData& settings() const { return m_data; }
    SettingsData& mutableSettings() { return m_data; }

    void load();
    void save();

    static ThemePreset themePresetFromName(const QString& name);
    static QString themePresetToName(ThemePreset preset);

signals:
    void settingsChanged();

private:
    QSettings m_qsettings;
    SettingsData m_data;
};

} // namespace nimbus