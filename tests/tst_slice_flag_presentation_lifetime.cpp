// =================================================================
// tests/tst_slice_flag_presentation_lifetime.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-native regression for secondary VFO flags removed during a
// multi-pan layout rehome. There is no upstream AetherSDR equivalent for
// this lifecycle boundary (no port check applies).
//
// Modification history (NereusSDR):
//   2026-09-21 -- Added by J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via OpenAI Codex.
// =================================================================

#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QTest>

#include "gui/SliceFlagPresentationBinding.h"
#include "gui/SpectrumWidget.h"
#include "gui/widgets/VfoWidget.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

QPushButton* buttonWithText(VfoWidget* flag, const QString& text)
{
    const QList<QPushButton*> buttons = flag->findChildren<QPushButton*>();
    for (QPushButton* button : buttons) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

QSlider* agcThresholdSlider(VfoWidget* flag)
{
    const QList<QSlider*> sliders = flag->findChildren<QSlider*>();
    for (QSlider* slider : sliders) {
        if (slider->minimum() == -160 && slider->maximum() == 0) {
            return slider;
        }
    }
    return nullptr;
}

QSlider* sliderWithTooltipPrefix(VfoWidget* flag, const QString& prefix)
{
    const QList<QSlider*> sliders = flag->findChildren<QSlider*>();
    for (QSlider* slider : sliders) {
        if (slider->toolTip().startsWith(prefix)) {
            return slider;
        }
    }
    return nullptr;
}

}  // namespace

class TestSliceFlagPresentationLifetime : public QObject {
    Q_OBJECT

private slots:
    void removingFlagDisconnectsEveryPresentationEdge()
    {
        SliceModel slice(1);
        SpectrumWidget spectrum;
        VfoWidget* flag = spectrum.addVfoWidget(slice.sliceIndex());
        QVERIFY(flag);

        const QList<QMetaObject::Connection> connections =
            wireSliceFlagPresentation(&slice, flag);
        QCOMPARE(connections.size(), 14);

        QPointer<VfoWidget> retiredFlag(flag);
        spectrum.removeVfoWidget(slice.sliceIndex());
        QVERIFY(retiredFlag.isNull());

        // QObject::disconnect(handle) returns false once Qt has already
        // retired the connection with its receiver. Check the operation,
        // not bool(handle): a stored handle can remain truthy after an
        // automatic disconnect. Consume every still-live handle before the
        // assertion so the RED test reports safely instead of dereferencing
        // the deleted flag on teardown or a later model update.
        bool allAlreadyDisconnected = true;
        for (const QMetaObject::Connection& connection : connections) {
            if (QObject::disconnect(connection)) {
                allAlreadyDisconnected = false;
            }
        }
        QVERIFY2(allAlreadyDisconnected,
                 "retired VFO flag left model presentation connections live");

        // Once a flag has gone, later mirrored/model updates must remain safe.
        slice.setRitEnabled(true);
        slice.setRitHz(125);
        slice.setXitEnabled(true);
        slice.setXitHz(-80);
        slice.setSnbEnabled(true);
        slice.setApfEnabled(true);
        slice.setApfTuneHz(950);
        slice.setMuted(true);
        slice.setAudioPan(0.75);
        slice.setSsqlEnabled(true);
        slice.setSsqlThresh(-82.0);
        slice.setAgcThreshold(-67);
        slice.setBinauralEnabled(true);
        slice.setLocked(true);

        // A layout rehome immediately creates a replacement flag for the
        // same live slice. Prove that the lifetime fix did not merely drop
        // the presentation path: representative boolean state and the exact
        // AGC-T slider from the crash still follow subsequent model updates.
        VfoWidget* replacement = spectrum.addVfoWidget(slice.sliceIndex());
        QVERIFY(replacement);
        const QList<QMetaObject::Connection> replacementConnections =
            wireSliceFlagPresentation(&slice, replacement);
        QCOMPARE(replacementConnections.size(), 14);

        slice.setRitEnabled(false);
        slice.setXitEnabled(false);
        slice.setSnbEnabled(false);
        slice.setApfEnabled(false);
        slice.setMuted(false);
        slice.setSsqlEnabled(false);
        slice.setBinauralEnabled(false);
        slice.setAudioPan(0.25);
        slice.setAgcThreshold(-91);

        QPushButton* ritButton = buttonWithText(replacement, QStringLiteral("RIT"));
        QPushButton* xitButton = buttonWithText(replacement, QStringLiteral("XIT"));
        QPushButton* snbButton = buttonWithText(replacement, QStringLiteral("SNB"));
        QPushButton* apfButton = buttonWithText(replacement, QStringLiteral("APF"));
        QPushButton* muteButton = buttonWithText(replacement, QStringLiteral("Mute"));
        QPushButton* sqlButton = buttonWithText(replacement, QStringLiteral("SQL"));
        QPushButton* binButton = buttonWithText(replacement, QStringLiteral("BIN"));
        QSlider* panSlider = sliderWithTooltipPrefix(
            replacement, QStringLiteral("Audio pan:"));
        QSlider* agcSlider = agcThresholdSlider(replacement);
        QVERIFY(ritButton);
        QVERIFY(xitButton);
        QVERIFY(snbButton);
        QVERIFY(apfButton);
        QVERIFY(muteButton);
        QVERIFY(sqlButton);
        QVERIFY(binButton);
        QVERIFY(panSlider);
        QVERIFY(agcSlider);
        QVERIFY(!ritButton->isChecked());
        QVERIFY(!xitButton->isChecked());
        QVERIFY(!snbButton->isChecked());
        QVERIFY(!apfButton->isChecked());
        QVERIFY(!muteButton->isChecked());
        QVERIFY(!sqlButton->isChecked());
        QVERIFY(!binButton->isChecked());
        QCOMPARE(panSlider->value(), 25);
        QCOMPARE(agcSlider->value(), -91);

        slice.setRitEnabled(true);
        slice.setXitEnabled(true);
        slice.setSnbEnabled(true);
        slice.setApfEnabled(true);
        slice.setMuted(true);
        slice.setSsqlEnabled(true);
        slice.setBinauralEnabled(true);
        slice.setAgcThreshold(-73);
        QVERIFY(ritButton->isChecked());
        QVERIFY(xitButton->isChecked());
        QVERIFY(snbButton->isChecked());
        QVERIFY(apfButton->isChecked());
        QVERIFY(muteButton->isChecked());
        QVERIFY(sqlButton->isChecked());
        QVERIFY(binButton->isChecked());
        QCOMPARE(agcSlider->value(), -73);
    }
};

QTEST_MAIN(TestSliceFlagPresentationLifetime)
#include "tst_slice_flag_presentation_lifetime.moc"
