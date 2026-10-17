// tests/tst_setup_ampersand_text.cpp  (NereusSDR)
//
// Widget texts that Qt reads for a keyboard mnemonic (group box titles,
// button, check box and radio button texts, tab titles and the labels a
// form row ties to its field) drop a lone "&" and underline the next
// character. A title meant to read "Identity & Status" showed
// "Identity _Status" offscreen and "Identity  Status" on macOS. A word
// ampersand in such a text must be written "&&". A real mnemonic ("&File")
// is a lone "&" before a letter, so the check looks for a lone "&" before
// a space or at the end, which no author means as a shortcut.
//
// no-port-check: NereusSDR-original test; no upstream logic is ported.

#include <QtTest/QtTest>
#include <QApplication>

#include <QAbstractButton>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTabBar>

#include "core/AppSettings.h"
#include "core/audio/VirtualCableDetector.h"
#include "gui/SetupDialog.h"
#include "gui/VaxFirstRunDialog.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

// True when the text holds a lone "&" (not part of "&&") followed by
// whitespace or the end of the text: Qt eats it as a mnemonic marker.
bool hasWordAmpersandEatenAsMnemonic(const QString& text)
{
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) != QLatin1Char('&')) {
            continue;
        }
        if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('&')) {
            ++i; // "&&" draws one ampersand
            continue;
        }
        if (i + 1 >= text.size() || text.at(i + 1).isSpace()) {
            return true;
        }
    }
    return false;
}

// Every mnemonic-parsed text under root whose word ampersand Qt would eat.
QStringList eatenAmpersands(const QWidget* root)
{
    QStringList faults;
    auto check = [&faults](const QString& kind, const QString& text) {
        if (hasWordAmpersandEatenAsMnemonic(text)) {
            faults << QStringLiteral("%1 \"%2\"").arg(kind, text);
        }
    };
    for (const QGroupBox* box : root->findChildren<QGroupBox*>()) {
        check(QStringLiteral("group box"), box->title());
    }
    for (const QAbstractButton* button : root->findChildren<QAbstractButton*>()) {
        check(QString::fromLatin1(button->metaObject()->className()), button->text());
    }
    for (const QLabel* label : root->findChildren<QLabel*>()) {
        if (label->buddy() != nullptr) {
            check(QStringLiteral("buddy label"), label->text());
        }
    }
    for (const QTabBar* bar : root->findChildren<QTabBar*>()) {
        for (int i = 0; i < bar->count(); ++i) {
            check(QStringLiteral("tab"), bar->tabText(i));
        }
    }
    return faults;
}

// The cables' render ends ("CABLE-x Input", isInput false): the end VAX
// writes to, and the only end the dialog suggests a binding for.
QVector<DetectedCable> twoNewCables()
{
    QVector<DetectedCable> v;
    v.push_back({VirtualCableProduct::VbCableC,
                 QStringLiteral("CABLE-C Input (VB-Audio Virtual Cable)"), false, 0});
    v.push_back({VirtualCableProduct::VbCableD,
                 QStringLiteral("CABLE-D Input (VB-Audio Virtual Cable)"), false, 0});
    return v;
}

} // namespace

class TstSetupAmpersandText : public QObject {
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

    void helperFindsOnlyWordAmpersands()
    {
        QVERIFY(hasWordAmpersandEatenAsMnemonic(QStringLiteral("Identity & Status")));
        QVERIFY(hasWordAmpersandEatenAsMnemonic(QStringLiteral("Tail &")));
        QVERIFY(!hasWordAmpersandEatenAsMnemonic(QStringLiteral("Identity && Status")));
        QVERIFY(!hasWordAmpersandEatenAsMnemonic(QStringLiteral("&File")));
        QVERIFY(!hasWordAmpersandEatenAsMnemonic(QStringLiteral("NR&2")));
        QVERIFY(!hasWordAmpersandEatenAsMnemonic(QStringLiteral("Plain text")));
    }

    // Every Setup page, built: no group title, button, tab or form label
    // loses a word ampersand to a mnemonic.
    void everySetupPageShowsItsAmpersands()
    {
        RadioModel model;
        SetupDialog dialog(&model);
        dialog.realizeAllPagesForTest();
        const QStringList faults = eatenAmpersands(&dialog);
        QVERIFY2(faults.isEmpty(), qPrintable(faults.join(QStringLiteral("; "))));
    }

    // The named group titles and buttons read with their ampersand.
    void namedTitlesReadWithTheirAmpersand()
    {
        RadioModel model;
        SetupDialog dialog(&model);
        dialog.realizeAllPagesForTest();
        QStringList titles;
        for (const QGroupBox* box : dialog.findChildren<QGroupBox*>()) {
            titles << box->title();
        }
        QStringList buttons;
        for (const QAbstractButton* button : dialog.findChildren<QAbstractButton*>()) {
            buttons << button->text();
        }
        QVERIFY(titles.contains(QStringLiteral("Identity && Status")));
        QVERIFY(titles.contains(QStringLiteral("Pairing && Band Source")));
        QVERIFY(titles.contains(QStringLiteral("Calibration && Peak Hold")));
        QVERIFY(buttons.contains(QStringLiteral("Save && Reboot Amp")));
        QVERIFY(buttons.contains(QStringLiteral("Save && Reboot Tuner")));
    }

    // Combo box items are drawn as written: none shows a doubled ampersand,
    // and NB2 Mode reads as Thetis's comboDSPNOBmode does
    // (setup.designer.cs:44550-44555 [v2.10.3.15]).
    void comboItemsShowOneAmpersand()
    {
        RadioModel model;
        SetupDialog dialog(&model);
        dialog.realizeAllPagesForTest();
        QStringList faults;
        bool sampleAndHold = false;
        bool holdAndSample = false;
        for (const QComboBox* combo : dialog.findChildren<QComboBox*>()) {
            for (int i = 0; i < combo->count(); ++i) {
                const QString text = combo->itemText(i);
                if (text.contains(QStringLiteral("&&"))) {
                    faults << QStringLiteral("combo item \"%1\"").arg(text);
                }
                sampleAndHold = sampleAndHold || text == QStringLiteral("Sample & Hold");
                holdAndSample = holdAndSample || text == QStringLiteral("Hold & Sample");
            }
        }
        QVERIFY2(faults.isEmpty(), qPrintable(faults.join(QStringLiteral("; "))));
        QVERIFY(sampleAndHold);
        QVERIFY(holdAndSample);
    }

    // The VAX first-run "Apply to VAX N & M" button.
    void vaxApplyButtonShowsItsAmpersand()
    {
        VaxFirstRunDialog dlg(FirstRunScenario::RescanNewCables, twoNewCables());
        auto* apply = dlg.findChild<QPushButton*>(QStringLiteral("btnApplySuggested"));
        QVERIFY(apply != nullptr);
        QVERIFY2(!hasWordAmpersandEatenAsMnemonic(apply->text()), qPrintable(apply->text()));
        QVERIFY(apply->text().contains(QStringLiteral(" && ")));
    }
};

QTEST_MAIN(TstSetupAmpersandText)
#include "tst_setup_ampersand_text.moc"
