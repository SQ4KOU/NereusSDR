// no-port-check: NereusSDR-original test file.  All Thetis source cites for
// the underlying TransmitModel properties live in TransmitModel.h.
// =================================================================
// tests/tst_tx_applet_vox_relocated.cpp  (NereusSDR)
// =================================================================
//
// Phase 3M-3a-iii bench polish (2026-05-04) — TxApplet VOX row wiring.
//
// The full VOX row (button + threshold slider + DexpPeakMeter strip + Hold
// slider + 100 ms peak-meter poller + right-click → Setup) was relocated
// from PhoneCwApplet (Phone tab Control #10) to TxApplet's right pane
// directly under TUNE/MOX (Option B - full row).  Operators wanted the
// VOX engage surface next to MOX/TUNE on the right pane where they engage
// TX, not buried on the Phone tab.  This file pins the new TxApplet
// wiring contracts:
//
//   VOX [ON]   — bidirectional ↔ TransmitModel::voxEnabled
//                 (objectName "TxVoxButton").
//   VOX Th     — bidirectional ↔ TransmitModel::voxThresholdDb
//                 (objectName "TxVoxThresholdSlider", range -80..0 dB).
//   VOX Hold   — bidirectional ↔ TransmitModel::voxHangTimeMs
//                 (objectName "TxVoxHoldSlider", range 1..2000 ms).
//   Right-click on [VOX] → emit openSetupRequested("Transmit", "DEXP/VOX").
//   Peak meter — DexpPeakMeter strip under threshold slider
//                 (objectName "TxVoxPeakMeter").
//   Polling     — 100 ms QTimer drives the VOX peak meter (matches Thetis
//                 UpdateNoiseGate Task.Delay(100), console.cs:25347
//                 [v2.10.3.13]).
//
// Modification history (NereusSDR):
//   2026-10-12 - Check whole inset readout text at the approved workspace minimum.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
// =================================================================

#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QFontInfo>
#include <QFontMetrics>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QFile>
#include <QStyleFactory>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QTimer>

#include "core/AppSettings.h"
#include "gui/applets/TxApplet.h"
#include "gui/applets/PhoneCwApplet.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/widgets/DexpPeakMeter.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

namespace {
class ReadoutThemeScope {
public:
    explicit ReadoutThemeScope(bool workspace)
        : m_sheet(qApp->styleSheet()), m_palette(qApp->palette())
    {
        WorkspacePreview::setEnabledForTest(workspace);
        applyDarkPalette(*qApp);
        applyAppBaselineQss(*qApp);
        if (workspace) {
            applyWorkspacePalette(*qApp);
            WorkspacePreview::installLiveTheming(*qApp);
        }
    }
    ~ReadoutThemeScope()
    {
        WorkspacePreview::setEnabledForTest(false);
        qApp->setStyleSheet(m_sheet);
        qApp->setPalette(m_palette);
    }
private:
    QString m_sheet;
    QPalette m_palette;
};

QLabel* labelWithText(QWidget* root, const QString& text)
{
    for (QLabel* label : root->findChildren<QLabel*>()) {
        if (label->text() == text) {
            return label;
        }
    }
    return nullptr;
}

void settleReadout(QWidget* panel)
{
    panel->setFixedWidth(260);
    panel->adjustSize();
    panel->show();
    WorkspacePreview::themeSubtree(panel);
    for (int pass = 0; pass < 4; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents();
        panel->adjustSize();
    }
}

QImage readoutImage(QLabel* actual, const QString& text, int width, int dpr)
{
    QLabel copy;
    copy.setFont(actual->font());
    copy.setPalette(actual->palette());
    copy.setStyleSheet(actual->styleSheet());
    copy.setAlignment(actual->alignment());
    copy.setMargin(actual->margin());
    copy.setIndent(actual->indent());
    copy.setText(text);
    copy.setFixedSize(width, actual->height());
    copy.ensurePolished();
    QImage image(copy.size() * dpr, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    copy.render(&image);
    return image;
}

int readoutInkPixels(QLabel* actual, const QString& text, int width, int dpr)
{
    const QImage words = readoutImage(actual, text, width, dpr);
    const QImage empty = readoutImage(actual, QString(), width, dpr);
    int count = 0;
    for (int y = 0; y < words.height(); ++y) {
        for (int x = 0; x < words.width(); ++x) {
            if (words.pixel(x, y) != empty.pixel(x, y)) {
                ++count;
            }
        }
    }
    return count;
}

QStringList readoutRangeFailures(QLabel* label, QSlider* slider, const QString& suffix)
{
    const QFontMetrics metrics(label->font(), label);
    int widest = 0;
    QString widestText;
    for (int value = slider->minimum(); value <= slider->maximum(); ++value) {
        const QString text = QString::number(value) + suffix;
        if (metrics.horizontalAdvance(text) > widest) {
            widest = metrics.horizontalAdvance(text);
            widestText = text;
        }
    }
    QStringList failures;
    // Style::insetValueStyle: 2px horizontal padding and 1px border
    // on each side. The independent raster comparison below checks
    // whole ink rather than relying solely on advance metrics.
    if (widest + 6 > label->width()) {
        failures << QStringLiteral("%1 needs %2px, field is %3px")
            .arg(widestText).arg(widest + 6).arg(label->width());
    }
    for (int dpr : {1, 2}) {
        // Centered text can land on half pixels. Keep the reference box's
        // parity so its antialias phase matches the actual box at DPR 1.
        const int referenceWidth = 200 + label->width() % 2;
        const int fullInk = readoutInkPixels(label, widestText, referenceWidth, dpr);
        const int actualInk = readoutInkPixels(label, widestText, label->width(), dpr);
        if (actualInk != fullInk) {
            failures << QStringLiteral("%1 @%2x ink %3, uncut %4")
                .arg(widestText).arg(dpr).arg(actualInk).arg(fullInk);
        }
    }
    return failures;
}

void captureReadoutPanel(QWidget* panel, const QString& name,
                         const QList<QLabel*>& labels)
{
    const QString destination = qEnvironmentVariable("NEREUS_READOUT_CAPTURE_DIR");
    if (destination.isEmpty()) {
        return;
    }
    QDir().mkpath(destination);
    for (int dpr : {1, 2}) {
        QImage image(panel->size() * dpr, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        panel->render(&image);
        const QString stem = destination + QLatin1Char('/') + name
            + QStringLiteral("-%1x").arg(dpr);
        QVERIFY(image.save(stem + QStringLiteral(".png")));
        QJsonArray fields;
        for (QLabel* label : labels) {
            fields.append(QJsonObject{
                {QStringLiteral("text"), label->text()},
                {QStringLiteral("width"), label->width()},
                {QStringLiteral("height"), label->height()},
                {QStringLiteral("resolved_px"), QFontInfo(label->font()).pixelSize()},
                {QStringLiteral("font"), label->font().toString()},
                {QStringLiteral("sheet"), label->styleSheet()},
            });
        }
        QFile sidecar(stem + QStringLiteral(".json"));
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        sidecar.write(QJsonDocument(QJsonObject{
            {QStringLiteral("dpr"), dpr}, {QStringLiteral("panel_width"), panel->width()},
            {QStringLiteral("fields"), fields},
            {QStringLiteral("display_only"), true},
        }).toJson());
    }
}
} // namespace

class TestTxAppletVoxRelocated : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (!qApp) {
            static int argc = 0;
            new QApplication(argc, nullptr);
        }
        AppSettings::instance().clear();
    }

    void cleanup()
    {
        AppSettings::instance().clear();
    }

    // ── 1. VOX [ON] toggle bidirectional with TM::voxEnabled ──────────────
    void voxButton_bidirectionalWithTm()
    {
        RadioModel rm;
        TxApplet applet(&rm);

        auto* btn = applet.findChild<QPushButton*>(QStringLiteral("TxVoxButton"));
        QVERIFY2(btn != nullptr, "VOX button not found on TxApplet (relocation regression?)");
        QVERIFY(btn->isCheckable());

        // Default voxEnabled is false (TransmitModel.h:583-591 [v2.10.3.13]).
        QCOMPARE(rm.transmitModel().voxEnabled(), false);
        QCOMPARE(btn->isChecked(), false);

        // UI → Model
        btn->click();
        QCOMPARE(rm.transmitModel().voxEnabled(), true);
        QCOMPARE(btn->isChecked(), true);

        btn->click();
        QCOMPARE(rm.transmitModel().voxEnabled(), false);
        QCOMPARE(btn->isChecked(), false);

        // Model → UI
        rm.transmitModel().setVoxEnabled(true);
        QCOMPARE(btn->isChecked(), true);

        rm.transmitModel().setVoxEnabled(false);
        QCOMPARE(btn->isChecked(), false);
    }

    // ── 2. VOX threshold slider bidirectional with TM::voxThresholdDb ────
    void voxThresholdSlider_bidirectionalWithTm()
    {
        RadioModel rm;
        TxApplet applet(&rm);

        auto* sl = applet.findChild<QSlider*>(QStringLiteral("TxVoxThresholdSlider"));
        QVERIFY2(sl != nullptr, "VOX threshold slider not found on TxApplet (relocation regression?)");

        // Range -80..0 from console.Designer.cs:6018-6019 [v2.10.3.13].
        QCOMPARE(sl->minimum(), -80);
        QCOMPARE(sl->maximum(),   0);

        // UI → Model
        sl->setValue(-30);
        QCOMPARE(rm.transmitModel().voxThresholdDb(), -30);

        sl->setValue(0);
        QCOMPARE(rm.transmitModel().voxThresholdDb(), 0);

        sl->setValue(-80);
        QCOMPARE(rm.transmitModel().voxThresholdDb(), -80);

        // Model → UI
        rm.transmitModel().setVoxThresholdDb(-25);
        QCOMPARE(sl->value(), -25);

        rm.transmitModel().setVoxThresholdDb(-60);
        QCOMPARE(sl->value(), -60);
    }

    // ── 3. VOX hold slider bidirectional with TM::voxHangTimeMs ──────────
    void voxHoldSlider_bidirectionalWithTm()
    {
        RadioModel rm;
        TxApplet applet(&rm);

        auto* sl = applet.findChild<QSlider*>(QStringLiteral("TxVoxHoldSlider"));
        QVERIFY2(sl != nullptr, "VOX hold slider not found on TxApplet (relocation regression?)");

        // Range 1..2000 from setup.designer.cs:45005-45013 [v2.10.3.13].
        QCOMPARE(sl->minimum(),    1);
        QCOMPARE(sl->maximum(), 2000);

        // UI → Model
        sl->setValue(750);
        QCOMPARE(rm.transmitModel().voxHangTimeMs(), 750);

        sl->setValue(1);
        QCOMPARE(rm.transmitModel().voxHangTimeMs(), 1);

        sl->setValue(2000);
        QCOMPARE(rm.transmitModel().voxHangTimeMs(), 2000);

        // Model → UI
        rm.transmitModel().setVoxHangTimeMs(1200);
        QCOMPARE(sl->value(), 1200);

        rm.transmitModel().setVoxHangTimeMs(500);
        QCOMPARE(sl->value(), 500);
    }

    // ── 4. Right-click on VOX button emits openSetupRequested("Transmit",
    //      "DEXP/VOX") ─────────────────────────────────────────────────────
    void voxButton_rightClickEmitsOpenSetupRequested()
    {
        RadioModel rm;
        TxApplet applet(&rm);

        auto* btn = applet.findChild<QPushButton*>(QStringLiteral("TxVoxButton"));
        QVERIFY(btn != nullptr);
        QCOMPARE(btn->contextMenuPolicy(), Qt::CustomContextMenu);

        QSignalSpy spy(&applet, &TxApplet::openSetupRequested);
        QVERIFY(spy.isValid());

        // CustomContextMenu policy means right-click emits
        // customContextMenuRequested.  Emit directly to exercise the
        // applet's connected slot.
        emit btn->customContextMenuRequested(QPoint(0, 0));

        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("Transmit"));
        QCOMPARE(args.at(1).toString(), QStringLiteral("DEXP/VOX"));
    }

    // ── 5. m_voxPeakMeter exists (not null) ──────────────────────────────
    void voxPeakMeter_existsAfterConstruction()
    {
        RadioModel rm;
        TxApplet applet(&rm);

        auto* voxMeter = applet.findChild<DexpPeakMeter*>(QStringLiteral("TxVoxPeakMeter"));
        QVERIFY2(voxMeter != nullptr, "VOX peak meter not created on TxApplet");
    }

    // ── 6. m_voxMeterTimer interval is 100 ms ────────────────────────────
    //      Matches Thetis UpdateNoiseGate Task.Delay(100) at
    //      console.cs:25347 [v2.10.3.13].
    void voxMeterTimer_intervalIs100ms()
    {
        RadioModel rm;
        TxApplet applet(&rm);

        // TxApplet now owns at least one QTimer (the VOX meter @ 100 ms;
        // there's also the existing 50 ms fwd-power refresh timer).  Walk
        // all QTimer children and confirm exactly one ticks at 100 ms.
        const auto timers = applet.findChildren<QTimer*>();
        bool found100 = false;
        for (QTimer* t : timers) {
            if (t->interval() == 100 && t->isActive()) {
                found100 = true;
                break;
            }
        }
        QVERIFY2(found100, "No active 100 ms QTimer found on TxApplet");
    }

    // ── 7. PhoneCwApplet must NOT have the VOX widgets anymore ──────────
    //      After the 2026-05-04 relocation, the PhoneCw* VOX object names
    //      should not appear on TxApplet (those are PhoneCw-namespaced).
    //      The TxVoxButton / TxVoxThresholdSlider / TxVoxHoldSlider /
    //      TxVoxPeakMeter checks above already pin the relocation; this
    //      explicit check just ensures we didn't accidentally put both
    //      sets on TxApplet during the move.
    void onlyTxVoxObjectNamesPresent()
    {
        RadioModel rm;
        TxApplet applet(&rm);

        QVERIFY2(applet.findChild<QPushButton*>(QStringLiteral("PhoneCwVoxButton")) == nullptr,
                 "PhoneCw VOX button should not be on TxApplet");
        QVERIFY2(applet.findChild<QSlider*>(QStringLiteral("PhoneCwVoxThresholdSlider")) == nullptr,
                 "PhoneCw VOX threshold slider should not be on TxApplet");
        QVERIFY2(applet.findChild<QSlider*>(QStringLiteral("PhoneCwVoxHoldSlider")) == nullptr,
                 "PhoneCw VOX hold slider should not be on TxApplet");
        QVERIFY2(applet.findChild<DexpPeakMeter*>(QStringLiteral("PhoneCwVoxPeakMeter")) == nullptr,
                 "PhoneCw VOX peak meter should not be on TxApplet");
    }
    // The full formatted ranges must fit the final 11px font and inset
    // border/padding, without clipping the minus sign or unit glyphs.
    void workspaceVoxAndDexpReadoutsFitTheirRanges()
    {
        ReadoutThemeScope theme(true);
        TxApplet tx(nullptr);
        PhoneCwApplet phone(nullptr);
        QLabel* threshold = labelWithText(&tx, QStringLiteral("-20 dB"));
        QLabel* hold = labelWithText(&tx, QStringLiteral("500 ms"));
        QLabel* dexp = labelWithText(&phone, QStringLiteral("-50 dB"));
        QVERIFY(threshold != nullptr);
        QVERIFY(hold != nullptr);
        QVERIFY(dexp != nullptr);
        settleReadout(&tx);
        settleReadout(&phone);
        threshold->setText(QStringLiteral("-40 dB"));
        dexp->setText(QStringLiteral("-160 dB"));
        captureReadoutPanel(&tx, QStringLiteral("workspace-vox"), {threshold, hold});
        captureReadoutPanel(&phone, QStringLiteral("workspace-dexp"), {dexp});
        hold->setText(QStringLiteral("2000 ms"));
        threshold->setText(QStringLiteral("-80 dB"));
        captureReadoutPanel(&tx, QStringLiteral("workspace-vox-range"), {threshold, hold});

        struct Field { QLabel* label; QSlider* slider; QString suffix; };
        const QList<Field> fields{
            {threshold, tx.findChild<QSlider*>(QStringLiteral("TxVoxThresholdSlider")), QStringLiteral(" dB")},
            {hold, tx.findChild<QSlider*>(QStringLiteral("TxVoxHoldSlider")), QStringLiteral(" ms")},
            {dexp, phone.findChild<QSlider*>(QStringLiteral("PhoneCwDexpThresholdSlider")), QStringLiteral(" dB")},
        };
        QStringList failures;
        for (const Field& field : fields) {
            QVERIFY(field.slider != nullptr);
            QVERIFY(QFontInfo(field.label->font()).pixelSize() >= 11);
            failures += readoutRangeFailures(field.label, field.slider, field.suffix);
        }
        qInfo().noquote() << failures.join(QLatin1Char('\n'));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));

        // A larger explicit font remains larger, and repeat presentation
        // events cannot feed the previous fixed width back into sizing.
        for (const Field& field : fields) {
            field.label->setStyleSheet(field.label->styleSheet()
                + QStringLiteral("QLabel { font-size: 14px; }"));
            WorkspacePreview::themeSubtree(field.label);
            QCOMPARE(QFontInfo(field.label->font()).pixelSize(), 14);
            const int fittedWidth = field.label->width();
            for (int repeat = 0; repeat < 4; ++repeat) {
                QEvent change(QEvent::FontChange);
                QCoreApplication::sendEvent(field.label, &change);
                QCOMPARE(field.label->width(), fittedWidth);
            }
            const QStringList largerFailures = readoutRangeFailures(field.label, field.slider, field.suffix);
            QVERIFY2(largerFailures.isEmpty(), qPrintable(largerFailures.join(QLatin1Char('\n'))));
        }
    }

    void classicVoxAndDexpReadoutsKeepTheirWidths()
    {
        ReadoutThemeScope theme(false);
        TxApplet tx(nullptr);
        PhoneCwApplet phone(nullptr);
        QLabel* threshold = labelWithText(&tx, QStringLiteral("-20 dB"));
        QLabel* hold = labelWithText(&tx, QStringLiteral("500 ms"));
        QLabel* dexp = labelWithText(&phone, QStringLiteral("-50 dB"));
        QVERIFY(threshold != nullptr);
        QVERIFY(hold != nullptr);
        QVERIFY(dexp != nullptr);
        settleReadout(&tx);
        settleReadout(&phone);
        QCOMPARE(threshold->width(), 38);
        QCOMPARE(hold->width(), 42);
        QCOMPARE(dexp->width(), 38);
        threshold->setText(QStringLiteral("-40 dB"));
        dexp->setText(QStringLiteral("-160 dB"));
        captureReadoutPanel(&tx, QStringLiteral("classic-vox"), {threshold, hold});
        captureReadoutPanel(&phone, QStringLiteral("classic-dexp"), {dexp});
    }

};

QTEST_MAIN(TestTxAppletVoxRelocated)
#include "tst_tx_applet_vox_relocated.moc"
