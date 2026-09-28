#pragma once

#include <QtWidgets/QToolButton>

namespace nimbus {

class IconButton : public QToolButton {
    Q_OBJECT
public:
    explicit IconButton(QWidget* parent = nullptr);
    ~IconButton();

    void setIconName(const QString& name);
    void setActive(bool active);

private:
    QString m_iconName;
    bool m_active = false;
};

} // namespace nimbus