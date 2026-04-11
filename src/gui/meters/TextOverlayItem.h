#pragma once

#include "MeterItem.h"
#include <QColor>
#include <QMap>

namespace NereusSDR {

// From Thetis clsTextOverlay (MeterManager.cs:18746+)
// Two-line text display with %VARIABLE% substitution.
class TextOverlayItem : public MeterItem {
    Q_OBJECT

public:
    explicit TextOverlayItem(QObject* parent = nullptr) : MeterItem(parent) {}

    // Template text with %PLACEHOLDER% tokens
    void setText1(const QString& t) { m_text1 = t; }
    QString text1() const { return m_text1; }

    void setText2(const QString& t) { m_text2 = t; }
    QString text2() const { return m_text2; }

    // Per-line styling
    void setTextColour1(const QColor& c) { m_textColour1 = c; }
    QColor textColour1() const { return m_textColour1; }

    void setTextColour2(const QColor& c) { m_textColour2 = c; }
    QColor textColour2() const { return m_textColour2; }

    void setTextBackColour1(const QColor& c) { m_textBackColour1 = c; }
    void setTextBackColour2(const QColor& c) { m_textBackColour2 = c; }

    void setShowTextBackColour1(bool s) { m_showTextBack1 = s; }
    void setShowTextBackColour2(bool s) { m_showTextBack2 = s; }

    void setShowBackPanel(bool s) { m_showBackPanel = s; }
    bool showBackPanel() const { return m_showBackPanel; }

    void setPanelBackColour1(const QColor& c) { m_panelBack1 = c; }
    void setPanelBackColour2(const QColor& c) { m_panelBack2 = c; }

    void setFontFamily1(const QString& f) { m_fontFamily1 = f; }
    void setFontFamily2(const QString& f) { m_fontFamily2 = f; }

    void setFontSize1(float s) { m_fontSize1 = s; }
    void setFontSize2(float s) { m_fontSize2 = s; }

    void setFontBold1(bool b) { m_fontBold1 = b; }
    void setFontBold2(bool b) { m_fontBold2 = b; }

    void setScrollX(float s) { m_scrollX = s; }
    float scrollX() const { return m_scrollX; }

    void setPadding(float p) { m_padding = p; }

    // External variable registry for substitution
    void setVariable(const QString& name, const QString& value) {
        m_variables[name.toUpper()] = value;
    }

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    void paint(QPainter& p, int widgetW, int widgetH) override;
    QString serialize() const override;
    bool deserialize(const QString& data) override;

private:
    // From Thetis parseText() (MeterManager.cs:19267)
    QString resolveText(const QString& templateText) const;

    // Templates
    QString m_text1;
    QString m_text2;

    // Line 1 styling
    QColor  m_textColour1{0xff, 0xff, 0xff};
    QColor  m_textBackColour1{0x40, 0x40, 0x40};
    bool    m_showTextBack1{false};
    QString m_fontFamily1{QStringLiteral("Trebuchet MS")};
    float   m_fontSize1{18.0f};
    bool    m_fontBold1{false};

    // Line 2 styling
    QColor  m_textColour2{0xff, 0xff, 0xff};
    QColor  m_textBackColour2{0x40, 0x40, 0x40};
    bool    m_showTextBack2{false};
    QString m_fontFamily2{QStringLiteral("Trebuchet MS")};
    float   m_fontSize2{18.0f};
    bool    m_fontBold2{false};

    // Panel
    bool   m_showBackPanel{true};
    QColor m_panelBack1{0x20, 0x20, 0x20};
    QColor m_panelBack2{0x20, 0x20, 0x20};

    // Scrolling
    float m_scrollX{0.0f};       // 0 = disabled, negative = scroll left
    float m_scrollOffset1{0.0f};
    float m_scrollOffset2{0.0f};

    float m_padding{0.1f};

    // Variable registry
    QMap<QString, QString> m_variables;
    mutable int m_precision{1};  // current precision for %PRECIS=n%
};

} // namespace NereusSDR
