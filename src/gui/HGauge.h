// src/gui/HGauge.h
#pragma once
#include <QWidget>

namespace NereusSDR {

class HGauge : public QWidget {
    Q_OBJECT
public:
    explicit HGauge(QWidget* parent = nullptr);

    void setRange(double min, double max);
    void setYellowStart(double val);
    void setRedStart(double val);
    void setReversed(bool rev);
    void setTitle(const QString& t);
    void setUnit(const QString& u);
    void setValue(double val);
    void setPeakValue(double val);
    void setTickLabels(const QStringList& labels);

    // Read-only accessor for unit tests + state inspection.  No
    // corresponding signal — HGauge is a passive read-only widget driven
    // only by its setValue setter.  Phase 3M-4 Task 13 (added for
    // PureSignalApplet wiring tests).
    double value() const noexcept { return m_value; }

    QSize sizeHint() const override { return {200, 30}; }
    QSize minimumSizeHint() const override { return {100, 26}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double m_min = 0.0;
    double m_max = 100.0;
    double m_value = 0.0;
    double m_peak = -999.0;
    double m_yellowStart = 80.0;
    double m_redStart = 90.0;
    bool   m_reversed = false;
    QString m_title;
    QString m_unit;
    QStringList m_tickLabels;
};

} // namespace NereusSDR
