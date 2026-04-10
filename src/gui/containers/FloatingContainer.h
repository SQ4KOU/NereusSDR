#pragma once

#include <QWidget>
#include <QString>

namespace NereusSDR {

class ContainerWidget;

class FloatingContainer : public QWidget {
    Q_OBJECT
public:
    explicit FloatingContainer(int rxSource, QWidget* parent = nullptr);
    ~FloatingContainer() override;

private:
    int m_rxSource{1};
};

} // namespace NereusSDR
