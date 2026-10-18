// no-port-check: NereusSDR-original. Display top and legal marker are separate.
// Modification history (NereusSDR):
// 2026-10-12 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include <QtTest>
#include <QJsonArray>
#include <QLineF>
#include <QPainter>
#include <QImage>
#include "gui/meters/presets/CompositePresetItem.h"
#include "gui/meters/MeterPoller.h"
using namespace NereusSDR;
class TstCompositeMainPowerScale : public QObject {
    Q_OBJECT
private slots:
    void displayScalePreservesConfigCurveAndOverrange_data()
    {
        QTest::addColumn<int>("faceType");
        QTest::addColumn<int>("powerIndex");
        QTest::addColumn<double>("radioEndpointW");
        QTest::addColumn<QPointF>("endpoint");
        QTest::newRow("PowerSwr") << int(CompositePresetItem::Face::PowerSwr) << 0 << 120.0 << QPointF(.99,0);
        QTest::newRow("Cross") << int(CompositePresetItem::Face::Cross) << 0 << 100.0 << QPointF(.662,.083);
        QTest::newRow("Anan") << int(CompositePresetItem::Face::Anan) << 3 << 150.0 << QPointF(.899,.352);
    }
    void displayScalePreservesConfigCurveAndOverrange()
    {
        QFETCH(int, faceType);
        QFETCH(int, powerIndex);
        QFETCH(double, radioEndpointW);
        QFETCH(QPointF, endpoint);
        CompositePresetItem face(static_cast<CompositePresetItem::Face>(faceType));
        QJsonArray channels = face.configuration()["channels"].toArray();
        QJsonObject power = channels[powerIndex].toObject();
        power["attack"] = 1.0;
        power["decay"] = 1.0;
        power["color"] = QStringLiteral("#ff55aaff");
        power["name"] = QStringLiteral("Custom power");
        channels[powerIndex] = power;
        QVERIFY(face.applyConfiguration({{"channels",channels}}));
        face.setPowerScale(100);
        const QString saved = face.serialize();
        const QPointF original = face.calibratedPoint(powerIndex, radioEndpointW);
        QVERIFY(QLineF(original,endpoint).length()<1e-8);
        const QPointF reverseBefore = faceType==int(CompositePresetItem::Face::Cross)
            ? face.calibratedPoint(1, 4.0) : QPointF();
        face.setPowerScale(1500,2000.0);
        QCOMPARE(face.legalMarkerW(),1500.0);
        QVERIFY(QLineF(face.calibratedPoint(powerIndex,2000.0),endpoint).length()<1e-8);
        QVERIFY(QLineF(face.calibratedPoint(powerIndex,1500.0),endpoint).length()>1e-3);
        QCOMPARE(face.serialize(),saved);
        if (faceType==int(CompositePresetItem::Face::Cross)) {
            QCOMPARE(face.calibratedPoint(1,4.0),reverseBefore);
        }
        face.resetForTxTransition(true);
        face.pushBindingValue(MeterBinding::TxPower,2300.0);
        face.advanceMeter(0);
        QVERIFY(face.channelHasReading(powerIndex));
        QCOMPARE(face.channelValue(powerIndex),2300.0);
        const auto render = [&face] {
            QImage image(720,420,QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            face.paintForLayer(painter,720,420,MeterItem::Layer::Background);
            face.paintForLayer(painter,720,420,MeterItem::Layer::OverlayDynamic);
            painter.end();
            return image;
        };
        const QImage marker1500 = render();
        // A lower visual warning changes no normalization or reported value.
        const QPointF half = face.calibratedPoint(powerIndex,1000.0);
        face.setPowerScale(1200,2000.0);
        QCOMPARE(face.legalMarkerW(),1200.0);
        QVERIFY(marker1500 != render());  // same top/config/reading, moved visual marker
        QCOMPARE(face.calibratedPoint(powerIndex,1000.0),half);
        QCOMPARE(face.channelValue(powerIndex),2300.0);
        face.setPowerScale(100);
        QCOMPARE(face.legalMarkerW(),0.0);
        QCOMPARE(face.calibratedPoint(powerIndex,radioEndpointW),original);
        QCOMPARE(face.serialize(),saved);
        CompositePresetItem restored(static_cast<CompositePresetItem::Face>(faceType));
        QVERIFY(restored.deserialize(saved));
        QCOMPARE(restored.serialize(),saved);
    }
};
QTEST_MAIN(TstCompositeMainPowerScale)
#include "tst_composite_main_power_scale.moc"
