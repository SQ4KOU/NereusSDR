// no-port-check: NereusSDR-original test.
// =================================================================
// tests/tst_mnr_bnr_availability.cpp  (NereusSDR)
// =================================================================
// R-R3-49, Sub-epic C-1: MNR and BNR are always on the VFO flag. When one
// cannot run, its button is shown disabled with the plain reason, never
// hidden (operator, 2026-09-25: "Not a fan of disappearing buttons but
// rather disabled."), and choosing it is refused with that reason.
//
//   - MNR runs only on a Mac. The Core says whether it can run it
//     (DspAssetService mnrRunnable / mnrStatus); a remote window follows
//     its Core, so a Mac window on a Linux Core shows MNR disabled.
//   - BNR is in no build (NVIDIA only), so it is disabled everywhere with
//     a reason that holds for every build.
// No radio is connected and nothing keys.
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original test for NereusSDR by J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code
//               (R-R3-49, Sub-epic C-1).
// =================================================================

#include <QtTest>
#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>

#include "core/WdspTypes.h"
#include "core/dsp/DspAssetService.h"
#include "gui/widgets/DspParamPopup.h"
#include "gui/widgets/VfoWidget.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "OperatorWording.h"

using namespace NereusSDR;

namespace {

bool offered(const QPushButton* button)
{
    return button && !button->isHidden() && button->isEnabled();
}

bool shownDisabledWith(const QPushButton* button, const QString& reason)
{
    return button && !button->isHidden() && !button->isEnabled()
        && button->toolTip() == reason && OperatorWording::isPlain(reason);
}

int popupsOf(const QWidget& widget)
{
    return int(widget.findChildren<DspParamPopup*>().size());
}

const QString kMnrReason =
    QStringLiteral("MNR runs only on a Mac, and this Core is not a Mac, so MNR cannot run.");
const QString kBnrReason =
    QStringLiteral("NVIDIA noise removal is not in this version of NereusSDR.");

} // namespace

class TestMnrBnrAvailability : public QObject {
    Q_OBJECT

private slots:
    void reasonsArePlain()
    {
        QCOMPARE(RadioModel::mnrCannotRunReason(), kMnrReason);
        QCOMPARE(RadioModel::bnrCannotRunReason(), kBnrReason);
        QVERIFY(OperatorWording::isPlain(kMnrReason));
        QVERIFY(OperatorWording::isPlain(kBnrReason));
        QVERIFY(OperatorWording::coreCalledStationIn(kMnrReason).isEmpty());
        QVERIFY(OperatorWording::coreCalledStationIn(kBnrReason).isEmpty());
    }

    void bnrIsShownDisabledAndRefused()
    {
        QVERIFY(!RadioModel::bnrBuilt());
        RadioModel model;
        QCOMPARE(model.nrCannotRunReason(NrSlot::BNR), kBnrReason);
        model.addSlice();
        SliceModel* slice = model.activeSlice();
        QVERIFY(slice);
        QSignalSpy refused(slice, &SliceModel::nrSelectionRefused);
        slice->setActiveNr(NrSlot::BNR);
        QCOMPARE(slice->activeNr(), NrSlot::Off);
        QCOMPARE(refused.count(), 1);
        QCOMPARE(refused.at(0).at(0).toString(), kBnrReason);

        VfoWidget vfo;
        vfo.setRadioModel(&model);
        vfo.setSlice(slice);
        QVERIFY(shownDisabledWith(vfo.bnrButtonForTest(), kBnrReason));
        emit vfo.bnrButtonForTest()->customContextMenuRequested(QPoint(1, 1));
        QCOMPARE(popupsOf(vfo), 0);

        // A remote window shows it the same way.
        RadioModel remote(RadioModel::Role::Remote);
        VfoWidget window;
        window.setRadioModel(&remote);
        QVERIFY(shownDisabledWith(window.bnrButtonForTest(), kBnrReason));
    }

    void mnrFollowsWhatTheCoreCanRun()
    {
        RadioModel model;
        DspAssetService* assets = model.dspAssets();
        QVERIFY(assets);
        model.addSlice();
        SliceModel* slice = model.activeSlice();
        QVERIFY(slice);
        VfoWidget vfo;
        vfo.setRadioModel(&model);
        vfo.setSlice(slice);
#ifdef HAVE_MNR
        // A Mac Core runs MNR.
        QVERIFY(assets->mnrRunnable());
        QVERIFY(assets->mnrStatus().isEmpty());
        QVERIFY(model.nrCannotRunReason(NrSlot::MNR).isEmpty());
        QVERIFY(offered(vfo.mnrButtonForTest()));
        slice->setActiveNr(NrSlot::MNR);
        QCOMPARE(slice->activeNr(), NrSlot::MNR);
        QVERIFY(vfo.mnrButtonForTest()->isChecked());
        // Were this Core unable to run it, a slice holding MNR turns off
        // with the reason and the button disables.
        assets->setMnrAvailability(false, kMnrReason);
        QCOMPARE(slice->activeNr(), NrSlot::Off);
        QCOMPARE(slice->nnrLastError(), kMnrReason);
        QVERIFY(!vfo.mnrButtonForTest()->isChecked());
#else
        // A Core that is not a Mac cannot.
        QVERIFY(!assets->mnrRunnable());
        QCOMPARE(assets->mnrStatus(), kMnrReason);
#endif
        QCOMPARE(model.nrCannotRunReason(NrSlot::MNR), kMnrReason);
        QVERIFY(shownDisabledWith(vfo.mnrButtonForTest(), kMnrReason));
        emit vfo.mnrButtonForTest()->customContextMenuRequested(QPoint(1, 1));
        QCOMPARE(popupsOf(vfo), 0);
        QSignalSpy refused(slice, &SliceModel::nrSelectionRefused);
        slice->setActiveNr(NrSlot::MNR);
        QCOMPARE(slice->activeNr(), NrSlot::Off);
        QCOMPARE(refused.count(), 1);
        QCOMPARE(refused.at(0).at(0).toString(), kMnrReason);
        // Other NR still works.
        slice->setActiveNr(NrSlot::NR2);
        QCOMPARE(slice->activeNr(), NrSlot::NR2);
    }

    void remoteWindowFollowsTheCoresMnr()
    {
        RadioModel remote(RadioModel::Role::Remote);
        DspAssetService* assets = remote.dspAssets();
        QVERIFY(assets->mnrRunnable());   // an older Core never says
        VfoWidget vfo;
        vfo.setRadioModel(&remote);
        QVERIFY(offered(vfo.mnrButtonForTest()));

        // A Mac window on a Linux Core.
        QVERIFY(assets->applyRemoteProperty("mnrStatus", kMnrReason));
        QVERIFY(assets->applyRemoteProperty("mnrRunnable", false));
        QVERIFY(shownDisabledWith(vfo.mnrButtonForTest(), kMnrReason));
        QCOMPARE(remote.nrCannotRunReason(NrSlot::MNR), kMnrReason);

        QVERIFY(assets->applyRemoteProperty("mnrRunnable", true));
        QVERIFY(offered(vfo.mnrButtonForTest()));
        QVERIFY(remote.nrCannotRunReason(NrSlot::MNR).isEmpty());
        // A new session starts from the defaults again.
        QVERIFY(assets->applyRemoteProperty("mnrRunnable", false));
        QVERIFY(!vfo.mnrButtonForTest()->isEnabled());
        assets->resetSession();
        QVERIFY(assets->mnrRunnable());
        QVERIFY(assets->mnrStatus().isEmpty());
        QVERIFY(offered(vfo.mnrButtonForTest()));
    }

    void flagWithoutAModelFollowsThisBuild()
    {
        VfoWidget vfo;
        QVERIFY(!vfo.mnrButtonForTest()->isHidden());
        QVERIFY(!vfo.bnrButtonForTest()->isHidden());
        QVERIFY(!vfo.dfnrButtonForTest()->isHidden());
        QVERIFY(shownDisabledWith(vfo.bnrButtonForTest(), kBnrReason));
#ifdef HAVE_MNR
        QVERIFY(offered(vfo.mnrButtonForTest()));
#else
        QVERIFY(shownDisabledWith(vfo.mnrButtonForTest(), kMnrReason));
#endif
#ifdef HAVE_DFNR
        QVERIFY(offered(vfo.dfnrButtonForTest()));
#else
        QVERIFY(shownDisabledWith(vfo.dfnrButtonForTest(),
                                  RadioModel::nrCannotRunInThisBuildReason(NrSlot::DFNR)));
#endif
        QVERIFY(RadioModel::nrCannotRunInThisBuildReason(NrSlot::NR2).isEmpty());
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestMnrBnrAvailability test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_mnr_bnr_availability.moc"
