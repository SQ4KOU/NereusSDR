// no-port-check: enforces the kPopupMenu stylesheet invariant (Phase 3P-I-a T22)
//
// For every file that contains an antenna QMenu popup, each `menu.exec(...)`
// call must be preceded in the same source region by
// `menu.setStyleSheet(QString::fromLatin1(kPopupMenu))` (or any expression
// containing the literal `kPopupMenu` within 500 chars before exec). This
// test grep-scans the known antenna-menu call sites and fails the build
// if any violation appears.
//
// Gates issue #98 regression: an earlier version of VfoWidget / RxApplet
// shipped the menu without the dark-palette stylesheet, rendering as
// dark-on-dark on Ubuntu 25.10 GNOME. If a future edit restores a
// zero-style menu at one of the known sites, this test fails at build
// time before anyone sees the regression.
//
// Call-site registry: add a file path to kKnownAntennaMenuSites when a
// new antenna QMenu lands in the codebase.

#include <QtTest/QtTest>
#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

class TestPopupStyleCoverage : public QObject {
    Q_OBJECT
private slots:
    void every_antenna_menu_uses_kPopupMenu() {
        const QStringList knownAntennaMenuSites = {
            QStringLiteral("src/gui/widgets/VfoWidget.cpp"),
            QStringLiteral("src/gui/applets/RxApplet.cpp"),
            // Add more when new antenna QMenu sites land.
        };

        const QString root = QString::fromLatin1(NEREUS_SOURCE_ROOT);
        for (const QString& rel : knownAntennaMenuSites) {
            QFile f(root + QStringLiteral("/") + rel);
            QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
                     qPrintable(f.errorString() + QStringLiteral(" ") + rel));
            const QString src = QString::fromUtf8(f.readAll());

            // Every occurrence of menu.exec(...) must have the literal
            // "kPopupMenu" within 500 chars before it. This is a coarse
            // proximity check — the intent is "the nearest setStyleSheet
            // call used kPopupMenu", not a full AST match. 500 chars is
            // comfortably enough to catch a one-line-before setStyleSheet
            // while rejecting an exec in a different method a few
            // hundred lines below.
            QRegularExpression reExec(QStringLiteral(R"(menu\.exec\()"));
            auto it = reExec.globalMatch(src);
            while (it.hasNext()) {
                const auto m = it.next();
                const int pos = m.capturedStart();
                const QString window = src.mid(qMax(0, pos - 500), 500);
                QVERIFY2(window.contains(QStringLiteral("kPopupMenu")),
                         qPrintable(
                             QStringLiteral("antenna menu in ") + rel +
                             QStringLiteral(" near char ") + QString::number(pos) +
                             QStringLiteral(" missing kPopupMenu")));
            }
        }
    }
};

QTEST_APPLESS_MAIN(TestPopupStyleCoverage)
#include "tst_popup_style_coverage.moc"
