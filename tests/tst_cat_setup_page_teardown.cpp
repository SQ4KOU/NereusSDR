// no-port-check: NereusSDR-original test that a CAT channel setup page outlives its model safely.
// 2026-10-08 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// 2026-10-09 CAT reports nothing once the model's slices are deleted: the
//            model retires CatService while its slices exist, and the
//            service's destructor does not retire it again. J.J. Boyd
//            (KG4VCF), AI-assisted via Anthropic Claude Code.
#include <QtTest>
#include <memory>
#include <QComboBox>
#include "core/AppSettings.h"
#include "core/cat/CatControl.h"
#include "core/cat/CatService.h"
#include "gui/setup/CatNetworkSetupPages.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

// MainWindow creates its RadioModel before its SetupDialog, so on quit the
// model's children (CatService among them) are destroyed while a realized
// Serial Ports or TCP/IP CAT page is still alive. CatService's teardown
// reports every channel stopped; the page must not answer by reading the
// model's slices, which the model has already deleted.
class TstCatSetupPageTeardown : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(
            QStringLiteral("cat-setup-teardown-%1").arg(QCoreApplication::applicationPid()));
    }
    void init() { AppSettings::instance().clear(); }

    void serialPageOutlivesModel() { outlivesModel(true); }
    void tcpPageOutlivesModel() { outlivesModel(false); }

    // The cause of the 2026-10-08 09:48 visual preview crashes: RadioModel's
    // destructor retires CatService while its slices exist, deletes the
    // slices, and then deletes CatService (a child), whose destructor
    // retired it a second time and reported every channel stopped again.
    // Any listener that read the model's slices on those reports (the CAT
    // channel pages did, in fillSlices) read deleted slices. Counts the
    // CAT reports made after the last slice is deleted.
    void catReportsNothingAfterSlicesAreDeleted()
    {
        auto model = std::make_unique<RadioModel>();
        model->addSlice();
        model->addSlice();
        CatService* service = model->catService();
        CatControl* control = model->catControl();
        QVERIFY(service);
        QVERIFY(control);
        int slicesLeft = model->slices().size();
        QVERIFY(slicesLeft >= 2);
        for (SliceModel* slice : model->slices()) {
            connect(slice, &QObject::destroyed, this, [&slicesLeft] { --slicesLeft; });
        }
        QStringList late;
        const auto count = [&slicesLeft, &late](const char* what) {
            return [&slicesLeft, &late, what] {
                if (slicesLeft == 0) { late << QString::fromLatin1(what); }
            };
        };
        connect(control, &CatControl::channelStatusChanged, this, count("CatControl::channelStatusChanged"));
        connect(control, &CatControl::channelConfigChanged, this, count("CatControl::channelConfigChanged"));
        connect(control, &CatControl::availabilityChanged, this, count("CatControl::availabilityChanged"));
        connect(control, &CatControl::pttStateChanged, this, count("CatControl::pttStateChanged"));
        connect(service, &CatService::channelStateChanged, this, count("CatService::channelStateChanged"));
        connect(service, &CatService::ptyPathChanged, this, count("CatService::ptyPathChanged"));
        connect(service, &CatService::clientCountChanged, this, count("CatService::clientCountChanged"));
        connect(service, &CatService::rigctldClientCountChanged, this, count("CatService::rigctldClientCountChanged"));
        connect(service, &CatService::pttStateChanged, this, count("CatService::pttStateChanged"));
        connect(service, &CatService::sessionClosed, this, count("CatService::sessionClosed"));
        model.reset();
        QCOMPARE(slicesLeft, 0);
        QVERIFY2(late.isEmpty(), qPrintable(QStringLiteral("%1 CAT report(s) after the slices were deleted: %2")
                                                .arg(late.size()).arg(late.join(QStringLiteral(", ")))));
    }

private:
    void outlivesModel(bool serial)
    {
        auto model = std::make_unique<RadioModel>();
        model->addSlice();
        model->addSlice();
        std::unique_ptr<QWidget> page;
        if (serial) {
            page = std::make_unique<CatSerialPortsPage>(model.get());
        } else {
            page = std::make_unique<CatTcpIpPage>(model.get());
        }
        auto* primary = page->findChild<QComboBox*>(QStringLiteral("cat1Primary"));
        QVERIFY(primary);
        const int slicesShown = primary->count();
        QVERIFY(slicesShown >= 3);  // None plus the two slices
        model.reset();
        // The page stays usable and keeps what it last showed.
        QCOMPARE(primary->count(), slicesShown);
        page->show();
        page.reset();
    }
};

QTEST_MAIN(TstCatSetupPageTeardown)
#include "tst_cat_setup_page_teardown.moc"
