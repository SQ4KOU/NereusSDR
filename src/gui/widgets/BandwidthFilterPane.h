// src/gui/widgets/BandwidthFilterPane.h

// --- From src/gui/widgets/BandwidthFilterPane.h ---
// =================================================================
// src/gui/widgets/BandwidthFilterPane.h  (Longpath)
// =================================================================
//
// Longpath-original, nach der Vorlage des Betreibers („BANDWIDTH
// FILTER" der Vorlage, Bildschirmfoto vom 2026-08-20). Die Regeln
// hinter den Zahlen kommen aus Thetis, siehe SliceModel::widthToEdges
// und ::constrainFilter.
//
// Der Durchlass EINES Empfaengers auf einer echten Frequenzachse.
//
// ── Warum nicht das vorhandene Widget ───────────────────────────────
//
// FilterPassbandWidget (RxApplet) zeichnet ein FESTES Trapez: die Form
// aendert sich nicht mit der Breite, die Kanten sitzen immer an
// derselben Stelle. Das ist ein Schema — es sagt „hier ist ein
// Filter", nicht „hier liegt er".
//
// Die Vorlage macht es anders und das ist der ganze Gewinn: die Skala
// liest 13.137 MHz, nicht „−300 Hz". Die Frage, die man wirklich hat,
// ist „liegt meine Flanke auf 13.139?" — und die beantwortet nur eine
// Achse, auf der die Frequenz steht.
//
// Beide bleiben vorerst nebeneinander stehen. Das vorhandene ist eine
// kleine Anzeige im RxApplet, dies hier eine Arbeitsflaeche. Sobald
// klar ist, dass die neue traegt, gehoert die alte zurueckgebaut —
// zwei Stellen, an denen man dasselbe zieht, sind eine zu viel.
//
// ── Drei Ziehbereiche ───────────────────────────────────────────────
//
//   Kante links / rechts   nur diese Kante, die Breite aendert sich
//   Mitte                  der ganze Durchlass, die Breite BLEIBT
//
// Der mittlere ist der Grund fuer die Flaeche: bei CW schiebt man den
// Durchlass ueber eine Stoerung, ohne die Frequenz zu verlieren.
//
// ── Was hier NICHT passiert ─────────────────────────────────────────
//
// Begrenzt wird nicht hier. Die Flaeche meldet Wunschwerte; die Regeln
// (Seitenband, Deckel, Breite-beim-Verschieben) sitzen in
// SliceModel::constrainFilter, damit sie auch fuer CAT und Tastatur
// gelten. Eine Flaeche, die selbst begrenzt, ist die zweite Rechnung,
// die beim naechsten Umbau auseinanderlaeuft.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-20 — Original fuer NereusSDR von Martin Fischer,
//                 KI-gestuetzt ueber Anthropic Claude (Cowork).
//   2026-09-17 — Neu gezeichnet in der Richtung "Glas & Tiefe"
//                 (Stilblatt 3), Achse auf runden Frequenzen.
// =================================================================

// =================================================================
// src/gui/widgets/BandwidthFilterPane.h  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/widgets/BandwidthFilterPane.h
//   at revision 551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banners are copied above.
//
//   Upstream source has no top-of-file copyright or GPL header; the
//   project-level LICENSE (GPLv3) applies.
//
//   Namespace Longpath becomes NereusSDR.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - hoverCursor(): native cursor shapes in place of
//                 Qt::SizeAllCursor. J.J. Boyd (KG4VCF), AI-assisted
//                 via Anthropic Claude Code.
//   2026-10-11 - Paint cost, nothing visible changed: traceBand(), kept
//                 colors, pens, fonts and working lists, and the kept
//                 ground picture (see the .cpp). J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
// =================================================================

// Modification history (NereusSDR):
//   2026-10-12 - Track the approved workspace text minimum in paint caches.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.

#pragma once

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPen>
#include <QPolygonF>
#include <QString>
#include <QWidget>

#include <array>
#include <optional>

namespace NereusSDR {

// From Longpath src/gui/widgets/BandwidthFilterPane.h:61 [@551576e3]
class BandwidthFilterPane : public QWidget
{
    Q_OBJECT

public:
    explicit BandwidthFilterPane(QWidget* parent = nullptr);


    // Beschriftung links oben, „RX1" / „RX2".
    void setLabel(const QString& text);
    QString label() const { return m_label; }

    // Die Farbe dieses Empfaengers. In der Vorlage ist RX1 blau und
    // RX2 gruen — man vergleicht sie, ohne die Beschriftung zu lesen.
    void setAccent(const QColor& c);

    // Wo der Empfaenger steht. Die Achse wird darum herum gezeichnet.
    void setVfoFrequency(double hz);
    double vfoFrequency() const { return m_vfoHz; }
    int    spanHz() const { return m_spanHz; }
    /// Die geglaettete Kurve. Nur fuer Pruefungen: die Glaettung ist
    /// das, was der Bediener als "ruhig" oder "verzoegert" erlebt, und
    /// eine Behauptung darueber gehoert gemessen.
    const QVector<float>& traceForTest() const { return m_trace; }

    // Wie viel Band die Flaeche zeigt. Die Vorlage zeigt rund 10 kHz.
    void setSpan(int hz);
    int  span() const { return m_spanHz; }

    void setFilter(int low, int high);
    int  filterLow()  const { return m_low; }
    int  filterHigh() const { return m_high; }

    // Ohne Verbindung steht keine Frequenz auf der Achse. Eine
    // erfundene waere eine Behauptung — dieselbe Regel wie beim
    // Panadapter-Kopf und beim Rotorzeiger.
    void setHasFrequency(bool on);

    /// Der Spektrumausschnitt hinter dem Durchlass, in dBm, von der
    /// linken bis zur rechten Kante der angezeigten Spanne.
    ///
    /// Vorbild ist die Vorlage (vorgefuehrt am 2026-08-22): dort zeigt
    /// der Bandfilter das ECHTE Signal, und erst dadurch sieht man,
    /// ob die Kante an der richtigen Stelle sitzt. AetherSDR hat das
    /// nicht — sein FilterPassbandWidget ist ein reiner Kanteneditor.
    /// Nachgesehen, bevor gebaut wurde.
    void setTrace(const QVector<float>& dbm);

    /// Die Wunschhoehe. Ohne sie richtete sich ein frisch abgeloestes
    /// Fenster nur nach der Mindesthoehe (84) und der 160-Punkte-
    /// Untergrenze des Fensters — davon gingen Kopfzeile und Achse ab,
    /// und fuer 40 dB Kurve blieben rund 25 Punkte: das "flach", das
    /// der Betreiber am 2026-09-17 fotografiert hat. 140 Punkte geben
    /// der Kurve etwa 80, ein Sprechsignal 20 dB ueber dem Flur steht
    /// dann 40 Punkte hoch. Kleiner ziehen darf man weiterhin.
    QSize sizeHint() const override;

    /// NereusSDR: the rows a new trace can change, which is all that
    /// setTrace() asks to have repainted (see the .cpp).
    QRect traceBand() const;

    /// NereusSDR: the pixel factor of the kept ground picture, 0 while
    /// there is none. Only for tests: a frame check proves nothing about
    /// the kept picture unless it was in use.
    int keptGroundScaleForTest() const { return m_base.isNull() ? 0 : m_baseKey.scale; }

signals:
    // Beim Ziehen einer Kante. Die Breite darf sich aendern.
    void filterChanged(int low, int high);

    // Beim Ziehen der Mitte. Die Breite soll erhalten bleiben —
    // deshalb ein eigenes Signal: der Empfaenger ruft dann
    // SliceModel::setFilterCenter, das mit filterShift begrenzt.
    void filterCentreChanged(int centreHz);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void leaveEvent(QEvent*) override;
    void changeEvent(QEvent* ev) override;

private:
    enum class Zone { None, LowEdge, Body, HighEdge };

    // Welcher Bereich liegt unter x? Auch fuer den Zeigercursor: er
    // soll VORHER sagen, was ein Ziehen tun wird.
    Zone zoneAt(int x) const;

    // NereusSDR: the native cursor shape for a zone (see the .cpp).
    static Qt::CursorShape hoverCursor(Zone z);

    int    hzToX(int hz) const;
    int    xToHz(int x) const;
    QRect  plotRect() const;

    QString m_label{QStringLiteral("RX1")};
    QColor  m_accent;
    double  m_vfoHz{0.0};
    QVector<float> m_trace;
    /// Traeger Mittelwert — die blasse Bezugslinie, siehe paintEvent.
    QVector<float> m_avgLine;
    int     m_spanHz{10000};
    int     m_low{-2850};
    int     m_high{-150};
    bool    m_hasFrequency{false};

    Zone   m_drag{Zone::None};
    int    m_dragStartX{0};
    int    m_dragStartLow{0};
    int    m_dragStartHigh{0};
    Zone   m_hover{Zone::None};

    // NereusSDR: what paintEvent needs on every paint and what changes
    // seldom or never, kept between paints (2026-10-11, paint cost; see
    // the .cpp). None of it changes a pixel.

    // The role colors and the pens made from them (resolveColors).
    struct Colors {
        QColor insetBg;
        QColor grid;
        QColor gridLine;
        QColor accent;
        QColor passTop;
        QColor passBottom;
        QColor border;
        QColor traceLine;
        QColor fillTop;
        QColor fillBottom;
        QColor innerTop;
        QColor innerBottom;
        std::array<QPen, 3> glowPens;
        QPen   linePen;
        QPen   avgPen;
        QColor edge;
        QPen   edgePen;
        QPen   handlePen;
        QPen   handlePenActive;
        QColor tick;
        QColor handleTop;
        QColor handleBottom;
        QColor handleTopActive;
        QColor handleBottomActive;
        QColor faint;
        QColor ink;
        QColor labelText;
    };
    void resolveColors();
    Colors m_colors;

    // The fonts made from the widget's font, and their metrics
    // (resolveFonts).
    void resolveFonts();
    bool m_readableFonts{false};
    QFont m_fontBase;
    QFont m_valueFont;
    QFont m_smallFont;
    QFont m_capsFont;
    std::optional<QFontMetrics> m_valueMetrics;
    std::optional<QFontMetrics> m_capsMetrics;

    // Working lists of paintEvent.
    QVector<int>   m_marks;
    QVector<float> m_sortScratch;
    QPolygonF      m_poly;
    QPolygonF      m_avgPoly;

    // The ground under the curve as a kept picture, and what it was
    // drawn for (stampBase).
    struct BaseKey {
        QSize size;
        int scale{0};
        QImage::Format format{QImage::Format_Invalid};
        double vfoHz{0.0};
        int spanHz{0};
        int low{0};
        int high{0};
        bool operator==(const BaseKey& other) const = default;
    };
    void paintBase(QPainter& p, const QRect& r, const QVector<int>& marks, int xl, int xh) const;
    int  baseLayerScale(const QPainter& p, QImage::Format* format) const;
    void stampBase(QPainter& p, int scale, QImage::Format format,
                   const QRect& r, const QVector<int>& marks, int xl, int xh);
    QImage  m_base;
    BaseKey m_baseKey;

    // From Longpath src/gui/widgets/BandwidthFilterPane.h:164 [@551576e3]
    // Fangbereich an den Kanten. Groesser als die gezeichnete Pille,
    // damit man nicht zielen muss.
    static constexpr int kGrabPx = 8;

    // From Longpath src/gui/widgets/BandwidthFilterPane.h:169 [@551576e3]
    // Kleinster Schritt beim Ziehen. Hertz-genau zu ziehen ist auf
    // einer 10-kHz-Achse ohnehin Zufall, und krumme Werte lesen sich
    // schlecht.
    static constexpr int kStepHz = 10;
};

} // namespace NereusSDR
