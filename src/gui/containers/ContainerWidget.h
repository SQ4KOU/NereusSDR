#pragma once

#include <QWidget>
#include <QPoint>
#include <QSize>
#include <QColor>
#include <QString>

namespace NereusSDR {

enum class AxisLock {
    Left = 0, TopLeft, Top, TopRight,
    Right, BottomRight, Bottom, BottomLeft
};

enum class DockMode {
    PanelDocked,
    OverlayDocked,
    Floating
};

class ContainerWidget : public QWidget {
    Q_OBJECT
public:
    static constexpr int kMinContainerWidth = 24;
    static constexpr int kMinContainerHeight = 24;

    explicit ContainerWidget(QWidget* parent = nullptr);
    ~ContainerWidget() override;
};

} // namespace NereusSDR
