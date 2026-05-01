#pragma once

#include <QWidget>
#include <QString>

class QLabel;

namespace NereusSDR {

// MetricLabel — labelled-metric pair widget for the right-side status strip.
// Renders [LABEL] [value] horizontally where LABEL is uppercase 9px and
// value is the actual number. Used for PSU/PA/CPU readouts.
class MetricLabel : public QWidget {
    Q_OBJECT

public:
    MetricLabel(const QString& label, const QString& initialValue,
                QWidget* parent = nullptr);

    void setLabel(const QString& l);
    void setValue(const QString& v);
    QString label() const noexcept { return m_label; }
    QString value() const noexcept { return m_value; }

private:
    void applyStyle();

    QString  m_label;
    QString  m_value;
    QLabel*  m_labelPart{nullptr};
    QLabel*  m_valuePart{nullptr};
};

} // namespace NereusSDR
