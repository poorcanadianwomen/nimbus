#pragma once

#include <QtWidgets/QDialog>

namespace nimbus {

class SettingsWindow : public QDialog {
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget* parent = nullptr);
    ~SettingsWindow();
};

} // namespace nimbus