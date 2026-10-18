// tests/tst_bandwidth_filter_pane_paint.cpp

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

// --- From src/gui/widgets/BandwidthFilterPane.cpp ---
// =================================================================
// src/gui/widgets/BandwidthFilterPane.cpp  (Longpath)
// =================================================================
//
// Longpath-original. Begruendung steht im Header.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-20 — Original fuer NereusSDR von Martin Fischer,
//                 KI-gestuetzt ueber Anthropic Claude (Cowork).
// =================================================================

// =================================================================
// tests/tst_bandwidth_filter_pane_paint.cpp  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/widgets/BandwidthFilterPane.h
//   src/gui/widgets/BandwidthFilterPane.cpp
//   at revision 551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banners are copied above.
//
//   Upstream source has no top-of-file copyright or GPL header; the
//   project-level LICENSE (GPLv3) applies.
//
//   What is ported here is the REFERENCE: ReferencePane below is a
//   line-for-line copy of the pane's drawing (paintEvent, setTrace,
//   plotRect, hzToX, zoneAt and the label helpers) as it stood in
//   src/gui/widgets/BandwidthFilterPane.cpp at commit 1a660005c, before
//   any work on its cost. The only additions are the lap() lines, each
//   marked "test:", which time the passes and change no drawing.
//
//   The test draws the same states through the real pane and through the
//   reference and requires every frame to be equal, pixel for pixel in
//   every channel. The reference is a copy, not a table of hashes, because
//   the frames hold text: a hash made on one machine would not hold on
//   another font set or operating system, and this test runs on three.
//   The commit that adds this file changes no product code, so its passing
//   run is the proof that the copy and the pane draw the same thing.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-11 - Reference copy and paint cost test for NereusSDR by
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//                 Code.
//   2026-10-11 - With the cost work in the pane: the band a new trace
//                 repaints, the frames behind the pane left alone, and
//                 the cost of a band repaint. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QBackingStore>
#include <QElapsedTimer>
#include <QImage>
#include <QLinearGradient>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

#include "gui/instruments/InstrumentStyle.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/widgets/BandwidthFilterPane.h"

using namespace NereusSDR;

// NereusSDR: the Longpath values, see gui/instruments/InstrumentStyle.h.
namespace Style = ::NereusSDR::InstrumentStyle;

namespace {

// test: the passes the reference times, in drawing order.
enum Lap {
    kLapBase = 0,     // ground, grid, passband strip, VFO line
    kLapScale,        // level range, path, level lines and dBm numbers
    kLapFillAll,      // pass 1: gradient under the whole curve
    kLapFillPass,     // pass 2: fill inside the passband
    kLapGlow0,        // pass 3: halo 8.0 wide
    kLapGlow1,        // pass 4: halo 4.0 wide
    kLapGlow2,        // pass 5: halo 2.5 wide
    kLapLine,         // pass 6: the line
    kLapAvg,          // pass 7: the average line
    kLapEdges,        // edge lines and handles
    kLapLabels,       // labels and chips
    kLapAxis,         // axis numbers
    kLapCount
};

constexpr std::array<const char*, kLapCount> kLapNames = {
    "ground, grid, strip, VFO line",
    "level range, path, level lines, dBm numbers",
    "pass 1 fill under the curve",
    "pass 2 fill in the passband",
    "pass 3 halo 8.0",
    "pass 4 halo 4.0",
    "pass 5 halo 2.5",
    "pass 6 line",
    "pass 7 average line",
    "edge lines, handles",
    "labels, chips",
    "axis numbers",
};

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:32 [@551576e3]
// Rand um die Zeichenflaeche. Oben mehr, weil dort die Beschriftungen
// der Kanten und die Breitenpille sitzen; unten fuer die Skala.
constexpr int kPadX      = 6;
constexpr int kPadTop    = 30;
constexpr int kPadBottom = 20;

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:39 [@551576e3]
// Eine Zahl auf der Achse: Megahertz mit drei Nachkommastellen, wie in
// der Vorlage („13.137"). Kilohertz waeren eine Ziffer mehr und sagen
// dasselbe.
QString axisLabel(double hz)
{
    return QString::number(hz / 1.0e6, 'f', 3);
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:55 [@551576e3]
// Eine Kantenangabe. Unter 1 kHz in Hertz, darueber in Kilohertz mit
// zwei Stellen — so macht es die Vorlage, und so bleibt die Zahl kurz
// genug, um ueber der Kante zu stehen.
//
// Betreiber 2026-09-03, mit Nachdruck: "minus darf nie!!!!!" — LSB legt
// beide Kanten unterhalb des Traegers, technisch also negativ, aber das
// gehoert nicht in die Anzeige. std::abs() schuetzte bisher nur die
// KILOHERTZ-JA-ODER-NEIN-Schwelle oben, nicht die ausgegebene Zahl
// selbst — "%1" mit dem rohen (moeglicherweise negativen) hz ergab
// "-150 Hz". Jetzt wird ueberall dieselbe Betragszahl verwendet wie die
// LOW/WIDTH/HIGH-Felder daneben (BandwidthFilterApplet::refreshNumbers).
QString cutLabel(int hz)
{
    const int mag = std::abs(hz);
    if (mag < 1000) {
        return QStringLiteral("%1 Hz").arg(mag);
    }
    return QStringLiteral("%1 kHz").arg(mag / 1000.0, 0, 'f', 2);
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:64 [@551576e3]
QString widthLabel(int hz)
{
    // Same "minus darf nie" guard as cutLabel() above -- m_high - m_low
    // can go negative if the edges are inverted (a distinct, separately
    // fixed bug), and this label must never expose that as a literal
    // minus sign either.
    const int mag = std::abs(hz);
    if (mag < 1000) { return QStringLiteral("%1 Hz").arg(mag); }
    return QStringLiteral("%1 kHz").arg(mag / 1000.0, 0, 'f', 1);
}

using Style::paintInsetTop;
using Style::paintGlassChip;

// The pane's drawing as it stood before the cost work. See the header.
class ReferencePane : public QWidget
{
public:
    enum class Zone { None, LowEdge, Body, HighEdge };

    // The pane's own limits (its constructor), so both take the same sizes.
    ReferencePane()
    {
        setMinimumHeight(84);
        setMinimumWidth(180);
    }

    void setTrace(const QVector<float>& dbm);
    Zone zoneAt(int x) const;
    int hzToX(int hz) const;
    QRect plotRect() const;

    QString m_label{QStringLiteral("RX1")};
    double  m_vfoHz{0.0};
    QVector<float> m_trace;
    QVector<float> m_avgLine;
    int     m_spanHz{10000};
    int     m_low{-2850};
    int     m_high{-150};
    bool    m_hasFrequency{false};
    Zone    m_drag{Zone::None};
    Zone    m_hover{Zone::None};

    static constexpr int kGrabPx = 8;

    // test: pass timing. Off unless a test turns it on.
    bool m_lapOn{false};
    QElapsedTimer m_lapTimer;
    qint64 m_lapLast{0};
    std::array<qint64, kLapCount> m_lapNs{};

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void lapStart()
    {
        if (m_lapOn) {
            m_lapTimer.start();
            m_lapLast = 0;
        }
    }
    void lap(int which)
    {
        if (m_lapOn) {
            const qint64 now = m_lapTimer.nsecsElapsed();
            m_lapNs[static_cast<size_t>(which)] += now - m_lapLast;
            m_lapLast = now;
        }
    }
};

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:109 [@551576e3]
void ReferencePane::setTrace(const QVector<float>& dbm)
{
    // ── Beruhigen, wie es OpenHPSDR zeigt ───────────────────────────
    //
    // Der Betreiber am 2026-08-23: "extrem unruhig, linie zu dick!
    // baue es genau wie bei openhpsdr."
    //
    // Die Rohwerte kommen mit 20 Bildern je Sekunde und sind
    // Spitzenwerte — jede Stuetzstelle springt bei jedem Bild. Bei
    // OpenHPSDR steht dort eine ruhige, duenne Kurve.
    //
    // Zwei Mittel, in dieser Reihenfolge:
    //
    //   1. ZEITLICH: exponentiell gleiten mit alpha = 0,22. Das ist
    //      dieselbe Familie wie die Glaettung im Panadapter
    //      (LogRecursive), nur ohne dessen Parameterwerk — hier geht
    //      es ums Augenmass beim Kantenziehen, nicht um Messtechnik.
    //      Ein Traeger steigt damit in rund einer Zehntelsekunde auf,
    //      das Zappeln verschwindet.
    //
    // ÖRTLICH wird NICHT geglaettet, und das ist eine Korrektur:
    //
    // Die erste Fassung mittelte ueber drei Stuetzstellen, mit dem
    // Kommentar, das verschlucke keinen echten Traeger — "der ist
    // breiter als drei Punkte". GEMESSEN war das falsch: ein Traeger
    // auf EINER Stuetzstelle, -60 dBm im -120er Rauschen, kam nach dem
    // Fenster bei -100 an. VIERZIG Dezibel weg, durch eine Zeile, die
    // ich fuer harmlos erklaert hatte.
    //
    // Zeitliches Gleiten allein genuegt: Rauschen ist von Bild zu Bild
    // zufaellig und mittelt sich damit ohnehin weg, ein Traeger steht
    // still und bleibt stehen.
    if (dbm.isEmpty()) {
        if (!m_trace.isEmpty()) { m_trace.clear(); update(); }
        return;
    }

    const QVector<float>& in = dbm;

    if (m_trace.size() != in.size()) {
        m_trace = in;                      // Groesse gewechselt: neu setzen
    } else {
        // From Longpath src/gui/widgets/BandwidthFilterPane.cpp:165 [@551576e3]
        // ── Schnell hoch, gemaechlich runter ────────────────────────
        //
        // Die erste Fassung glich in BEIDE Richtungen gleich schnell
        // (alpha 0,22). Der Betreiber sah daraufhin: "die form bleibt
        // auch immer leicht zu sehen, zeitversetzt" — ein blasses
        // Nachbild, das der Kurve hinterherlaeuft. Genau das macht
        // eine symmetrische Glaettung: sie verzoegert das Steigen
        // ebenso wie das Fallen.
        //
        // Richtig ist ungleich: STEIGEN sofort (ein Traeger, der
        // aufgeht, soll da sein, wenn er da ist), FALLEN gemaechlich
        // (dann beruhigt sich das Rauschen). Dieselbe Bauart wie ein
        // Spitzenwertzeiger mit Ruecklauf — und das ist auch, was
        // OpenHPSDR und Thetis an dieser Stelle zeigen.
        constexpr float aUp   = 0.75f;   // fast sofort
        constexpr float aDown = 0.16f;   // ruhiger Ruecklauf
        for (int i = 0; i < in.size(); ++i) {
            const float a = (in[i] > m_trace[i]) ? aUp : aDown;
            m_trace[i] = m_trace[i] * (1.0f - a) + in[i] * a;
        }
    }
    update();
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:205 [@551576e3]
QRect ReferencePane::plotRect() const
{
    return QRect(kPadX, kPadTop,
                 std::max(1, width() - 2 * kPadX),
                 std::max(1, height() - kPadTop - kPadBottom));
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:212 [@551576e3]
int ReferencePane::hzToX(int hz) const
{
    const QRect r = plotRect();
    const double frac = (static_cast<double>(hz) + m_spanHz / 2.0) / m_spanHz;
    return r.left() + static_cast<int>(std::lround(frac * r.width()));
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:227 [@551576e3]
ReferencePane::Zone ReferencePane::zoneAt(int x) const
{
    const int xl = hzToX(m_low);
    const int xh = hzToX(m_high);

    // Bei beiden in Reichweite gewinnt die naehere — sonst laesst sich
    // ein sehr schmaler Durchlass nur an einer Seite anfassen.
    const bool nearLow  = std::abs(x - xl) <= kGrabPx;
    const bool nearHigh = std::abs(x - xh) <= kGrabPx;
    if (nearLow && nearHigh) {
        return (std::abs(x - xl) <= std::abs(x - xh)) ? Zone::LowEdge
                                                      : Zone::HighEdge;
    }
    if (nearLow)  { return Zone::LowEdge; }
    if (nearHigh) { return Zone::HighEdge; }
    if (x > xl && x < xh) { return Zone::Body; }
    return Zone::None;
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:279 [@551576e3]
void ReferencePane::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    lapStart();   // test: timing only
    const QRect r = plotRect();

    // ── Grund: versenkt ─────────────────────────────────────────────
    p.fillRect(rect(), QColor(Style::role("inset-bg", Style::kInsetBg)));
    paintInsetTop(p, rect());
    p.setPen(QColor(255, 255, 255, 6));
    p.drawLine(0, height() - 1, width() - 1, height() - 1);

    // ── Die Marken der Achse: auf RUNDEN Frequenzen ─────────────────
    //
    // Bisher standen sie bei VFO ± k·Schritt. Steht der VFO auf
    // 7.192.500, liegen sie alle auf halben Kilohertz, und die
    // dreistellige Anzeige rundet mal auf, mal ab: 7.189 · 7.191 ·
    // 7.192 · 7.194 · 7.197 — gleiche Abstaende, ungleiche Zahlen, so
    // auf dem Foto vom 2026-09-17. Die Achse gehoert dem Band, nicht
    // dem VFO: Marken auf 7.188 · 7.190 · 7.192 …, die VFO-Linie steht
    // dazwischen, wo sie hingehoert. Gitter und Beschriftung nehmen
    // dieselbe Liste, damit die Zahlen etwas zum Festhalten haben.
    const int stepHz = (m_spanHz <= 6000) ? 1000
                     : (m_spanHz <= 24000) ? 2000 : 5000;
    QVector<int> marks;
    {
        const double first = std::ceil((m_vfoHz - m_spanHz / 2.0) / stepHz) * stepHz;
        for (double f = first; f <= m_vfoHz + m_spanHz / 2.0; f += stepHz) {
            marks << static_cast<int>(std::lround(f - m_vfoHz));
        }
    }

    QColor grid(Style::role("spectrum-grid", Style::kSpectrumGrid));
    grid.setAlpha(28);
    p.setPen(grid);
    for (int hz : marks) {
        const int x = hzToX(hz);
        p.drawLine(x, r.top() - 6, x, r.bottom());
    }

    // ── Der Durchlass als Glasstreifen ──────────────────────────────
    //
    // Von oben bis unten (Betreiber 2026-09-17), als Verlauf: oben
    // etwas dichter, unten leiser, mit einer Lichtkante am oberen
    // Rand. Er liegt UNTER der Kurve — die Kurve ist die Messung, der
    // Streifen nur das Fenster, durch das man sie hoert.
    const int xl = hzToX(m_low);
    const int xh = hzToX(m_high);
    const QColor accent(Style::role("accent", Style::kAccent));
    {
        QLinearGradient pass(0, 0, 0, height());
        QColor c0(accent); c0.setAlpha(54);
        QColor c1(accent); c1.setAlpha(24);
        pass.setColorAt(0.0, c0);
        pass.setColorAt(1.0, c1);
        p.fillRect(QRect(xl, 0, std::max(1, xh - xl), height()), pass);
        p.setPen(QColor(255, 255, 255, 26));
        p.drawLine(xl + 1, 0, xh - 1, 0);
    }

    // Die Nulllinie ist die VFO-Frequenz.
    {
        const int x0 = hzToX(0);
        p.setPen(QColor(Style::role("border", Style::kBorder)));
        p.drawLine(x0, r.top() - 6, x0, r.bottom());
    }

    lap(kLapBase);   // test: timing only

    // ── Das Signal ──────────────────────────────────────────────────
    //
    // Der Ausschnitt kommt vom Panadapter (SpectrumWidget::dbmOverRange)
    // — dieselbe Abbildung, dieselbe Kalibrierung. Boden ist das 10.
    // Perzentil (ein Ausreisser verschiebt sonst die Skala).
    //
    // Die Decke lag bis 2026-09-30 fest 40 dB darueber, und ein starkes
    // Signal stiess oben an — der Gipfel lief flach am Rand entlang, was
    // aussieht, als schluge die Kurve aus dem Rahmen. Betreiber am
    // 2026-09-30 an einem S9+12-Signal auf 40 m: "bandwith schlaegt ueber
    // den rahmen".
    //
    // Jetzt geht die Decke mit, sobald die Spitze sie erreicht: 40 dB
    // bleiben das MINDESTmass, damit ein ruhiges Band nicht zur flachen
    // Linie zusammenfaellt und die Skala zwischen zwei Baendern
    // vergleichbar bleibt — aber ein lautes Signal bekommt den Platz, den
    // es braucht, plus 3 dB Luft, damit der Gipfel nicht am Rahmen klebt.
    const QColor traceLine(Style::role("measured", Style::kAmberText));
    const int top = r.top() + 6;
    const int bot = r.bottom() - 2;
    if (m_trace.size() >= 2 && bot > top) {
        QVector<float> sorted = m_trace;
        std::sort(sorted.begin(), sorted.end());
        const float lo = sorted.at(sorted.size() / 10);
        const float spitze = sorted.constLast();
        const float hi = std::max(lo + 40.0f, spitze + 3.0f);
        const double yScale = (bot - top) / static_cast<double>(hi - lo);

        QPolygonF poly;
        poly.reserve(m_trace.size() + 2);
        poly << QPointF(r.left(), bot);
        for (int i = 0; i < m_trace.size(); ++i) {
            const double x = r.left()
                + (r.width() - 1.0) * i / (m_trace.size() - 1.0);
            const double y = bot - (m_trace[i] - lo) * yScale;
            poly << QPointF(x, qBound<double>(top, y, bot));
        }
        poly << QPointF(r.right(), bot);
        QPainterPath tracePath;
        tracePath.addPolygon(poly);

        // ── Pegelraster mit dBm-Marke ───────────────────────────────
        //
        // Drei waagrechte Linien. Die Zahlen nur, wenn sie Platz
        // haben (2026-09-17: bei elf Punkten Abstand beruehrten sich
        // alle drei) — eine Linie ohne Zahl ist immer noch ein Raster.
        {
            p.setFont(Style::monoFont(font(), Style::kFontCaption));
            QColor gridLine(grid); gridLine.setAlpha(44);
            const QColor gridText(Style::role("text-scale", Style::kTextScale));
            const int spacing = (bot - top) / 4;
            const bool roomForAll = spacing >= 14;
            const bool roomForOne = spacing >= 10;
            for (int k = 1; k <= 3; ++k) {
                const double frac = k / 4.0;
                const int y = static_cast<int>(bot - (bot - top) * frac);
                p.setPen(gridLine);
                p.drawLine(r.left() + 1, y, r.right() - 1, y);
                if (!roomForAll && !(roomForOne && k == 2)) { continue; }
                const int dbm = static_cast<int>(std::lround(lo + (hi - lo) * frac));
                p.setPen(gridText);
                p.drawText(QRect(r.right() - 46, y - 7, 42, 12),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QStringLiteral("%1").arg(dbm));
            }
        }

        lap(kLapScale);   // test: timing only
        const int xlF = qBound(r.left(), xl, r.right());
        const int xhF = qBound(r.left(), xh, r.right());

        // Kantenglaettung NUR fuer den Kurvenzug: Raster, Streifen und
        // Griffe sind waagrecht und senkrecht, Glaettung machte sie
        // nur unscharf.
        p.setRenderHint(QPainter::Antialiasing, true);

        // ── Verlauf unter der ganzen Kurve ──────────────────────────
        {
            QLinearGradient grad(0, top, 0, bot);
            QColor c0(traceLine); c0.setAlpha(96);
            QColor c1(traceLine); c1.setAlpha(0);
            grad.setColorAt(0.0, c0);
            grad.setColorAt(1.0, c1);
            p.setPen(Qt::NoPen);
            p.setBrush(grad);
            p.drawPath(tracePath);
        }

        lap(kLapFillAll);   // test: timing only

        // ── Im Durchlass gefuellt ───────────────────────────────────
        //
        // Was HOERBAR ist, steht als Flaeche da (2026-09-17: "leider
        // ist da nur ein kleiner strich"); ausserhalb bleibt der zarte
        // Verlauf — man sieht auf einen Blick, was die Kante
        // abschneidet.
        {
            p.save();
            p.setClipRect(QRect(xlF, top, std::max(1, xhF - xlF), bot - top + 1));
            QLinearGradient inner(0, top, 0, bot);
            QColor i0(traceLine); i0.setAlpha(150);
            QColor i1(traceLine); i1.setAlpha(64);
            inner.setColorAt(0.0, i0);
            inner.setColorAt(1.0, i1);
            p.setPen(Qt::NoPen);
            p.setBrush(inner);
            p.drawPath(tracePath);
            p.restore();
        }

        lap(kLapFillPass);   // test: timing only

        // ── Hof, dann Linie ─────────────────────────────────────────
        //
        // Der Hof ist es, was der Betreiber am OpenHPSDR-Bild als
        // "wirkt eher 3D" beschrieben hat (2026-08-23): kein breiter
        // Strich, sondern Durchgaenge von breit und blass nach schmal
        // und kraeftig. Drei, seit die Stuetzstellen dicht genug
        // liegen, dass keine Zacke zum Klumpen wird.
        {
            struct GlowPass { double width; int alpha; };
            static const GlowPass kGlow[] = { {8.0, 14}, {4.0, 30}, {2.5, 60} };
            p.setBrush(Qt::NoBrush);
            for (const GlowPass& g : kGlow) {
                QColor glow(traceLine);
                glow.setAlpha(g.alpha);
                QPen glowPen(glow, g.width);
                glowPen.setJoinStyle(Qt::RoundJoin);
                glowPen.setCapStyle(Qt::RoundCap);
                p.setPen(glowPen);
                p.drawPath(tracePath);
                lap(kLapGlow0 + static_cast<int>(&g - kGlow));   // test: timing only
            }
        }
        p.setPen(QPen(traceLine, 0.75));
        p.setBrush(Qt::NoBrush);
        p.drawPath(tracePath);

        lap(kLapLine);   // test: timing only

        // ── Die blasse Bezugslinie ──────────────────────────────────
        //
        // Der GEGLAETTETE Mittelwert, traege (alpha 0,02): sagt, wo
        // der Empfaenger im Mittel steht, und macht sichtbar, ob eine
        // Spitze wirklich heraussticht oder nur der Flur atmet.
        {
            if (m_avgLine.size() != m_trace.size()) {
                m_avgLine = m_trace;
            } else {
                // From Longpath src/gui/widgets/BandwidthFilterPane.cpp:488 [@551576e3]
                constexpr float aa = 0.02f;
                for (int i = 0; i < m_trace.size(); ++i) {
                    m_avgLine[i] = m_avgLine[i] * (1.0f - aa) + m_trace[i] * aa;
                }
            }
            QColor avg(Style::role("text-scale", Style::kTextScale));
            avg.setAlpha(60);
            p.setPen(QPen(avg, 1.0));
            QPolygonF ap;
            ap.reserve(m_avgLine.size());
            for (int i = 0; i < m_avgLine.size(); ++i) {
                const double x = r.left()
                    + (r.width() - 1.0) * i / (m_avgLine.size() - 1.0);
                const double y = bot - (m_avgLine[i] - lo) * yScale;
                ap << QPointF(x, qBound<double>(top, y, bot));
            }
            p.drawPolyline(ap);
        }
        lap(kLapAvg);   // test: timing only
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // ── Die Kanten ──────────────────────────────────────────────────
    //
    // Etwas heller als das Auswahlblau, damit sie auf dem Streifen
    // stehen; ueber die ganze Hoehe, weil die Zahlen oben und die
    // Achse unten zu dieser Saeule gehoeren.
    const QColor edge = accent.lighter(140);
    p.setPen(QPen(edge, 1));
    p.drawLine(xl, 0, xl, height());
    p.drawLine(xh, 0, xh, height());

    // ── Griffe: erhabene Pillen in halber Hoehe ─────────────────────
    //
    // Die ganze Kante als Griff waere groesser, sagt aber nicht, WO
    // man fassen soll. Erhaben: Verlauf, Lichtkante oben, dunkle
    // Unterkante — das Gegenstueck zur versenkten Flaeche.
    auto drawHandle = [&](int x, bool active) {
        const QRect h(x - 4, r.center().y() - 11, 9, 22);
        QLinearGradient g(0, h.top(), 0, h.bottom());
        g.setColorAt(0.0, QColor(active ? Style::kGlassSelTop : Style::kGlassBtnTop));
        g.setColorAt(1.0, QColor(active ? Style::kGlassSelBot : Style::kGlassBtnBot));
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QColor(0, 0, 0, 140));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(h.translated(0, 1), 4, 4);        // Schatten
        p.setPen(QPen(edge, active ? 1.5 : 1.0));
        p.setBrush(g);
        p.drawRoundedRect(h, 4, 4);
        p.setPen(QColor(255, 255, 255, Style::kGlassLightAlpha + 8));
        p.drawLine(h.left() + 3, h.top() + 1, h.right() - 3, h.top() + 1);   // Lichtkante
        QColor tick(edge); tick.setAlphaF(0.85f);
        p.setPen(tick);
        p.drawLine(x, h.top() + 6, x, h.bottom() - 6);
        p.restore();
    };
    drawHandle(xl, m_hover == Zone::LowEdge || m_drag == Zone::LowEdge);
    drawHandle(xh, m_hover == Zone::HighEdge || m_drag == Zone::HighEdge);

    lap(kLapEdges);   // test: timing only

    // ── Beschriftungen oben ─────────────────────────────────────────
    //
    // Zahlen Monospace (Hausstil Regel 4), auf der Schriftleiter: 11
    // fuer Werte, 9 fuer Achse und Marken. Wortmarken als Versalzeile.
    const QFont value = Style::monoFont(font(), Style::kFontSmall);
    const QFont small = Style::monoFont(font(), Style::kFontCaption);
    const QColor faint(Style::role("text-scale", Style::kTextScale));
    const QColor ink(Style::role("text", Style::kTextPrimary));

    // Eng: zuerst faellt das WORT, dann die Zahl (2026-08-22). Gemessen
    // am Platz zwischen den Griffen, nicht an der Fensterbreite — und
    // fuer die Zahlen am tatsaechlichen Platz NEBEN dem Breitenchip:
    // Monospace ist breiter als die Textschrift von frueher, und auf
    // dem 620×105-Blatt schob sich "3.00 kHz" in den Chip. Wo die Zahl
    // nicht neben den Chip passt, faellt sie; LOW/HIGH stehen ohnehin
    // in der Bedienzeile darunter.
    const int labelRoom = xh - xl;
    const bool wordMarks = labelRoom >= 190;
    const QString widthText = widthLabel(m_high - m_low);
    const int chipW  = QFontMetrics(value).horizontalAdvance(widthText) + 18;
    const int valueW = std::max(QFontMetrics(value).horizontalAdvance(cutLabel(m_low)),
                                QFontMetrics(value).horizontalAdvance(cutLabel(m_high)));
    const bool numbers = labelRoom >= chipW + 2 * (valueW + 14);

    if (wordMarks) {
        // Die Wortmarken folgen dem Seitenband: ohne Vorzeichen muss
        // das Wort sagen, was die Zahl ist. Bei LSB liegt die nahe
        // Kante rechts — links steht also der Audio-HOCHschnitt.
        p.setFont(Style::capsFont(font(), 8));
        p.setPen(faint);
        const bool lowerSideband = (m_low < 0 && m_high <= 0);
        p.drawText(QRect(xl + 5, 1, 90, 10),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   lowerSideband ? QStringLiteral("HIGH CUT")
                                 : QStringLiteral("LOW CUT"));
        p.drawText(QRect(xh - 95, 1, 90, 10),
                   Qt::AlignRight | Qt::AlignVCenter,
                   lowerSideband ? QStringLiteral("LOW CUT")
                                 : QStringLiteral("HIGH CUT"));
    }

    if (numbers) {
        p.setFont(value);
        p.setPen(ink);
        const int yVal = wordMarks ? 12 : 4;
        p.drawText(QRect(xl + 5, yVal, 90, 13),
                   Qt::AlignLeft | Qt::AlignVCenter, cutLabel(m_low));
        p.drawText(QRect(xh - 95, yVal, 90, 13),
                   Qt::AlignRight | Qt::AlignVCenter, cutLabel(m_high));
    }

    // Die Breite in der Mitte, in einem Glaschip: sie ist die Zahl,
    // nach der man den Filter benennt.
    {
        const int cx = (xl + xh) / 2;
        const QRect box(cx - chipW / 2, 7, chipW, 19);
        paintGlassChip(p, box, Style::kGlassChipRadius);
        p.setFont(value);
        p.setPen(ink);
        p.drawText(box, Qt::AlignCenter, widthText);
    }

    // ── Die Beschriftung des Empfaengers ────────────────────────────
    //
    // Eine Kapsel, kein Knopf: grau, nicht blau — sie ist keine
    // Bedienung, und "Blau ist anfassbar".
    {
        const QFont cap = Style::capsFont(font(), 8);
        const QFontMetrics fm(cap);
        const int tw = fm.horizontalAdvance(m_label) + 14;
        const QRect box(4, 4, tw, 14);
        paintGlassChip(p, box, 7);
        p.setFont(cap);
        p.setPen(QColor(Style::role("text-secondary", Style::kTextSecondary)));
        p.drawText(box, Qt::AlignCenter, m_label);
    }

    lap(kLapLabels);   // test: timing only

    // ── Die Achse ───────────────────────────────────────────────────
    //
    // Ohne Verbindung steht hier NICHTS. Eine erfundene Frequenz waere
    // eine Behauptung — dieselbe Regel wie beim Panadapter-Kopf.
    p.setFont(small);
    if (!m_hasFrequency) {
        p.setPen(faint);
        p.drawText(QRect(0, height() - kPadBottom, width(), kPadBottom),
                   Qt::AlignCenter, QStringLiteral("no radio"));
        lap(kLapAxis);   // test: timing only
        return;
    }

    // Zahlen mittig unter ihrer Marke, am Rand hineingeschoben statt
    // angeschnitten (".111" statt "7.111" ist schlimmer als keine).
    //
    // Eng — die Flaeche im RxApplet ist 180 Punkte breit — stuenden
    // fuenf Zahlen ineinander ("14.22014.222…" auf dem Blatt vom
    // 2026-09-17). Dann bekommt nur jede zweite (dritte, …) Marke eine
    // Zahl; die Gitterlinien bleiben alle. Gemessen an der breitesten
    // Zahl gegen den Markenabstand, nicht geraten.
    int every = 1;
    if (marks.size() >= 2) {
        int widest = 0;
        for (int hz : marks) {
            widest = qMax(widest, p.fontMetrics().horizontalAdvance(axisLabel(m_vfoHz + hz)));
        }
        const int gap = qAbs(hzToX(marks[1]) - hzToX(marks[0]));
        if (gap > 0) {
            while (every * gap < widest + 10) { ++every; }
        }
    }
    // Das Hineinschieben am Rand kann eine Zahl auf ihre Nachbarin
    // schieben (268 px breit: "14.22014.222", Blatt vom 2026-09-18).
    // Darum merkt sich die Schleife den rechten Rand der zuletzt
    // gezeichneten Zahl; was darueber laege, entfaellt — eine Zahl
    // weniger ist besser als zwei ineinander.
    int lastRight = -1000;
    for (int i = 0; i < marks.size(); ++i) {
        if (i % every != 0) { continue; }
        const int hz = marks[i];
        const int x = hzToX(hz);
        const QString t = axisLabel(m_vfoHz + hz);
        const int tw = p.fontMetrics().horizontalAdvance(t) + 8;
        int left = x - tw / 2;
        if (left < 1) { left = 1; }
        if (left + tw > width() - 1) { left = width() - 1 - tw; }
        if (left < lastRight + 4) { continue; }
        lastRight = left + tw;
        p.setPen(faint);
        p.drawText(QRect(left, height() - kPadBottom + 2, tw, 13),
                   Qt::AlignCenter, t);
    }
    lap(kLapAxis);   // test: timing only
}

// ── The test's own tools ─────────────────────────────────────────────

// The live pane feeds one point per 12 screen pixels, 64 to 400 of them
// (LongpathContent.cpp, kTraceDevicePxPerPoint); a Retina screen is two
// screen pixels per Qt pixel.
int pointsFor(int widthPx)
{
    return std::clamp(static_cast<int>(std::lround(widthPx * 2.0 / 12.0)), 64, 400);
}

// A made-up spectrum that is the same on every run: a noise floor that
// changes each tick, a voice-like hump and a carrier on one point.
QVector<float> syntheticTrace(int points, int tick)
{
    QVector<float> v(points);
    quint32 s = 0x9e3779b9u + static_cast<quint32>(tick) * 2654435761u;
    for (int i = 0; i < points; ++i) {
        s = s * 1664525u + 1013904223u;
        const float noise = static_cast<float>((s >> 8) & 0xffffu) / 65535.0f * 8.0f - 4.0f;
        float dbm = -118.0f + noise;
        const double frac = i / static_cast<double>(std::max(1, points - 1));
        if (frac > 0.55 && frac < 0.72) {
            dbm += 22.0f + 6.0f * static_cast<float>((tick + i) % 3);
        }
        if (i == points / 4) {
            dbm = -61.0f - static_cast<float>(tick % 4);
        }
        v[i] = dbm;
    }
    return v;
}

// A sawtooth between far below the floor and far above the ceiling: every
// second point is clamped to the top of the plot and the others to the
// bottom, so the halo reaches as far as it ever can.
QVector<float> sawtoothTrace(int points, int tick)
{
    QVector<float> v(points);
    for (int i = 0; i < points; ++i) {
        v[i] = ((i + tick) % 2 == 0) ? -150.0f : 20.0f;
    }
    return v;
}

QImage blankFrame(const QSize& size, qreal dpr, QImage::Format format, const QColor& fill)
{
    QImage img(size * dpr, format);
    img.setDevicePixelRatio(dpr);
    img.fill(fill);
    return img;
}

// The widget's real paint path, with nothing drawn for it first: whatever
// the frame holds afterwards, paintEvent put there.
void renderInto(QWidget& w, QImage& img, const QRegion& region = QRegion())
{
    const QPoint at = region.isEmpty() ? QPoint() : region.boundingRect().topLeft();
    w.render(&img, at, region, QWidget::RenderFlags());
}

// Empty when the frames are equal in size, format, pixel ratio and every
// channel of every pixel; otherwise what differs and where.
QString frameDifference(const QImage& want, const QImage& got)
{
    if (want.size() != got.size() || want.format() != got.format()
        || want.devicePixelRatio() != got.devicePixelRatio()) {
        return QStringLiteral("frame shape differs: %1x%2 format %3 ratio %4 against %5x%6 format %7 ratio %8")
            .arg(want.width()).arg(want.height()).arg(int(want.format())).arg(want.devicePixelRatio())
            .arg(got.width()).arg(got.height()).arg(int(got.format())).arg(got.devicePixelRatio());
    }
    int count = 0;
    int firstX = -1;
    int firstY = -1;
    int lastY = -1;
    for (int y = 0; y < want.height(); ++y) {
        for (int x = 0; x < want.width(); ++x) {
            if (want.pixel(x, y) != got.pixel(x, y)) {
                if (count == 0) {
                    firstX = x;
                    firstY = y;
                }
                lastY = y;
                ++count;
            }
        }
    }
    if (count == 0) {
        return QString();
    }
    return QStringLiteral("%1 pixels differ, rows %2 to %3; first at (%4, %5): want %6, got %7")
        .arg(count).arg(firstY).arg(lastY).arg(firstX).arg(firstY)
        .arg(want.pixel(firstX, firstY), 8, 16, QLatin1Char('0'))
        .arg(got.pixel(firstX, firstY), 8, 16, QLatin1Char('0'));
}

// First and last device row on which two frames differ; (-1, -1) if none.
QPair<int, int> differingRows(const QImage& a, const QImage& b)
{
    int first = -1;
    int last = -1;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) {
                if (first < 0) {
                    first = y;
                }
                last = y;
                break;
            }
        }
    }
    return {first, last};
}

void sendMouse(QWidget* w, QEvent::Type type, const QPoint& pos)
{
    const Qt::MouseButton button =
        (type == QEvent::MouseMove) ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButtons buttons =
        (type == QEvent::MouseButtonRelease || type == QEvent::MouseMove)
            ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent ev(type, QPointF(pos), QPointF(w->mapToGlobal(pos)),
                   button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

// The real pane and the reference, driven through the same states.
struct Rig {
    BandwidthFilterPane pane;
    ReferencePane ref;
    int tick{0};

    void resize(int w, int h)
    {
        pane.resize(w, h);
        ref.resize(w, h);
    }
    void label(const QString& text)
    {
        pane.setLabel(text);
        ref.m_label = text;
    }
    void vfo(double hz)
    {
        pane.setVfoFrequency(hz);
        ref.m_vfoHz = hz;
    }
    void span(int hz)
    {
        pane.setSpan(hz);
        ref.m_spanHz = std::clamp(hz, 2000, 40000);
    }
    void filter(int low, int high)
    {
        pane.setFilter(low, high);
        ref.m_low = low;
        ref.m_high = high;
    }
    void hasFrequency(bool on)
    {
        pane.setHasFrequency(on);
        ref.m_hasFrequency = on;
    }
    void enabled(bool on)
    {
        pane.setEnabled(on);
        ref.setEnabled(on);
    }
    void font(const QFont& f)
    {
        pane.setFont(f);
        ref.setFont(f);
    }
    void hoverAt(const QPoint& pos)
    {
        sendMouse(&pane, QEvent::MouseMove, pos);
        ref.m_hover = ref.zoneAt(pos.x());
    }
    void press(const QPoint& pos)
    {
        sendMouse(&pane, QEvent::MouseButtonPress, pos);
        ref.m_drag = ref.zoneAt(pos.x());
    }
    void release(const QPoint& pos)
    {
        sendMouse(&pane, QEvent::MouseButtonRelease, pos);
        ref.m_drag = ReferencePane::Zone::None;
        ref.m_hover = ref.zoneAt(pos.x());
    }
    void leave()
    {
        QEvent ev(QEvent::Leave);
        QCoreApplication::sendEvent(&pane, &ev);
        ref.m_hover = ReferencePane::Zone::None;
    }
    void feed(const QVector<float>& trace)
    {
        pane.setTrace(trace);
        ref.setTrace(trace);
    }
    void feedNext()
    {
        feed(syntheticTrace(pointsFor(pane.width()), tick++));
    }
    // Everything that decides the picture apart from the trace.
    QString stateWithoutTrace() const
    {
        return QStringLiteral("%1x%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12")
            .arg(ref.width()).arg(ref.height()).arg(ref.m_label).arg(ref.m_vfoHz, 0, 'f', 1)
            .arg(ref.m_spanHz).arg(ref.m_low).arg(ref.m_high).arg(ref.m_hasFrequency)
            .arg(int(ref.m_hover)).arg(int(ref.m_drag)).arg(ref.isEnabled())
            .arg(ref.font().toString());
    }
    void standardState()
    {
        resize(620, 105);
        label(QStringLiteral("RX1"));
        vfo(7192500.0);
        span(10000);
        filter(-2850, -150);
        hasFrequency(true);
    }
};

// Every state the pane can be in, one after the other on the same two
// widgets, so that anything the pane keeps between paints is also put
// through each change. frame(what) is called wherever a frame is due.
void runScript(Rig& rig, const std::function<void(const QString&)>& frame)
{
    auto ticks = [&](int n, const QString& what) {
        for (int i = 0; i < n; ++i) {
            rig.feedNext();
            frame(QStringLiteral("%1, tick %2").arg(what).arg(i + 1));
        }
    };

    rig.standardState();
    frame(QStringLiteral("620x105 lower sideband, no data yet"));
    ticks(6, QStringLiteral("620x105 lower sideband"));

    rig.filter(150, 2850);
    frame(QStringLiteral("upper sideband, same trace"));
    ticks(3, QStringLiteral("upper sideband"));

    rig.filter(-250, 250);
    ticks(2, QStringLiteral("narrow filter"));

    rig.filter(-2850, -150);
    const int y = rig.pane.height() / 2;
    rig.hoverAt(QPoint(rig.ref.hzToX(-2850), y));
    frame(QStringLiteral("pointer on the low edge"));
    ticks(2, QStringLiteral("pointer on the low edge"));
    rig.hoverAt(QPoint(rig.ref.hzToX(-1500), y));
    ticks(1, QStringLiteral("pointer on the body"));
    rig.hoverAt(QPoint(rig.ref.hzToX(-150), y));
    ticks(1, QStringLiteral("pointer on the high edge"));
    rig.press(QPoint(rig.ref.hzToX(-150), y));
    frame(QStringLiteral("high edge held"));
    ticks(2, QStringLiteral("high edge held"));
    rig.release(QPoint(rig.ref.hzToX(-1500), y));
    ticks(1, QStringLiteral("released over the body"));
    rig.leave();
    frame(QStringLiteral("pointer gone"));

    rig.span(24000);
    ticks(2, QStringLiteral("24 kHz span"));
    rig.span(5000);
    ticks(2, QStringLiteral("5 kHz span"));
    rig.span(10000);

    rig.vfo(14225000.0);
    ticks(2, QStringLiteral("20 m"));

    rig.label(QStringLiteral("Main RX2"));
    ticks(1, QStringLiteral("long label"));

    rig.hasFrequency(false);
    frame(QStringLiteral("no radio"));
    ticks(2, QStringLiteral("no radio"));
    rig.hasFrequency(true);

    rig.enabled(false);
    ticks(1, QStringLiteral("disabled"));
    rig.enabled(true);

    rig.resize(360, 140);
    frame(QStringLiteral("360x140, trace of the old size"));
    ticks(3, QStringLiteral("360x140"));
    rig.resize(180, 84);
    ticks(2, QStringLiteral("180x84, the smallest"));
    rig.resize(901, 333);
    ticks(2, QStringLiteral("901x333"));

    rig.resize(620, 105);
    rig.feed(QVector<float>(103, -100.0f));
    frame(QStringLiteral("flat trace"));
    rig.feed(sawtoothTrace(103, 0));
    frame(QStringLiteral("sawtooth through both limits"));
    rig.feed(sawtoothTrace(103, 1));
    frame(QStringLiteral("sawtooth, other phase"));
    rig.feed(QVector<float>(1, -80.0f));
    frame(QStringLiteral("one-point trace"));
    rig.feed(QVector<float>());
    frame(QStringLiteral("trace cleared"));
    ticks(3, QStringLiteral("trace back"));

    QFont big = rig.pane.font();
    big.setPixelSize(17);
    const QFont before = rig.pane.font();
    rig.font(big);
    ticks(2, QStringLiteral("larger widget font"));
    rig.font(before);
    ticks(2, QStringLiteral("widget font back"));
}

struct Stats {
    double minUs{0.0};
    double medianUs{0.0};
    double meanUs{0.0};
};

Stats statsOf(QVector<qint64> ns)
{
    Stats s;
    if (ns.isEmpty()) {
        return s;
    }
    std::sort(ns.begin(), ns.end());
    double sum = 0.0;
    for (qint64 v : ns) {
        sum += static_cast<double>(v);
    }
    s.minUs = ns.first() / 1000.0;
    s.medianUs = ns.at(ns.size() / 2) / 1000.0;
    s.meanUs = sum / ns.size() / 1000.0;
    return s;
}

QString statsLine(const Stats& s)
{
    return QStringLiteral("median %1 us, least %2 us, mean %3 us")
        .arg(s.medianUs, 0, 'f', 1).arg(s.minUs, 0, 'f', 1).arg(s.meanUs, 0, 'f', 1);
}

class PaintCounter : public QObject
{
public:
    int paints{0};

protected:
    bool eventFilter(QObject*, QEvent* ev) override
    {
        if (ev->type() == QEvent::Paint) {
            ++paints;
        }
        return false;
    }
};

} // namespace

class TstBandwidthFilterPanePaint : public QObject
{
    Q_OBJECT

private:
    // Reference frames of the last script run, kept so a failure can be
    // looked at. Set NEREUS_FILTER_PANE_FRAMES to a directory to have the
    // reference and pane frames written there as PNG files.
    QVector<QImage> m_referenceFrames;
    // The pixel factor of the pane's kept ground picture after each frame
    // of the last script run; 0 where it had none.
    QVector<int> m_keptScales;

    static int paintCount()
    {
        bool ok = false;
        const int asked = qEnvironmentVariableIntValue("NEREUS_FILTER_PANE_PAINTS", &ok);
        return (ok && asked > 0) ? asked : 40;
    }

    void compareScript(const std::function<qreal(int)>& ratioFor, QImage::Format format)
    {
        Rig rig;
        m_referenceFrames.clear();
        m_keptScales.clear();
        const QString dumpDir = qEnvironmentVariable("NEREUS_FILTER_PANE_FRAMES");
        int index = 0;
        runScript(rig, [&](const QString& what) {
            if (QTest::currentTestFailed()) {
                return;
            }
            const qreal dpr = ratioFor(index);
            // Different colors under the two frames: a pixel either widget
            // left unpainted would show as a difference.
            QImage want = blankFrame(rig.ref.size(), dpr, format, QColor(255, 0, 255));
            QImage got = blankFrame(rig.pane.size(), dpr, format, QColor(0, 255, 0));
            renderInto(rig.ref, want);
            renderInto(rig.pane, got);
            if (!dumpDir.isEmpty()) {
                want.save(QStringLiteral("%1/%2_ref.png").arg(dumpDir).arg(index, 3, 10, QLatin1Char('0')));
                got.save(QStringLiteral("%1/%2_pane.png").arg(dumpDir).arg(index, 3, 10, QLatin1Char('0')));
            }
            m_referenceFrames.append(want);
            m_keptScales.append(rig.pane.keptGroundScaleForTest());
            const QString diff = frameDifference(want, got);
            QVERIFY2(diff.isEmpty(),
                     qPrintable(QStringLiteral("frame %1 (%2, ratio %3): %4")
                                    .arg(index).arg(what).arg(dpr).arg(diff)));
            QVERIFY(want == got);
            ++index;
        });
        if (!QTest::currentTestFailed()) {
            QVERIFY(index >= 50);
        }
    }

private slots:
    void initTestCase()
    {
        // Qt's note on how long it took to list the fonts is not about
        // this test.
        QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.fonts=false"));
        WorkspacePreview::setEnabledForTest(false);
    }
    void cleanupTestCase() { WorkspacePreview::setEnabledForTest(false); }
    void init() { WorkspacePreview::setEnabledForTest(false); }
    void cleanup() { WorkspacePreview::setEnabledForTest(false); }
    void workspaceBandRepaintMatchesWholeFrame() {
        WorkspacePreview::setEnabledForTest(true);
        for(const QSize& size:{QSize(620,105),QSize(180,84)}) {
            for(qreal dpr:{qreal(1),qreal(2)}) {
                Rig rig; rig.standardState(); rig.resize(size.width(),size.height());
                Rig fullRig; fullRig.standardState(); fullRig.resize(size.width(),size.height());
                QImage initialWhole=blankFrame(size,dpr,QImage::Format_ARGB32_Premultiplied,Qt::transparent);
                renderInto(fullRig.pane,initialWhole);
                QImage kept=blankFrame(size,dpr,QImage::Format_ARGB32_Premultiplied,Qt::transparent);
                renderInto(rig.pane,kept);
                for(int frame=0;frame<8;++frame) {
                    const QVector<float> trace=sawtoothTrace(pointsFor(size.width()),frame);
                    rig.feed(trace); fullRig.feed(trace);
                    renderInto(rig.pane,kept,QRegion(rig.pane.traceBand()));
                    QImage whole=blankFrame(size,dpr,QImage::Format_ARGB32_Premultiplied,Qt::transparent);
                    // Average-line smoothing advances on each paint. Use an
                    // independently fed pane with the same paint count, so
                    // this compares partial vs whole rather than two time steps.
                    renderInto(fullRig.pane,whole);
                    const QString difference=frameDifference(whole,kept);
                    QVERIFY2(difference.isEmpty(),qPrintable(difference));
                }
            }
        }
        WorkspacePreview::setEnabledForTest(false);
    }


    void everyFrameEqualsTheReference_data()
    {
        QTest::addColumn<qreal>("dpr");
        QTest::addColumn<int>("format");
        QTest::newRow("ratio 1, with alpha") << qreal(1.0) << int(QImage::Format_ARGB32_Premultiplied);
        QTest::newRow("ratio 2, with alpha") << qreal(2.0) << int(QImage::Format_ARGB32_Premultiplied);
        QTest::newRow("ratio 1, opaque") << qreal(1.0) << int(QImage::Format_RGB32);
        QTest::newRow("ratio 2, opaque") << qreal(2.0) << int(QImage::Format_RGB32);
        QTest::newRow("ratio 3, with alpha") << qreal(3.0) << int(QImage::Format_ARGB32_Premultiplied);
        QTest::newRow("ratio 1.5, with alpha") << qreal(1.5) << int(QImage::Format_ARGB32_Premultiplied);
        // A picture format the pane's kept ground is not used for.
        QTest::newRow("ratio 2, other byte order") << qreal(2.0) << int(QImage::Format_RGBA8888_Premultiplied);
    }

    void everyFrameEqualsTheReference()
    {
        QFETCH(qreal, dpr);
        QFETCH(int, format);
        compareScript([dpr](int) { return dpr; }, static_cast<QImage::Format>(format));
        if (QTest::currentTestFailed()) {
            return;
        }
        // Equal frames say nothing about the kept ground picture unless
        // it was in use: at a whole ratio into one of Qt's two window
        // picture formats it is, save for the one frame too large to
        // keep; at a fractional ratio or into another format, never.
        const bool whole = dpr == std::floor(dpr);
        const bool windowFormat = format == int(QImage::Format_ARGB32_Premultiplied)
            || format == int(QImage::Format_RGB32);
        const qsizetype kept = m_keptScales.size() - m_keptScales.count(0);
        if (whole && windowFormat) {
            QVERIFY2(kept >= m_keptScales.size() - 12,
                     qPrintable(QStringLiteral("kept ground used on %1 of %2 frames")
                                    .arg(kept).arg(m_keptScales.size())));
            for (int scale : std::as_const(m_keptScales)) {
                QVERIFY(scale == 0 || scale == static_cast<int>(dpr));
            }
        } else {
            QCOMPARE(kept, qsizetype(0));
        }
    }

    // The same script with the pixel ratio changing from frame to frame,
    // as when a window is dragged between two screens.
    void everyFrameEqualsTheReferenceAcrossScreens()
    {
        compareScript([](int index) {
            constexpr std::array<qreal, 5> kRatios = {1.0, 2.0, 2.0, 1.0, 1.5};
            return kRatios[static_cast<size_t>(index) % kRatios.size()];
        }, QImage::Format_ARGB32_Premultiplied);
    }

    // Which rows a new trace can change. The reference is drawn twice with
    // nothing changed between the frames but the trace, for the traces
    // that reach furthest; no row outside the plot band may differ.
    void aNewTraceChangesOnlyThePlotBand_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<qreal>("dpr");
        QTest::newRow("620x105 ratio 1") << QSize(620, 105) << qreal(1.0);
        QTest::newRow("620x105 ratio 2") << QSize(620, 105) << qreal(2.0);
        QTest::newRow("180x84 ratio 2") << QSize(180, 84) << qreal(2.0);
        QTest::newRow("901x333 ratio 1") << QSize(901, 333) << qreal(1.0);
    }

    void aNewTraceChangesOnlyThePlotBand()
    {
        QFETCH(QSize, size);
        QFETCH(qreal, dpr);
        Rig rig;
        rig.standardState();
        rig.resize(size.width(), size.height());
        const int points = pointsFor(size.width());

        // The band the pane asks to have repainted for a new trace. The
        // rows are measured on the reference, the drawing as it was.
        const QRect band = rig.pane.traceBand();
        const int bandTop = band.top();
        const int bandBottom = band.bottom();
        QCOMPARE(band.left(), 0);
        QCOMPARE(band.width(), size.width());
        QVERIFY(bandTop > 0 && bandBottom < size.height() - 1);

        int firstSeen = -1;
        int lastSeen = -1;
        QImage previous;
        for (int i = 0; i < 12; ++i) {
            if (i < 4) {
                rig.feed(sawtoothTrace(points, i));
            } else if (i < 6) {
                rig.feed(QVector<float>(points, (i == 4) ? -140.0f : 10.0f));
            } else {
                rig.feedNext();
            }
            QImage frame = blankFrame(size, dpr, QImage::Format_ARGB32_Premultiplied, QColor(255, 0, 255));
            renderInto(rig.ref, frame);
            if (!previous.isNull()) {
                const QPair<int, int> rows = differingRows(previous, frame);
                if (rows.first >= 0) {
                    firstSeen = (firstSeen < 0) ? rows.first : std::min(firstSeen, rows.first);
                    lastSeen = std::max(lastSeen, rows.second);
                }
            }
            previous = frame;
        }
        QVERIFY2(firstSeen >= 0, "the traces changed nothing: the check would prove nothing");
        const int firstRow = static_cast<int>(std::floor(firstSeen / dpr));
        const int lastRow = static_cast<int>(std::floor(lastSeen / dpr));
        qInfo().noquote() << QStringLiteral("trace reach: rows %1 to %2 of %3; band %4 to %5")
                                 .arg(firstRow).arg(lastRow).arg(size.height()).arg(bandTop).arg(bandBottom);
        QVERIFY2(firstRow >= bandTop,
                 qPrintable(QStringLiteral("row %1 changed, above the band at %2").arg(firstRow).arg(bandTop)));
        QVERIFY2(lastRow <= bandBottom,
                 qPrintable(QStringLiteral("row %1 changed, below the band at %2").arg(lastRow).arg(bandBottom)));
    }

    // What a band repaint leaves on the screen. One picture is kept, as a
    // window keeps one: after every new trace only the pane's trace band
    // is painted onto it, and the whole picture must equal the reference
    // frame drawn from scratch. Any change of another kind repaints it
    // all, as the pane's other setters do.
    void aBandRepaintLeavesTheReferenceFrame_data()
    {
        QTest::addColumn<qreal>("dpr");
        QTest::addColumn<int>("format");
        QTest::newRow("ratio 1, with alpha") << qreal(1.0) << int(QImage::Format_ARGB32_Premultiplied);
        QTest::newRow("ratio 2, with alpha") << qreal(2.0) << int(QImage::Format_ARGB32_Premultiplied);
        QTest::newRow("ratio 2, opaque") << qreal(2.0) << int(QImage::Format_RGB32);
        QTest::newRow("ratio 1.5, with alpha") << qreal(1.5) << int(QImage::Format_ARGB32_Premultiplied);
    }

    void aBandRepaintLeavesTheReferenceFrame()
    {
        QFETCH(qreal, dpr);
        QFETCH(int, format);
        const QImage::Format fmt = static_cast<QImage::Format>(format);
        Rig rig;
        QImage screen;
        int bandPaints = 0;
        int index = 0;
        QString lastState;
        runScript(rig, [&](const QString& what) {
            if (QTest::currentTestFailed()) {
                return;
            }
            QImage want = blankFrame(rig.ref.size(), dpr, fmt, QColor(255, 0, 255));
            renderInto(rig.ref, want);
            // A band repaint is what follows a new trace when nothing
            // else changed since the last frame: every other change makes
            // the pane ask for a whole repaint.
            const QString state = rig.stateWithoutTrace();
            const bool traceOnly = state == lastState
                && screen.size() == want.size()
                && !rig.pane.traceForTest().isEmpty();
            lastState = state;
            if (traceOnly) {
                renderInto(rig.pane, screen, QRegion(rig.pane.traceBand()));
                ++bandPaints;
            } else {
                screen = blankFrame(rig.pane.size(), dpr, fmt, QColor(0, 255, 0));
                renderInto(rig.pane, screen);
            }
            const QString diff = frameDifference(want, screen);
            QVERIFY2(diff.isEmpty(),
                     qPrintable(QStringLiteral("frame %1 (%2, %3): %4")
                                    .arg(index).arg(what)
                                    .arg(traceOnly ? QStringLiteral("band repaint") : QStringLiteral("whole repaint"))
                                    .arg(diff)));
            ++index;
        });
        if (!QTest::currentTestFailed()) {
            QVERIFY2(bandPaints >= 25, qPrintable(QStringLiteral("only %1 band repaints").arg(bandPaints)));
        }
    }

    // The pane tells Qt it paints every pixel itself. That holds only
    // while its ground color is opaque, which the frame tests above prove
    // by drawing the reference and the pane over different colors.
    void thePaneIsOpaque()
    {
        BandwidthFilterPane pane;
        QVERIFY(pane.testAttribute(Qt::WA_OpaquePaintEvent));
        const QColor ground(Style::role("inset-bg", Style::kInsetBg));
        QVERIFY(ground.isValid());
        QCOMPARE(ground.alpha(), 255);
    }

    // The pane in a real (offscreen) window: what reaches the window's
    // own picture after each tick, through Qt's repaint handling, equals
    // the reference frame.
    void theWindowShowsTheReferenceFrames()
    {
        QWidget host;
        host.resize(700, 220);
        Rig rig;
        rig.standardState();
        rig.pane.setParent(&host);
        rig.pane.move(7, 11);
        host.show();
        if (!QTest::qWaitForWindowExposed(&host, 3000)) {
            QSKIP("the offscreen window was never exposed; the window picture could not be read");
        }
        QCoreApplication::processEvents();

        QBackingStore* store = host.backingStore();
        if (store == nullptr || store->paintDevice() == nullptr
            || store->paintDevice()->devType() != QInternal::Image) {
            QSKIP("this platform's window picture is not an image; it could not be read");
        }
        const QImage* window = static_cast<const QImage*>(store->paintDevice());
        const qreal dpr = window->devicePixelRatio();

        PaintCounter panePaints;
        rig.pane.installEventFilter(&panePaints);

        auto check = [&](const QString& what) {
            if (QTest::currentTestFailed()) {
                return;
            }
            QImage want = blankFrame(rig.ref.size(), dpr, window->format(), QColor(255, 0, 255));
            renderInto(rig.ref, want);
            const QRect area(rig.pane.pos() * dpr, rig.pane.size() * dpr);
            QImage got = window->copy(area);
            got.setDevicePixelRatio(dpr);
            const QString diff = frameDifference(want, got);
            QVERIFY2(diff.isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(what, diff)));
        };

        for (int i = 0; i < 12; ++i) {
            if (i == 5) {
                rig.filter(150, 2850);
            }
            if (i == 8) {
                rig.hoverAt(QPoint(rig.ref.hzToX(150), 50));
            }
            const int before = panePaints.paints;
            rig.feedNext();
            QTRY_COMPARE(panePaints.paints, before + 1);
            QCoreApplication::processEvents();
            // One paint per tick, no more: the average line moves a step
            // on every paint, so an extra one would show.
            QCOMPARE(panePaints.paints, before + 1);
            check(QStringLiteral("window tick %1").arg(i + 1));
        }
        // The pane belongs to the rig, not to the window.
        rig.pane.setParent(nullptr);
    }

    // What one paint costs, and where it goes. Offscreen raster drawing
    // into an image: a measure of the drawing work, not of a screen. The
    // reference and the pane are painted turn about, so whatever else the
    // machine is doing falls on both alike. NEREUS_FILTER_PANE_PAINTS sets
    // the number of paints (40 if unset).
    void paintCost_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<qreal>("dpr");
        QTest::addColumn<int>("format");
        QTest::newRow("620x105 ratio 2") << QSize(620, 105) << qreal(2.0)
                                         << int(QImage::Format_ARGB32_Premultiplied);
        QTest::newRow("620x105 ratio 1") << QSize(620, 105) << qreal(1.0)
                                         << int(QImage::Format_ARGB32_Premultiplied);
        // A picture format the pane's kept ground is not used for, so the
        // ground is drawn directly: what the kept colors, fonts and lists
        // save without it.
        QTest::newRow("620x105 ratio 2, ground drawn directly")
            << QSize(620, 105) << qreal(2.0) << int(QImage::Format_RGBA8888_Premultiplied);
    }

    void paintCost()
    {
        QFETCH(QSize, size);
        QFETCH(qreal, dpr);
        QFETCH(int, format);
        const QImage::Format fmt = static_cast<QImage::Format>(format);
        constexpr int kWarmUp = 20;
        const int paints = paintCount();

        Rig rig;
        rig.standardState();
        rig.resize(size.width(), size.height());

        QImage refFrame = blankFrame(size, dpr, fmt, QColor(255, 0, 255));
        QImage paneFrame = blankFrame(size, dpr, fmt, QColor(0, 255, 0));
        QElapsedTimer timer;

        // The parts that do not move, alone: no trace yet.
        QVector<qint64> refStill;
        QVector<qint64> paneStill;
        for (int i = 0; i < kWarmUp + paints; ++i) {
            timer.start();
            renderInto(rig.ref, refFrame);
            const qint64 refTook = timer.nsecsElapsed();
            timer.start();
            renderInto(rig.pane, paneFrame);
            const qint64 paneTook = timer.nsecsElapsed();
            if (i >= kWarmUp) {
                refStill.append(refTook);
                paneStill.append(paneTook);
            }
        }
        QString diff = frameDifference(refFrame, paneFrame);
        QVERIFY2(diff.isEmpty(), qPrintable(diff));

        // The whole picture, a new trace before every paint. The pane is
        // painted as it is in a window after a new trace: its trace band
        // only, onto the picture it painted before.
        QVector<qint64> refNs;
        QVector<qint64> paneNs;
        const QRegion band(rig.pane.traceBand());
        for (int i = 0; i < kWarmUp + paints; ++i) {
            rig.feedNext();
            const bool counted = i >= kWarmUp;
            rig.ref.m_lapOn = counted;
            timer.start();
            renderInto(rig.ref, refFrame);
            const qint64 refTook = timer.nsecsElapsed();
            timer.start();
            renderInto(rig.pane, paneFrame, band);
            const qint64 paneTook = timer.nsecsElapsed();
            if (counted) {
                refNs.append(refTook);
                paneNs.append(paneTook);
            }
        }
        rig.ref.m_lapOn = false;
        diff = frameDifference(refFrame, paneFrame);
        QVERIFY2(diff.isEmpty(), qPrintable(diff));

        // The same again with the pane painted whole, as the reference is.
        QVector<qint64> refWholeNs;
        QVector<qint64> paneWholeNs;
        for (int i = 0; i < kWarmUp + paints; ++i) {
            rig.feedNext();
            timer.start();
            renderInto(rig.ref, refFrame);
            const qint64 refTook = timer.nsecsElapsed();
            timer.start();
            renderInto(rig.pane, paneFrame);
            const qint64 paneTook = timer.nsecsElapsed();
            if (i >= kWarmUp) {
                refWholeNs.append(refTook);
                paneWholeNs.append(paneTook);
            }
        }
        diff = frameDifference(refFrame, paneFrame);
        QVERIFY2(diff.isEmpty(), qPrintable(diff));

        qInfo().noquote() << QStringLiteral("%1x%2 at ratio %3, %4 paints, %5 trace points")
                                 .arg(size.width()).arg(size.height()).arg(dpr).arg(paints)
                                 .arg(pointsFor(size.width()));
        qInfo().noquote() << "  no trace, reference whole:    " << statsLine(statsOf(refStill));
        qInfo().noquote() << "  no trace, pane whole:         " << statsLine(statsOf(paneStill));
        qInfo().noquote() << "  with trace, reference whole:  " << statsLine(statsOf(refNs));
        qInfo().noquote() << "  with trace, pane band only:   " << statsLine(statsOf(paneNs));
        qInfo().noquote() << "  with trace, reference whole:  " << statsLine(statsOf(refWholeNs));
        qInfo().noquote() << "  with trace, pane whole:       " << statsLine(statsOf(paneWholeNs));

        qint64 lapTotal = 0;
        for (qint64 ns : rig.ref.m_lapNs) {
            lapTotal += ns;
        }
        QVERIFY(lapTotal > 0);
        qInfo().noquote() << "  the reference, pass by pass (mean per paint, share):";
        for (int i = 0; i < kLapCount; ++i) {
            const qint64 ns = rig.ref.m_lapNs[static_cast<size_t>(i)];
            qInfo().noquote() << QStringLiteral("    %1 us  %2%  %3")
                                     .arg(ns / 1000.0 / paints, 8, 'f', 1)
                                     .arg(100.0 * ns / lapTotal, 5, 'f', 1)
                                     .arg(QLatin1String(kLapNames[static_cast<size_t>(i)]));
        }
    }

    // One tick in a window, from the new trace to the finished repaint,
    // through Qt's repaint handling: the reference and the pane each sit
    // in their own window, two styled frames deep. A stand-in for the
    // application's window, which has more and costlier layers; the
    // offscreen platform has one pixel per Qt pixel.
    void repaintCostInAWindow()
    {
        constexpr int kWarmUp = 20;
        const int paints = paintCount();

        struct Window {
            QWidget top;
            QWidget* inner{nullptr};
            PaintCounter panePaints;
            PaintCounter framePaints;
            void build(QWidget* pane)
            {
                top.setObjectName(QStringLiteral("top"));
                top.setAttribute(Qt::WA_StyledBackground, true);
                top.setStyleSheet(QStringLiteral(
                    "#top { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #17171b, stop:1 #0e0e10); }"
                    "#inner { background: #0c0c0e; border: 1px solid #2c2c31; border-radius: 10px; }"));
                inner = new QWidget(&top);
                inner->setObjectName(QStringLiteral("inner"));
                inner->setAttribute(Qt::WA_StyledBackground, true);
                auto* outerLayout = new QVBoxLayout(&top);
                outerLayout->addWidget(inner);
                auto* innerLayout = new QVBoxLayout(inner);
                innerLayout->addWidget(pane);
                pane->installEventFilter(&panePaints);
                inner->installEventFilter(&framePaints);
                top.installEventFilter(&framePaints);
                top.resize(660, 150);
                top.show();
            }
        };

        Rig rig;
        rig.standardState();
        Window refWindow;
        Window paneWindow;
        refWindow.build(&rig.ref);
        paneWindow.build(&rig.pane);
        if (!QTest::qWaitForWindowExposed(&refWindow.top, 3000)
            || !QTest::qWaitForWindowExposed(&paneWindow.top, 3000)) {
            rig.ref.setParent(nullptr);
            rig.pane.setParent(nullptr);
            QSKIP("the offscreen windows were never exposed; nothing could be timed");
        }
        QCoreApplication::processEvents();
        QCOMPARE(rig.ref.size(), rig.pane.size());

        QVector<qint64> refNs;
        QVector<qint64> paneNs;
        QElapsedTimer timer;
        int refFrames = 0;
        int paneFrames = 0;
        for (int i = 0; i < kWarmUp + paints; ++i) {
            const QVector<float> trace = syntheticTrace(pointsFor(rig.pane.width()), rig.tick++);
            const bool counted = i >= kWarmUp;

            int before = refWindow.panePaints.paints;
            int framesBefore = refWindow.framePaints.paints;
            timer.start();
            rig.ref.setTrace(trace);
            while (refWindow.panePaints.paints == before && timer.elapsed() < 2000) {
                QCoreApplication::processEvents();
            }
            const qint64 refTook = timer.nsecsElapsed();
            QCOMPARE(refWindow.panePaints.paints, before + 1);
            if (counted) {
                refNs.append(refTook);
                refFrames += refWindow.framePaints.paints - framesBefore;
            }

            before = paneWindow.panePaints.paints;
            framesBefore = paneWindow.framePaints.paints;
            timer.start();
            rig.pane.setTrace(trace);
            while (paneWindow.panePaints.paints == before && timer.elapsed() < 2000) {
                QCoreApplication::processEvents();
            }
            const qint64 paneTook = timer.nsecsElapsed();
            QCOMPARE(paneWindow.panePaints.paints, before + 1);
            if (counted) {
                paneNs.append(paneTook);
                paneFrames += paneWindow.framePaints.paints - framesBefore;
            }
        }
        qInfo().noquote() << QStringLiteral("%1x%2 in a window at ratio 1, %3 ticks")
                                 .arg(rig.pane.width()).arg(rig.pane.height()).arg(paints);
        qInfo().noquote() << "  reference:" << statsLine(statsOf(refNs))
                          << QStringLiteral("; frames behind it repainted %1 times").arg(refFrames);
        qInfo().noquote() << "  pane:     " << statsLine(statsOf(paneNs))
                          << QStringLiteral("; frames behind it repainted %1 times").arg(paneFrames);
        // The reference, not opaque, has both frames behind it repainted
        // on every tick; the pane has none.
        QCOMPARE(refFrames, 2 * paints);
        QCOMPARE(paneFrames, 0);

        // The panes belong to the rig, not to the windows.
        rig.ref.setParent(nullptr);
        rig.pane.setParent(nullptr);
    }
};

QTEST_MAIN(TstBandwidthFilterPanePaint)
#include "tst_bandwidth_filter_pane_paint.moc"
