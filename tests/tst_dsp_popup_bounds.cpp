// no-port-check: NereusSDR-original regression for an oversized DSP popup.
// 2026-10-12: J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include <QtTest/QtTest>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include "gui/widgets/DspParamPopup.h"
#include "gui/widgets/DspPopupGeometry.h"

using namespace NereusSDR;

class TestDspPopupBounds : public QObject {
    Q_OBJECT
private slots:
    void negativeMonitorOriginAndOversizedGeometry_data()
    {
        QTest::addColumn<QPoint>("anchor");
        QTest::addColumn<QSize>("wanted");
        QTest::addColumn<QRect>("available");
        QTest::addColumn<QRect>("expected");
        QTest::newRow("lower-left-display") << QPoint(-1700, 1550) << QSize(600, 1400)
            << QRect(-1920, 700, 1920, 900) << QRect(-1700, 700, 600, 900);
        QTest::newRow("negative-origin-right-edge") << QPoint(-30, -50) << QSize(300, 200)
            << QRect(-1280, -800, 1280, 800) << QRect(-301, -250, 300, 200);
        QTest::newRow("too-wide-and-tall") << QPoint(900, 1400) << QSize(2000, 2000)
            << QRect(800, 600, 640, 480) << QRect(800, 600, 640, 480);
        QTest::newRow("fitting") << QPoint(-1800, 740) << QSize(300, 200)
            << QRect(-1920, 700, 1920, 900) << QRect(-1800, 740, 300, 200);
    }
    void negativeMonitorOriginAndOversizedGeometry()
    {
        QFETCH(QPoint, anchor);
        QFETCH(QSize, wanted);
        QFETCH(QRect, available);
        QFETCH(QRect, expected);
        QCOMPARE(DspPopupGeometry::boundedGeometry(anchor, wanted, available), expected);
    }
    void oversizedContentStaysOnScreenAndFooterIsReachable()
    {
        const QRect available = QApplication::primaryScreen()->availableGeometry();
        DspParamPopup popup;
        auto* content = new QWidget(&popup);
        auto* layout = new QVBoxLayout(content);
        auto* tall = new QLabel(QStringLiteral("All controls retained"), content);
        tall->setMinimumHeight(available.height() + 200);
        layout->addWidget(tall);
        auto* footer = new QPushButton(QStringLiteral("More settings"), content);
        layout->addWidget(footer);
        popup.addWidget(content);
        popup.showAt(available.bottomLeft() + QPoint(40, -20));
        QCoreApplication::processEvents();
        qInfo() << "available" << available << "popup" << popup.geometry();
        QVERIFY(available.contains(popup.geometry()));
        auto* scroll = popup.findChild<QScrollArea*>();
        QVERIFY(scroll);
        scroll->ensureWidgetVisible(footer);
        QCoreApplication::processEvents();
        QVERIFY(scroll->viewport()->rect().contains(footer->mapTo(scroll->viewport(), footer->rect().center())));
        QSignalSpy clicked(footer, &QPushButton::clicked);
        QTest::mouseClick(footer, Qt::LeftButton);
        QCOMPARE(clicked.count(), 1);
    }
    void expansionAfterOpeningRemainsContained()
    {
        const QRect available = QApplication::primaryScreen()->availableGeometry();
        DspParamPopup popup;
        auto* content = new QLabel(QStringLiteral("Expandable controls"), &popup);
        popup.addWidget(content);
        popup.showAt(available.bottomLeft() + QPoint(40, -40));
        content->setMinimumHeight(available.height() + 200);
        popup.adjustSize();
        QTRY_VERIFY(available.contains(popup.geometry()));
    }
    void scrollingKeepsExistingPopupBackground()
    {
        DspParamPopup popup;
        auto* scroll = popup.findChild<QScrollArea*>();
        QVERIFY(scroll);
        QVERIFY(!scroll->viewport()->autoFillBackground());
        QVERIFY(!scroll->widget()->autoFillBackground());
    }
    void fittingPopupPreservesAnchor()
    {
        const QRect available = QApplication::primaryScreen()->availableGeometry();
        DspParamPopup popup;
        popup.addCheckbox(QStringLiteral("A control"), false, nullptr);
        const QPoint anchor = available.topLeft() + QPoint(40, 40);
        popup.showAt(anchor);
        QCoreApplication::processEvents();
        QCOMPARE(popup.pos(), anchor);
        QVERIFY(available.contains(popup.geometry()));
    }
};
QTEST_MAIN(TestDspPopupBounds)
#include "tst_dsp_popup_bounds.moc"
