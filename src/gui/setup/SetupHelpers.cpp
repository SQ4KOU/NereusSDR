#include "SetupHelpers.h"

#include <cmath>

#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

namespace NereusSDR {

SliderRow makeSliderRow(int min, int max, int initial,
                        const QString& suffix,
                        QWidget* parent)
{
    auto* container = new QWidget(parent);
    auto* layout    = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* slider = new QSlider(Qt::Horizontal, container);
    slider->setRange(min, max);
    slider->setValue(initial);

    auto* spin = new QSpinBox(container);
    spin->setRange(min, max);
    spin->setValue(initial);
    spin->setSuffix(suffix);
    spin->setFixedWidth(80);
    spin->setAlignment(Qt::AlignRight);

    // Bidirectional sync. QSignalBlocker on the opposite widget prevents the
    // receiver from re-emitting and creating an infinite loop. Consumers of
    // the row should connect to slider->valueChanged as the single source of
    // truth for model updates.
    QObject::connect(slider, &QSlider::valueChanged, spin, [spin](int v) {
        QSignalBlocker block(spin);
        spin->setValue(v);
    });
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged),
                     slider, [slider](int v) {
        QSignalBlocker block(slider);
        slider->setValue(v);
    });

    layout->addWidget(slider, /*stretch=*/1);
    layout->addWidget(spin,   /*stretch=*/0);

    return { slider, spin, container };
}

SliderRowD makeDoubleSliderRow(double min, double max, double initial,
                               int decimals,
                               const QString& suffix,
                               QWidget* parent)
{
    const int scale = static_cast<int>(std::pow(10, decimals));

    auto* container = new QWidget(parent);
    auto* layout    = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* slider = new QSlider(Qt::Horizontal, container);
    slider->setRange(static_cast<int>(min * scale),
                     static_cast<int>(max * scale));
    slider->setValue(static_cast<int>(initial * scale));

    auto* spin = new QDoubleSpinBox(container);
    spin->setDecimals(decimals);
    spin->setRange(min, max);
    spin->setValue(initial);
    spin->setSuffix(suffix);
    spin->setSingleStep(1.0 / scale);
    spin->setFixedWidth(90);
    spin->setAlignment(Qt::AlignRight);

    QObject::connect(slider, &QSlider::valueChanged, spin, [spin, scale](int v) {
        QSignalBlocker block(spin);
        spin->setValue(static_cast<double>(v) / scale);
    });
    QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                     slider, [slider, scale](double v) {
        QSignalBlocker block(slider);
        slider->setValue(static_cast<int>(v * scale));
    });

    layout->addWidget(slider, 1);
    layout->addWidget(spin,   0);

    return { slider, spin, container };
}

} // namespace NereusSDR
