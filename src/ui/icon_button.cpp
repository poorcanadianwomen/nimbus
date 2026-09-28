#include "icon_button.h"

namespace nimbus {

IconButton::IconButton(QWidget* parent)
    : QToolButton(parent) {
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setAutoRaise(true);
    setFixedSize(32, 32);
    setIconSize(QSize(16, 16));
}

IconButton::~IconButton() = default;

void IconButton::setIconName(const QString&) {}
void IconButton::setActive(bool) {}

} // namespace nimbus