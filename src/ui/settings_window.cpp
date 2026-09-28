#include "settings_window.h"

#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QLabel>

namespace nimbus {

SettingsWindow::SettingsWindow(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Settings");
    setMinimumSize(400, 300);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Settings (placeholder)", this));
}

SettingsWindow::~SettingsWindow() = default;

} // namespace nimbus