#pragma once

#include "MeterItem.h"
#include <QColor>
#include <QMap>
#include <QPointF>

namespace NereusSDR {

// From Thetis clsNeedleScalePwrItem (MeterManager.cs:14888+)
// Renders power scale text labels at calibration points around needle arcs.
class NeedleScalePwrItem : public MeterItem {
    Q_OBJECT

public:
    explicit NeedleScalePwrItem(QObject* parent = nullptr) : MeterItem(parent) {}

    void setLowColour(const QColor& c) { m_lowColour = c; }
    QColor lowColour() const { return m_lowColour; }

    void setHighColour(const QColor& c) { m_highColour = c; }
    QColor highColour() const { return m_highColour; }

    void setFontFamily(const QString& f) { m_fontFamily = f; }
    QString fontFamily() const { return m_fontFamily; }

    void setFontSize(float s) { m_fontSize = s; }
    float fontSize() const { return m_fontSize; }

    void setFontBold(bool b) { m_fontBold = b; }
    bool fontBold() const { return m_fontBold; }

    void setMarks(int m) { m_marks = m; }
    int marks() const { return m_marks; }

    void setShowMarkers(bool s) { m_showMarkers = s; }
    bool showMarkers() const { return m_showMarkers; }

    void setMaxPower(float p) { m_maxPower = p; }
    float maxPower() const { return m_maxPower; }

    void setDarkMode(bool d) { m_darkMode = d; }
    bool darkMode() const { return m_darkMode; }

    // Scale calibration: value (0-100 normalized) → normalized (x,y) position on gauge face
    void setScaleCalibration(const QMap<float, QPointF>& cal) { m_calibration = cal; }
    QMap<float, QPointF> scaleCalibration() const { return m_calibration; }
    void addCalibrationPoint(float value, float nx, float ny) {
        m_calibration.insert(value, QPointF(nx, ny));
    }

    Layer renderLayer() const override { return Layer::OverlayStatic; }
    void paint(QPainter& p, int widgetW, int widgetH) override;
    QString serialize() const override;
    bool deserialize(const QString& data) override;

private:
    // From Thetis renderNeedleScale (MeterManager.cs:31822-31823)
    static QString tidyPower(float watts, bool useMilliwatts);

    QColor  m_lowColour{0x80, 0x80, 0x80};    // Gray
    QColor  m_highColour{0xff, 0x00, 0x00};    // Red
    QString m_fontFamily{QStringLiteral("Trebuchet MS")};
    float   m_fontSize{20.0f};
    bool    m_fontBold{true};
    int     m_marks{7};
    bool    m_showMarkers{true};
    float   m_maxPower{150.0f};
    bool    m_darkMode{false};
    QMap<float, QPointF> m_calibration;
};

} // namespace NereusSDR
