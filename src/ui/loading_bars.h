#pragma once

#include <QtWidgets/QWidget>

namespace nimbus {

class LoadingBar : public QWidget {
    Q_OBJECT
public:
    explicit LoadingBar(QWidget* parent = nullptr);
    ~LoadingBar();

    void setProgress(float progress); // 0.0 to 1.0
    void setIndeterminate(bool indeterminate);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float m_progress = 0.0f;
    bool m_indeterminate = false;
    int m_animOffset = 0;
};

} // namespace nimbus