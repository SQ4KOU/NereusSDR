#pragma once

#include "MeterItem.h"
#include <QColor>

namespace NereusSDR {

// From Thetis clsSignalText (MeterManager.cs:20286+)
// Large text signal display with S-units/dBm/uV format switching.
class SignalTextItem : public MeterItem {
    Q_OBJECT

public:
    // From Thetis clsSignalText.Units enum (MeterManager.cs:20288)
    enum class Units { Dbm, SUnits, Uv };

    // From Thetis clsSignalText.BarStyle enum (MeterManager.cs:20294)
    enum class BarStyle { None, Line, SolidFilled, GradientFilled, Segments };

    explicit SignalTextItem(QObject* parent = nullptr);

    void setUnits(Units u) { m_units = u; }
    Units units() const { return m_units; }

    void setShowValue(bool s) { m_showValue = s; }
    bool showValue() const { return m_showValue; }

    void setShowPeakValue(bool s) { m_showPeakValue = s; }
    bool showPeakValue() const { return m_showPeakValue; }

    void setShowType(bool s) { m_showType = s; }
    bool showType() const { return m_showType; }

    void setPeakHold(bool p) { m_peakHold = p; }
    bool peakHold() const { return m_peakHold; }

    void setColour(const QColor& c) { m_colour = c; }
    QColor colour() const { return m_colour; }

    void setPeakValueColour(const QColor& c) { m_peakColour = c; }
    QColor peakValueColour() const { return m_peakColour; }

    void setMarkerColour(const QColor& c) { m_markerColour = c; }
    QColor markerColour() const { return m_markerColour; }

    void setFontFamily(const QString& f) { m_fontFamily = f; }
    QString fontFamily() const { return m_fontFamily; }

    void setFontSize(float s) { m_fontSize = s; }
    float fontSize() const { return m_fontSize; }

    void setBarStyleMode(BarStyle s) { m_barStyle = s; }
    BarStyle barStyleMode() const { return m_barStyle; }

    void setValue(double v) override;

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    void paint(QPainter& p, int widgetW, int widgetH) override;
    QString serialize() const override;
    bool deserialize(const QString& data) override;

private:
    // From Thetis console.cs — format helpers
    QString formatDbm(float dbm) const;
    QString formatSUnits(float dbm) const;
    QString formatUv(float dbm) const;
    QString formatValue(float dbm) const;

    // From Thetis Common.UVfromDBM
    static double uvFromDbm(double dbm);

    Units    m_units{Units::Dbm};
    bool     m_showValue{true};
    bool     m_showPeakValue{false};
    bool     m_showType{true};
    bool     m_peakHold{false};
    QColor   m_colour{0xff, 0x00, 0x00};         // Red (from Thetis line 20333)
    QColor   m_peakColour{0xff, 0x00, 0x00};      // Red (from Thetis line 20335)
    QColor   m_markerColour{0xff, 0xff, 0x00};    // Yellow (from Thetis line 20334)
    QString  m_fontFamily{QStringLiteral("Trebuchet MS")};
    float    m_fontSize{20.0f};
    BarStyle m_barStyle{BarStyle::None};

    // Smoothing (from Thetis line 20353-20354)
    float m_attackRatio{0.8f};
    float m_decayRatio{0.2f};
    float m_smoothedDbm{-140.0f};
    float m_peakDbm{-140.0f};
    int   m_peakHoldCounter{0};
};

} // namespace NereusSDR
