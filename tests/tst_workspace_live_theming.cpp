// no-port-check: NereusSDR-original test for the workspace preview's live
// scoped pass (WorkspacePreview::installLiveTheming, installed by
// applyWorkspacePalette).
//
// With the preview switch on, every widget gets the scoped pass when it is
// shown, with no call site: a Setup page and tab realized after the dialog
// is open, a dialog that never asks for it, a kPopupMenu menu (also one
// opened from a skipped widget), a label added to an open dialog and a
// sheet set later. With the switch off nothing changes.
//
// Also the preview's table grid: FreeDV Reporter's QTableView (no
// stylesheet of its own; a plain table stands in for it here, since the
// dialog needs a reporter client) drew its grid lines in the cell
// background's colour. Grid lines and header section edges take the
// workspace roles Longpath uses for them.
//
// Modification history (NereusSDR):
//   2026-10-08 - Written for the workspace visual review. J.J. Boyd
//                (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-12 - Text-floor regressions, J.J. Boyd (KG4VCF),
//                AI-assisted via OpenAI Codex.

#include <QtTest/QtTest>

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QHeaderView>
#include <QFontMetricsF>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextFragment>
#include <QMenu>
#include <QStandardItemModel>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>

#include "gui/DiversityDialog.h"
#include "gui/NetworkDiagnosticsDialog.h"
#include "gui/SetupDialog.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/PopupMenuStyle.h"
#include "gui/styles/QssTranslator.h"
#include "gui/styles/WorkspacePalette.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/styles/WorkspaceStyle.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

// The classes the scoped pass leaves alone (WorkspacePreview.cpp).
constexpr const char* kSkipped[] = {
    "NereusSDR::VfoLevelBar",       "NereusSDR::SpectrumWidget",
    "NereusSDR::ColorSwatchButton", "NereusSDR::GradientPickerWidget",
};

bool skippedWidget(const QWidget* w)
{
    if (w->property(WorkspacePreview::kSkipProperty).toBool()) {
        return true;
    }
    for (const char* cls : kSkipped) {
        if (w->inherits(cls)) {
            return true;
        }
    }
    return false;
}

QString describe(const QWidget* w)
{
    return QStringLiteral("%1 '%2'").arg(QLatin1String(w->metaObject()->className()),
                                         w->objectName());
}

struct PassReport {
    // Shown widgets with a stylesheet the pass has not handled.
    QStringList unthemed;
    // Widgets whose sheet held a legacy literal the pass replaced.
    int translated = 0;
};

bool holdsLegacyLiteral(const QString& qss)
{
    const QString lower = qss.toLower();
    for (const QssTranslator::Rule& rule : QssTranslator::defaultTable()) {
        if (lower.contains(rule.legacyHex.toLower())) {
            return true;
        }
    }
    return false;
}

void walk(QWidget* w, PassReport& report)
{
    if (!w->isVisible() || skippedWidget(w)) {
        return;
    }
    if (!w->styleSheet().isEmpty()) {
        const QVariant themed = w->property(WorkspacePreview::kThemedQssProperty);
        if (!themed.isValid() || themed.toString() != w->styleSheet()) {
            report.unthemed.append(describe(w));
        }
        const QString original = w->property(WorkspacePreview::kOriginalQssProperty).toString();
        if (holdsLegacyLiteral(original) && original != w->styleSheet()) {
            ++report.translated;
        }
    }
    for (QObject* o : w->children()) {
        if (o->isWidgetType() && !static_cast<QWidget*>(o)->isWindow()) {
            walk(static_cast<QWidget*>(o), report);
        }
    }
}

PassReport passReport(QWidget* root)
{
    PassReport report;
    walk(root, report);
    return report;
}

// Every widget of the application that the pass has marked.
int markedWidgets()
{
    int marked = 0;
    for (QWidget* w : QApplication::allWidgets()) {
        if (w->property(WorkspacePreview::kThemedQssProperty).isValid()
            || w->property(WorkspacePreview::kOriginalQssProperty).isValid()) {
            ++marked;
        }
    }
    return marked;
}


struct TableColours {
    QColor grid;
    QColor cell;
    QColor headerEdge;
    QColor headerFill;
};

// A table like FreeDV Reporter's: grid on, no stylesheet, a horizontal
// header with three sections, shown and grabbed at its own size.
TableColours tableColours()
{
    QStandardItemModel model(3, 3);
    QTableView table;
    table.setModel(&model);
    table.setShowGrid(true);
    table.verticalHeader()->setVisible(false);
    table.resize(320, 160);
    table.show();
    QCoreApplication::processEvents();

    const QImage view = table.viewport()->grab().toImage();
    const int gridX = table.columnViewportPosition(0) + table.columnWidth(0) - 1;
    const int midY = table.rowViewportPosition(0) + table.rowHeight(0) / 2;
    QHeaderView* header = table.horizontalHeader();
    const QImage head = header->grab().toImage();
    const int edgeX = header->sectionViewportPosition(0) + header->sectionSize(0) - 1;
    TableColours c;
    c.grid = view.pixelColor(gridX, midY);
    // Right of the line: the cell left of it is the current index.
    c.cell = view.pixelColor(gridX + 3, midY);
    c.headerEdge = head.pixelColor(edgeX, head.height() / 2);
    c.headerFill = head.pixelColor(edgeX + 3, head.height() / 2);
    return c;
}

QColor roleColour(const char* role)
{
    return Workspace::WorkspacePalette::glassAndDepth().color(QString::fromLatin1(role));
}

constexpr auto kLegacyPanel = "background: #0f0f1a;";
constexpr auto kLegacyLabel = "QLabel { color: #c8d8e8; background: #abcdef; }";
constexpr auto kLegacyLater = "QLabel { color: #8090a0; }";

} // namespace

class TestWorkspaceLiveTheming : public QObject
{
    Q_OBJECT

private slots:
    // Runs first, before the switch is ever on: nothing is translated.
    void switchOffChangesNothing()
    {
        WorkspacePreview::setEnabledForTest(false);
        RadioModel model;

        SetupDialog setup(&model);
        setup.show();
        setup.selectPage(QStringLiteral("NR/ANF"));
        QWidget* page = setup.realizedPageForTest(QStringLiteral("NR/ANF"));
        QVERIFY(page != nullptr);
        auto* tabs = page->findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        tabs->setCurrentIndex(tabs->count() - 1);

        DiversityDialog diversity(&model);
        diversity.show();

        QWidget host;
        host.setProperty(WorkspacePreview::kSkipProperty, true);
        QMenu menu(&host);
        menu.setStyleSheet(QString::fromLatin1(kPopupMenu));
        menu.addAction(QStringLiteral("Ant 1"));
        menu.popup(QPoint(10, 10));
        QVERIFY(menu.isVisible());

        QDialog plain;
        plain.setStyleSheet(QString::fromLatin1(kLegacyPanel));
        auto* label = new QLabel(QStringLiteral("added"), &plain);
        label->setStyleSheet(QString::fromLatin1(kLegacyLabel));
        plain.show();
        label->setStyleSheet(QString::fromLatin1(kLegacyLater));
        QCoreApplication::processEvents();

        QCOMPARE(menu.styleSheet(), QString::fromLatin1(kPopupMenu));
        QCOMPARE(plain.styleSheet(), QString::fromLatin1(kLegacyPanel));
        QCOMPARE(label->styleSheet(), QString::fromLatin1(kLegacyLater));
        QCOMPARE(markedWidgets(), 0);
        menu.hide();
    }

    // The switch off: the table draws as main.cpp's dark theme draws it
    // (applyDarkPalette and applyAppBaselineQss), and no table rule of the
    // preview is in the application stylesheet.
    void tableGridUnchangedWithTheSwitchOff()
    {
        WorkspacePreview::setEnabledForTest(false);
        applyDarkPalette(*qApp);
        applyAppBaselineQss(*qApp);
        const TableColours c = tableColours();
        qInfo("switch off: grid %s cell %s header edge %s header fill %s",
              qPrintable(c.grid.name()), qPrintable(c.cell.name()),
              qPrintable(c.headerEdge.name()), qPrintable(c.headerFill.name()));
        // Fusion's grid: Window darker 120, clear of the dark theme's cells.
        QCOMPARE(c.grid.name(), qApp->palette().color(QPalette::Window).darker(120).name());
        QVERIFY(c.grid != c.cell);
        QVERIFY(!qApp->styleSheet().contains(QLatin1String("gridline-color")));
        QVERIFY(!qApp->styleSheet().contains(QLatin1String("QHeaderView::section")));
    }

    void setupPagesAndTabsRealizedLaterAreThemed()
    {
        turnOn();
        RadioModel model;
        SetupDialog setup(&model);
        setup.show();
        QVERIFY(!setup.isPageRealizedForTest(QStringLiteral("NR/ANF")));

        setup.selectPage(QStringLiteral("NR/ANF"));
        QWidget* page = setup.realizedPageForTest(QStringLiteral("NR/ANF"));
        QVERIFY(page != nullptr);
        QVERIFY(page->isVisible());
        PassReport first = passReport(page);
        QVERIFY2(first.unthemed.isEmpty(), qPrintable(first.unthemed.join(QLatin1String(", "))));
        QVERIFY(first.translated > 0);

        auto* tabs = page->findChild<QTabWidget*>();
        QVERIFY(tabs != nullptr);
        QVERIFY(tabs->count() > 1);
        tabs->setCurrentIndex(tabs->count() - 1);
        QWidget* lastTab = tabs->currentWidget();
        QVERIFY(lastTab->isVisible());
        PassReport later = passReport(lastTab);
        QVERIFY2(later.unthemed.isEmpty(), qPrintable(later.unthemed.join(QLatin1String(", "))));
        QVERIFY(later.translated > 0);
    }

    void dialogsWithoutACallSiteAreThemed()
    {
        turnOn();
        RadioModel model;

        DiversityDialog diversity(&model);
        diversity.show();
        PassReport d = passReport(&diversity);
        QVERIFY2(d.unthemed.isEmpty(), qPrintable(d.unthemed.join(QLatin1String(", "))));
        QVERIFY(d.translated > 0);

        // Offline: no connection and no audio engine; it only reads stats.
        NetworkDiagnosticsDialog network(&model, nullptr);
        network.show();
        PassReport n = passReport(&network);
        QVERIFY2(n.unthemed.isEmpty(), qPrintable(n.unthemed.join(QLatin1String(", "))));
        QVERIFY(n.translated > 0);

        QDialog plain;
        plain.setStyleSheet(QString::fromLatin1(kLegacyPanel));
        plain.show();
        QCOMPARE(plain.styleSheet(), QStringLiteral("background: #08080a;"));
    }

    void popupMenuIsThemedAlsoFromASkippedWidget()
    {
        turnOn();
        QWidget host;
        host.setProperty(WorkspacePreview::kSkipProperty, true);
        QMenu menu(&host);
        menu.setStyleSheet(QString::fromLatin1(kPopupMenu));
        menu.addAction(QStringLiteral("Ant 1"));
        menu.popup(QPoint(10, 10));
        QVERIFY(menu.isVisible());

        const QString sheet = menu.styleSheet();
        QVERIFY2(!sheet.contains(QLatin1String("#1a2a3a"), Qt::CaseInsensitive), qPrintable(sheet));
        QVERIFY2(!sheet.contains(QLatin1String("#304050"), Qt::CaseInsensitive), qPrintable(sheet));
        QCOMPARE(menu.property(WorkspacePreview::kOriginalQssProperty).toString(),
                 QString::fromLatin1(kPopupMenu));
        menu.hide();
    }

    void addedAndRestyledWidgetsAreThemedOnce()
    {
        turnOn();
        WorkspacePreview::resetUnknownLiteralCounts();
        QDialog plain;
        auto* layout = new QVBoxLayout(&plain);
        plain.show();

        // Added to a dialog that is already open.
        auto* label = new QLabel(QStringLiteral("added"));
        label->setStyleSheet(QString::fromLatin1(kLegacyLabel));
        layout->addWidget(label);
        QTRY_VERIFY(label->isVisible());
        QCOMPARE(label->styleSheet(),
                 QStringLiteral("QLabel { color: #dcdce1; background: #abcdef; }"));
        QCOMPARE(WorkspacePreview::unknownLiteralCounts().value(QStringLiteral("#abcdef")), 1);

        // Shown again: not translated again.
        plain.hide();
        plain.show();
        QCOMPARE(WorkspacePreview::unknownLiteralCounts().value(QStringLiteral("#abcdef")), 1);
        QCOMPARE(label->property(WorkspacePreview::kOriginalQssProperty).toString(),
                 QString::fromLatin1(kLegacyLabel));

        // A new sheet set while shown is themed, once.
        label->setStyleSheet(QString::fromLatin1(kLegacyLater));
        QVERIFY(label->styleSheet() != QString::fromLatin1(kLegacyLater));
        QCOMPARE(label->property(WorkspacePreview::kOriginalQssProperty).toString(),
                 QString::fromLatin1(kLegacyLater));
        const QString themed = label->styleSheet();
        QCOMPARE(WorkspacePreview::lookQss(QString::fromLatin1(kLegacyLater), label), themed);
    }

    // The switch on: grid lines in border-subtle (Longpath tableStyle's
    // gridline-color) and the header's section edges in border, both clear
    // of the cell and header fill. Fusion drew the grid in Window darker
    // 120: #070708 on cells of #050507.
    void tableGridAndHeaderEdgesReadInThePreview()
    {
        turnOn();
        const TableColours c = tableColours();
        qInfo("switch on: grid %s cell %s header edge %s header fill %s",
              qPrintable(c.grid.name()), qPrintable(c.cell.name()),
              qPrintable(c.headerEdge.name()), qPrintable(c.headerFill.name()));
        QVERIFY(c.grid != c.cell);
        QCOMPARE(c.grid.name(), roleColour("border-subtle").name());
        QVERIFY(c.headerEdge != c.headerFill);
        QCOMPARE(c.headerEdge.name(), roleColour("border").name());
    }

    void readableFontsPreserveSizeAndAttributes()
    {
        WorkspacePreview::setEnabledForTest(true);
        QFont small(QStringLiteral("monospace"));
        small.setPixelSize(9);
        small.setBold(true);
        small.setItalic(true);
        QFont expected = small;
        expected.setPixelSize(11);
        QCOMPARE(WorkspacePreview::readableFont(small), expected);
        QCOMPARE(WorkspacePreview::readableTextPixelSize(9), 11);
        for (int size : {11, 12, 18, 24}) {
            QFont larger = small;
            larger.setPixelSize(size);
            QCOMPARE(WorkspacePreview::readableFont(larger), larger);
            QCOMPARE(WorkspacePreview::readableTextPixelSize(size), size);
        }

        // Compare point sizes using the same logical DPI as Qt's metrics;
        // 10 logical pixels is small at any offscreen platform DPI.
        QFont points = small;
        const qreal dpi = QFontMetricsF(points).fontDpi();
        points.setPointSizeF(10.0 * 72.0 / dpi);
        expected = points;
        expected.setPixelSize(11);
        QCOMPARE(WorkspacePreview::readableFont(points), expected);
        points.setPointSizeF(12.0 * 72.0 / dpi);
        QCOMPARE(WorkspacePreview::readableFont(points), points);

        const QFont caps = Workspace::capsFont(small, 9);
        QCOMPARE(caps.pixelSize(), 11);
        QCOMPARE(caps.capitalization(), QFont::AllUppercase);
        WorkspacePreview::setEnabledForTest(false);
        const QFont classicCaps = Workspace::capsFont(small, 9);
        WorkspacePreview::setEnabledForTest(true);
        // QFont quantizes absolute spacing; preserve the factory's actual
        // stored value rather than comparing against unquantized arithmetic.
        QCOMPARE(caps.letterSpacing(), classicCaps.letterSpacing());
        QFont expectedCaps = classicCaps;
        expectedCaps.setPixelSize(11);
        QCOMPARE(caps, expectedCaps);
        const QFont mono = Workspace::monoFont(small, 9, QFont::Bold);
        QCOMPARE(mono.pixelSize(), 11);
        QVERIFY(mono.fixedPitch());
        QCOMPARE(mono.weight(), QFont::Bold);
        QCOMPARE(Workspace::monoFont(small, 18, QFont::Bold).pixelSize(), 18);

        WorkspacePreview::setEnabledForTest(false);
        QCOMPARE(Workspace::capsFont(small, 9).pixelSize(), 9);
        QCOMPARE(Workspace::monoFont(small, 9, QFont::Bold).pixelSize(), 9);
        QCOMPARE(WorkspacePreview::readableFont(small), small);
        QCOMPARE(WorkspacePreview::readableFont(points), points);
        QCOMPARE(WorkspacePreview::readableTextPixelSize(9), 9);
        const QString original = QStringLiteral("font-size: 9px; min-height: 9px;");
        QCOMPARE(WorkspacePreview::lookQss(original), original);
        QLabel classic;
        classic.setFont(small);
        classic.setStyleSheet(original);
        classic.show();
        QCoreApplication::processEvents();
        QCOMPARE(classic.styleSheet(), original);
        QCOMPARE(classic.font().pixelSize(), 9);
    }

    void readablePointFontsUseTheActualPaintDeviceDpi()
    {
        WorkspacePreview::setEnabledForTest(true);
        QImage at72(16, 16, QImage::Format_ARGB32_Premultiplied);
        QImage at96(16, 16, QImage::Format_ARGB32_Premultiplied);
        // dots per metre controls QImage's logical DPI; DPR controls only
        // physical backing scale and must not change this text threshold.
        at72.setDotsPerMeterX(qRound(72.0 / 0.0254));
        at72.setDotsPerMeterY(qRound(72.0 / 0.0254));
        at96.setDotsPerMeterX(qRound(96.0 / 0.0254));
        at96.setDotsPerMeterY(qRound(96.0 / 0.0254));
        QCOMPARE(QFontMetricsF(QFont(), &at72).fontDpi(), 72);
        QCOMPARE(QFontMetricsF(QFont(), &at96).fontDpi(), 96);
        QFont original(QStringLiteral("monospace"));
        original.setPointSizeF(9.0);
        original.setBold(true);
        original.setItalic(true);
        QCOMPARE(WorkspacePreview::readableFont(original, &at96), original);
        QFont expected = original;
        expected.setPixelSize(11);
        QCOMPARE(WorkspacePreview::readableFont(original, &at72), expected);
        at96.setDevicePixelRatio(2.0);
        QCOMPARE(WorkspacePreview::readableFont(original, &at96), original);
        WorkspacePreview::setEnabledForTest(false);
        QCOMPARE(WorkspacePreview::readableFont(original, &at72), original);
    }

    void liveFontsAndDynamicSheetsRespectTheFloor()
    {
        turnOn();
        QLabel label(QStringLiteral("small label"));
        QFont small = label.font();
        small.setPixelSize(8);
        label.setFont(small);
        label.show();
        QCoreApplication::processEvents();
        QCOMPARE(label.font().pixelSize(), 11);

        // A font change after the first Show must be handled even when
        // the stored stylesheet already matches; setFont must not recurse.
        small.setPixelSize(10);
        label.setFont(small);
        QCOMPARE(label.font().pixelSize(), 11);
        small.setPixelSize(18);
        label.setFont(small);
        QCOMPARE(label.font(), small);

        const QString original = QStringLiteral(
            "QLabel { font-size: 9px; min-height: 9px; padding: 2px; }");
        label.setStyleSheet(original);
        QCOMPARE(label.styleSheet(), QStringLiteral(
            "QLabel { font-size: 11px; min-height: 9px; padding: 2px; }"));
        QCOMPARE(label.font().pixelSize(), 11);
        QCOMPARE(label.property(WorkspacePreview::kOriginalQssProperty).toString(), original);
        label.setStyleSheet(QStringLiteral("QLabel { font-size: 15px; }"));
        QCOMPARE(label.styleSheet(), QStringLiteral("QLabel { font-size: 15px; }"));
        QCOMPARE(label.font().pixelSize(), 15);
        label.hide();
        label.setStyleSheet(QStringLiteral("QLabel { font-size: 7px; }"));
        label.show();
        QCOMPARE(label.styleSheet(), QStringLiteral("QLabel { font-size: 11px; }"));
        QCOMPARE(label.font().pixelSize(), 11);
    }

    void richLabelsFloorOnlyInlineSizesAndTrackDynamicText()
    {
        turnOn();
        const QString original = QStringLiteral(
            "<!-- <span style='font-size:8px'>comment</span> -->"
            "<span title='style=font-size:8px' style=\"color:#123456; font-size:9px;"
            "background:#abcdef; font-family:monospace\">badge</span>"
            "<span style='font-size:15px'>large</span> font-size:8px prose"
            " &lt;span style='font-size:8px'&gt;code&lt;/span&gt;");
        QString expected = original;
        expected.replace(QStringLiteral("font-size:9px"), QStringLiteral("font-size:11px"));
        QLabel label;
        label.setTextFormat(Qt::RichText);
        label.setText(original);
        label.show();
        QCOMPARE(label.text(), expected);
        label.setText(QStringLiteral("<b style='font-size:8px;color:red'>later</b>"));
        label.grab();
        QCOMPARE(label.text(), QStringLiteral("<b style='font-size:11px;color:red'>later</b>"));
        const QString floored = label.text();
        label.grab();
        QCOMPARE(label.text(), floored);
        label.hide();
        label.setText(QStringLiteral("<span style='font-size:10px'>hidden</span>"));
        label.show();
        QCOMPARE(label.text(), QStringLiteral("<span style='font-size:11px'>hidden</span>"));
        label.setTextFormat(Qt::PlainText);
        label.setText(original);
        label.grab();
        QCOMPARE(label.text(), original);
        WorkspacePreview::setEnabledForTest(false);
        QLabel classic;
        classic.setText(original);
        classic.show();
        classic.grab();
        QCOMPARE(classic.text(), original);
    }

    void readOnlyRichDocumentsFloorInitialAndReplacementHtml()
    {
        turnOn();
        QTextBrowser browser;
        browser.setHtml(QStringLiteral("<span style='font-size:9px;color:#123456'>small</span>"
                                       "<span style='font-size:15px'>large</span>"));
        browser.show();
        QTextBlock block = browser.document()->begin();
        QTextBlock::iterator it = block.begin();
        QCOMPARE(it.fragment().charFormat().font().pixelSize(), 11);
        QCOMPARE(it.fragment().charFormat().foreground().color(), QColor(QStringLiteral("#123456")));
        ++it;
        QCOMPARE(it.fragment().charFormat().font().pixelSize(), 15);
        browser.setHtml(QStringLiteral("<span style='font-size:8px'>later</span>"));
        QCOMPARE(browser.document()->begin().begin().fragment().charFormat().font().pixelSize(), 11);
        QCOMPARE(browser.toPlainText(), QStringLiteral("later"));
        browser.hide();
        browser.setHtml(QStringLiteral("<span style='font-size:10px'>hidden</span>"));
        browser.show();
        QCOMPARE(browser.document()->begin().begin().fragment().charFormat().font().pixelSize(), 11);
        WorkspacePreview::setEnabledForTest(false);
        browser.setHtml(QStringLiteral("<span style='font-size:9px'>classic</span>"));
        QCOMPARE(browser.document()->begin().begin().fragment().charFormat().font().pixelSize(), 9);
    }

    void skippedUserColourSurfacesGetOnlyTypography()
    {
        turnOn();
        QWidget swatch;
        swatch.setProperty(WorkspacePreview::kSkipProperty, true);
        const QString original = QStringLiteral(
            "background:#123456; color:#abcdef; font-size:7px;"
            "min-height:7px; image:url(:/icons/spin-up.svg);");
        swatch.setStyleSheet(original);
        QLabel child(QStringLiteral("<span style='font-size:8px;color:#123456'>swatch</span>"),
                     &swatch);
        swatch.show();
        QCoreApplication::processEvents();
        QString expected = original;
        expected.replace(QStringLiteral("font-size:7px"), QStringLiteral("font-size:11px"));
        QCOMPARE(swatch.styleSheet(), expected);
        QCOMPARE(child.font().pixelSize(), 11);
        QCOMPARE(child.text(), QStringLiteral("<span style='font-size:11px;color:#123456'>swatch</span>"));
        const QString large = QStringLiteral("background:#654321; font-size:15px;");
        swatch.setStyleSheet(large);
        QCOMPARE(swatch.styleSheet(), large);
        QCOMPARE(swatch.font().pixelSize(), 15);
        swatch.setStyleSheet(original);
        QCOMPARE(swatch.styleSheet(), expected);
        QVERIFY(!swatch.property(WorkspacePreview::kThemedQssProperty).isValid());
        WorkspacePreview::setEnabledForTest(false);
        QWidget classic;
        classic.setProperty(WorkspacePreview::kSkipProperty, true);
        classic.setStyleSheet(original);
        classic.show();
        QCOMPARE(classic.styleSheet(), original);
        QCOMPARE(classic.font().pixelSize(), 7);
    }

    void cleanupTestCase()
    {
        WorkspacePreview::setEnabledForTest(false);
    }

private:
    static void turnOn()
    {
        WorkspacePreview::setEnabledForTest(true);
        applyWorkspacePalette(*qApp);
    }
};

QTEST_MAIN(TestWorkspaceLiveTheming)
#include "tst_workspace_live_theming.moc"
