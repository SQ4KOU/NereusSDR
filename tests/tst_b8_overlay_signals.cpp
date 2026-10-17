// =================================================================
// tests/tst_b8_overlay_signals.cpp  (NereusSDR)
// =================================================================
//
// The Display flyout on the left strip drives its pan's SpectrumWidget.
//
// Originally (docs/superpowers/plans/2026-05-01-ui-polish-cross-surface.md
// §B8) the flyout reported each row through a signal of its own
// (wfColorGainChanged, wfBlackLevelChanged, colorSchemeChanged, ...) and
// MainWindow connected each to a setter. This file checked those signals.
//
// 2026-10-10 - Display pill plan Task 16: the flyout is the four-tab
// Display pill and edits its pan directly, so those signals are gone
// (spec 3.2). The cases are rewritten for what replaced them: the signals
// no longer exist, the pill's rows write the pan it was given, and the
// footer's two requests are the only signals left for the window to
// connect. The waterfall rows return with the Waterfall tab.
// J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//
// 2026-10-16 - The footer names the Setup Display pages as the tree shows
// them, and the FFT tab's opens FFT & Engine. J.J. Boyd (KG4VCF),
// AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>

#include "core/AppSettings.h"
#include "gui/SpectrumOverlayPanel.h"
#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

class TstB8OverlaySignals : public QObject {
    Q_OBJECT

private:
    using Tab = SpectrumOverlayPanel::DisplayPillTab;

    // The strip as MainWindow builds it: a child of the pan it serves.
    struct PanelHarness {
        SpectrumWidget pan;
        SpectrumOverlayPanel* panel{nullptr};
        PanelHarness() {
            panel = new SpectrumOverlayPanel(&pan);
            panel->openDisplayPill();
        }
        QPushButton* button(const char* name) {
            return pan.findChild<QPushButton*>(QString::fromLatin1(name));
        }
        QSlider* slider(const char* name) {
            return pan.findChild<QSlider*>(QString::fromLatin1(name));
        }
    };

private slots:

    void init() {
        AppSettings::instance().clear();
    }

    // ── 1. The per-row signals of the old flyout are gone ──────────────
    void theOldRowSignalsAreGone() {
        const QMetaObject* meta = &SpectrumOverlayPanel::staticMetaObject;
        const char* const gone[] = {
            "wfColorGainChanged(int)", "wfBlackLevelChanged(int)",
            "colorSchemeChanged(int)", "cursorFreqVisibleChanged(bool)",
            "gridVisibleChanged(bool)", "fillColorChanged(QColor)",
            "fillAlphaChanged(float)",
        };
        for (const char* signature : gone) {
            QVERIFY2(meta->indexOfSignal(signature) < 0, signature);
        }
        const char* const kept[] = {
            "openSetupRequested(QString)", "resetPanRequested()",
            "perBandDbMaxRequested(int)", "perBandDbMinRequested(int)",
        };
        for (const char* signature : kept) {
            QVERIFY2(meta->indexOfSignal(signature) >= 0, signature);
        }
    }

    // ── 2. The strip built on a pan edits that pan ─────────────────────
    void theStripEditsThePanItIsBuiltOn() {
        PanelHarness h;
        QCOMPARE(h.panel->displayTarget(), &h.pan);
        QVERIFY(h.panel->displayPillVisible());
    }

    // ── 3. Grid drives setGridEnabled ──────────────────────────────────
    void gridPillDrivesThePan() {
        PanelHarness h;
        QPushButton* grid = h.button("pillDisplayGridEnabled");
        QVERIFY(grid);
        QCOMPARE(grid->isChecked(), h.pan.gridEnabled());
        const bool before = h.pan.gridEnabled();
        grid->click();
        QCOMPARE(h.pan.gridEnabled(), !before);
        grid->click();
        QCOMPARE(h.pan.gridEnabled(), before);
    }

    // ── 4. Fill opacity drives setFillAlpha ────────────────────────────
    void fillOpacityDrivesThePan() {
        PanelHarness h;
        QSlider* alpha = h.slider("pillDisplayFftFillAlpha");
        QVERIFY(alpha);
        QCOMPARE(alpha->value(), qRound(h.pan.fillAlpha() * 100.0f));
        alpha->setValue(40);
        QCOMPARE(h.pan.fillAlpha(), 0.4f);
    }

    // ── 5. Cursor Freq drives setCursorFreqVisible ─────────────────────
    void cursorFreqPillDrivesThePan() {
        PanelHarness h;
        QPushButton* cursor = h.button("pillDisplayShowCursorFreq");
        QVERIFY(cursor);
        const bool before = h.pan.cursorFreqVisible();
        QCOMPARE(cursor->isChecked(), before);
        cursor->click();
        QCOMPARE(h.pan.cursorFreqVisible(), !before);
    }

    // ── 6. The fill colour button drives setFillColor ──────────────────
    void fillColourButtonDrivesThePan() {
        PanelHarness h;
        const QColor before = h.pan.fillColor();
        QColor offered;
        h.panel->setPillColorChooser([&offered](const QColor& initial, const QString&) {
            offered = initial;
            return QColor(QStringLiteral("#3366cc"));
        });
        QPushButton* swatch = h.button("pillDisplayFillColor");
        QVERIFY(swatch);
        swatch->click();
        QCOMPARE(offered, before);
        QCOMPARE(h.pan.fillColor(), QColor(QStringLiteral("#3366cc")));
    }

    // ── 7. The footer's requests are what the window connects ──────────
    void footerRequestsReachAConnectedSlot() {
        PanelHarness h;
        QStringList pages;
        int resets = 0;
        connect(h.panel, &SpectrumOverlayPanel::openSetupRequested,
                h.panel, [&pages](const QString& page) { pages << page; });
        connect(h.panel, &SpectrumOverlayPanel::resetPanRequested,
                h.panel, [&resets]() { ++resets; });

        const struct {
            Tab tab;
            const char* page;
        } tabs[] = {
            {Tab::Spectrum, "displayPageSpectrum"},
            {Tab::Fft, "displayPageFft"},
            {Tab::Waterfall, "displayPageWaterfall"},
            {Tab::View3D, "displayPage3D"},
        };
        for (const auto& tab : tabs) {
            h.panel->setDisplayPillTab(tab.tab);
            QWidget* page = h.pan.findChild<QWidget*>(QString::fromLatin1(tab.page));
            QVERIFY2(page, tab.page);
            page->findChild<QPushButton*>(QStringLiteral("pillSetupDefaults"))->click();
            page->findChild<QPushButton*>(QStringLiteral("pillResetPan"))->click();
        }
        QCOMPARE(pages, QStringList({QStringLiteral("Spectrum"),
                                     QStringLiteral("FFT & Engine"),
                                     QStringLiteral("Waterfall"),
                                     QStringLiteral("View")}));
        QCOMPARE(resets, 4);
    }
};

QTEST_MAIN(TstB8OverlaySignals)
#include "tst_b8_overlay_signals.moc"
