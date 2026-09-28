#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QStandardPaths>

namespace nimbus {

SettingsManager::SettingsManager(QObject* parent)
    : QObject(parent),
      m_qsettings(QSettings::IniFormat, QSettings::UserScope, "nimbus", "nimbus") {}

void SettingsManager::load() {
    m_qsettings.beginGroup("theme");
    const QString presetName = m_qsettings.value("preset", "ash").toString();
    m_qsettings.endGroup();

    m_qsettings.beginGroup("appearance");
    m_data.reduceMotion = m_qsettings.value("reduceMotion", false).toBool();
    m_data.compactMode = m_qsettings.value("compactMode", true).toBool();
    m_data.fontSize = m_qsettings.value("fontSize", 12).toInt();
    m_data.fontFamily = m_qsettings.value("fontFamily", "JetBrains Mono").toString();
    m_qsettings.endGroup();

    m_data.themePreset = themePresetFromName(presetName);
    if (m_data.themePreset == ThemePreset::kCustom) {
        // Load custom theme from JSON
        const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/theme.json";
        if (QFile::exists(path)) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly)) {
                const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                const QJsonObject o = doc.object();
                m_data.theme.base = o.value("base").toString();
                m_data.theme.surface = o.value("surface").toString();
                m_data.theme.surfaceRaised = o.value("surfaceRaised").toString();
                m_data.theme.border = o.value("border").toString();
                m_data.theme.text = o.value("text").toString();
                m_data.theme.textMuted = o.value("textMuted").toString();
                m_data.theme.accent = o.value("accent").toString();
                m_data.theme.accentMuted = o.value("accentMuted").toString();
                m_data.theme.mention = o.value("mention").toString();
                m_data.theme.mentionBg = o.value("mentionBg").toString();
                m_data.theme.error = o.value("error").toString();
                m_data.theme.success = o.value("success").toString();
            }
        }
    } else {
        m_data.theme = Theme::makeAsh();
    }
}

void SettingsManager::save() {
    m_qsettings.beginGroup("theme");
    m_qsettings.setValue("preset", themePresetToName(m_data.themePreset));
    m_qsettings.endGroup();

    m_qsettings.beginGroup("appearance");
    m_qsettings.setValue("reduceMotion", m_data.reduceMotion);
    m_qsettings.setValue("compactMode", m_data.compactMode);
    m_qsettings.setValue("fontSize", m_data.fontSize);
    m_qsettings.setValue("fontFamily", m_data.fontFamily);
    m_qsettings.endGroup();

    if (m_data.themePreset == ThemePreset::kCustom) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(dir);
        const QString path = dir + "/theme.json";
        QJsonObject o;
        o["base"] = m_data.theme.base;
        o["surface"] = m_data.theme.surface;
        o["surfaceRaised"] = m_data.theme.surfaceRaised;
        o["border"] = m_data.theme.border;
        o["text"] = m_data.theme.text;
        o["textMuted"] = m_data.theme.textMuted;
        o["accent"] = m_data.theme.accent;
        o["accentMuted"] = m_data.theme.accentMuted;
        o["mention"] = m_data.theme.mention;
        o["mentionBg"] = m_data.theme.mentionBg;
        o["error"] = m_data.theme.error;
        o["success"] = m_data.theme.success;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        }
    }

    m_qsettings.sync();
    emit settingsChanged();
}

ThemePreset SettingsManager::themePresetFromName(const QString& name) {
    if (name.compare("ash", Qt::CaseInsensitive) == 0) return ThemePreset::kAsh;
    if (name.compare("custom", Qt::CaseInsensitive) == 0) return ThemePreset::kCustom;
    return ThemePreset::kAsh;
}

QString SettingsManager::themePresetToName(ThemePreset preset) {
    return preset == ThemePreset::kCustom ? "custom" : "ash";
}

} // namespace nimbus