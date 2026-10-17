// no-port-check: NereusSDR-original test of the workspace look's default.
// Modification history (NereusSDR):
//   2026-10-12 - Written: the workspace look is on unless
//                 NEREUS_WORKSPACE_PREVIEW is "0". Unset, empty, "1" and
//                 any other value are on. J.J. Boyd (KG4VCF), AI-assisted
//                 via Anthropic Claude Code.
#include <QtTest>
#include <QString>
#include "gui/styles/WorkspacePreview.h"

using namespace NereusSDR;

class TstWorkspacePreviewDefault : public QObject
{
    Q_OBJECT

private slots:
    void theRule_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("on");

        // qEnvironmentVariable() gives a null string for an unset variable.
        QTest::newRow("unset") << QString() << true;
        QTest::newRow("empty") << QStringLiteral("") << true;
        QTest::newRow("1") << QStringLiteral("1") << true;
        QTest::newRow("0") << QStringLiteral("0") << false;
        QTest::newRow("junk") << QStringLiteral("junk") << true;
    }

    void theRule()
    {
        QFETCH(QString, value);
        QFETCH(bool, on);
        QCOMPARE(WorkspacePreview::enabledFromEnvironmentValue(value), on);
    }

    // The runner starts every test with the look off (tests/CMakeLists.txt),
    // and setEnabledForTest() still decides after that, both ways.
    void aTestStillSetsTheSwitchItself()
    {
        const bool before = WorkspacePreview::enabled();
        WorkspacePreview::setEnabledForTest(true);
        QVERIFY(WorkspacePreview::enabled());
        WorkspacePreview::setEnabledForTest(false);
        QVERIFY(!WorkspacePreview::enabled());
        WorkspacePreview::setEnabledForTest(before);
    }
};

QTEST_GUILESS_MAIN(TstWorkspacePreviewDefault)
#include "tst_workspace_preview_default.moc"
