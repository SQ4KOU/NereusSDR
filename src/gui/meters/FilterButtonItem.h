#pragma once

#include "ButtonBoxItem.h"

namespace NereusSDR {

// Filter preset buttons: F1-F10, Var1, Var2.
// Ported from Thetis clsFilterButtonBox (MeterManager.cs:7674+).
class FilterButtonItem : public ButtonBoxItem {
    Q_OBJECT

public:
    explicit FilterButtonItem(QObject* parent = nullptr);

    void setActiveFilter(int index);
    int activeFilter() const { return m_activeFilter; }

    void setFilterLabel(int index, const QString& label);

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    QString serialize() const override;
    bool deserialize(const QString& data) override;

signals:
    void filterClicked(int filterIndex);
    // From Thetis PopupFilterContextMenu (MeterManager.cs:7917)
    void filterContextRequested(int filterIndex);

private:
    void onButtonClicked(int index, Qt::MouseButton button);
    int m_activeFilter{-1};
    static constexpr int kFilterCount = 12;
};

} // namespace NereusSDR
