// no-port-check: NereusSDR-original test for the colour forms the
// workspace preview's QSS translator reads (src/gui/styles/QssTranslator)
// and the stylesheet colour table beside the legacy rows
// (src/gui/styles/StylesheetColourTable).
//
// Covers: 3-, 4-, 6- and 8-digit hex, rgb() and rgba() in Qt's 0-255
// and percent forms, colour names; alpha kept; "transparent" and "none"
// kept; rows that resolve differently by property kind; a kept literal
// left alone; comments, quoted strings, url() and selectors untouched;
// translating the retained original twice gives the same result; the
// table names only existing roles and never shadows a legacy row.

// Modification history (NereusSDR):
//   2026-10-12 - Text token regressions, J.J. Boyd (KG4VCF),
//                AI-assisted via OpenAI Codex.

#include <QtTest/QtTest>

#include "gui/styles/QssTranslator.h"
#include "gui/styles/StylesheetColourTable.h"
#include "gui/styles/WorkspacePalette.h"

using namespace NereusSDR;
namespace QT = NereusSDR::QssTranslator;

class TestQssTranslatorForms : public QObject
{
    Q_OBJECT

private:
    static QString role(const char* name)
    {
        return Workspace::WorkspacePalette::glassAndDepth().qss(QString::fromLatin1(name));
    }

private slots:
    void fontFloorOnlyRewritesFontSizeTokens()
    {
        const QString original = QStringLiteral(
            "QLabel[tag=\"font-size: 8px\"] {"
            " /* font-size: 7px; */ font-size: 9px; min-height: 9px;"
            " padding: 2px; font-family: '9px'; image: url(:/9px.svg); }"
            "QLabel:disabled { FONT-SIZE: 10.5px; }"
            "QLabel#larger { font-size: 11px; }"
            "QLabel#points { font-size: 7pt; }"
            "QLabel#largePoints { font-size: 12pt; }");
        QT::Options options;
        options.minimumTextPixelSize = 11;
        options.textLogicalDpi = 96.0;
        const QString expected = QStringLiteral(
            "QLabel[tag=\"font-size: 8px\"] {"
            " /* font-size: 7px; */ font-size: 11px; min-height: 9px;"
            " padding: 2px; font-family: '9px'; image: url(:/9px.svg); }"
            "QLabel:disabled { FONT-SIZE: 11px; }"
            "QLabel#larger { font-size: 11px; }"
            "QLabel#points { font-size: 11px; }"
            "QLabel#largePoints { font-size: 12pt; }");
        QCOMPARE(QT::translate(original, options).qss, expected);
        QCOMPARE(QT::translate(expected, options).qss, expected);
        QCOMPARE(QT::translate(original).qss, original);
        QCOMPARE(QT::translate(QStringLiteral("font-size:9px; margin:9px"), options).qss,
                 QStringLiteral("font-size:11px; margin:9px"));
        QCOMPARE(QT::translate(QStringLiteral("font-size: '9px';"), options).qss,
                 QStringLiteral("font-size: '9px';"));
        // Higher logical DPI makes this point font large enough already.
        options.textLogicalDpi = 144.0;
        QCOMPARE(QT::translate(QStringLiteral("font-size: 7pt;"), options).qss,
                 QStringLiteral("font-size: 7pt;"));
    }

    void sixDigitHex()
    {
        const auto r = QT::translate(QStringLiteral("QLabel { border: 1px solid #205070; }"));
        QCOMPARE(r.qss, QStringLiteral("QLabel { border: 1px solid %1; }").arg(role("border")));
        QVERIFY(r.unknownLiterals.isEmpty());
    }

    void threeDigitHex()
    {
        // "#888" is "#888888": text-tertiary for text.
        const auto r = QT::translate(QStringLiteral("QLabel { color: #888; }"));
        QCOMPARE(r.qss, QStringLiteral("QLabel { color: %1; }").arg(role("text-tertiary")));
        QCOMPARE(r.rewritten, 1);
    }

    void eightDigitHexKeepsAlpha()
    {
        // Qt reads #AARRGGBB: alpha 0x80 over the legacy #205070.
        const auto r = QT::translate(QStringLiteral("QWidget { border-color: #80205070; }"));
        QCOMPARE(r.qss, QStringLiteral("QWidget { border-color: #80%1; }")
                            .arg(role("border").mid(1)));
    }

    void fourDigitHexKeepsAlpha()
    {
        // #ARGB, one digit per channel: alpha 0xaa over "#555555", the
        // neutral chip, which is badge-off-bg as a background.
        const auto r = QT::translate(QStringLiteral("QLabel { background: #a555; }"));
        QCOMPARE(r.qss, QStringLiteral("QLabel { background: #aa%1; }")
                            .arg(role("badge-off-bg").mid(1)));
    }

    void rgbFunction()
    {
        const auto r = QT::translate(QStringLiteral("QLabel { border: 1px solid rgb(32, 80, 112); }"));
        QCOMPARE(r.qss, QStringLiteral("QLabel { border: 1px solid %1; }").arg(role("border")));
    }

    void rgbaKeepsIntegerAlpha()
    {
        const QColor b = QColor::fromString(role("border"));
        const auto r = QT::translate(
            QStringLiteral("QLabel { border: 1px solid rgba(32, 80, 112, 128); }"));
        QCOMPARE(r.qss, QStringLiteral("QLabel { border: 1px solid rgba(%1, %2, %3, 128); }")
                            .arg(b.red()).arg(b.green()).arg(b.blue()));
    }

    void rgbaKeepsFractionAlpha()
    {
        const QColor b = QColor::fromString(role("border"));
        const auto r = QT::translate(QStringLiteral("QLabel { border-color: rgba(32,80,112,0.5); }"));
        QCOMPARE(r.qss, QStringLiteral("QLabel { border-color: rgba(%1, %2, %3, 0.5); }")
                            .arg(b.red()).arg(b.green()).arg(b.blue()));
    }

    void percentComponentsAndAlpha()
    {
        // rgb(100%, 0%, 0%) is "#ff0000": danger as text.
        const QColor d = QColor::fromString(role("danger"));
        const auto opaque = QT::translate(QStringLiteral("QLabel { color: rgb(100%, 0%, 0%); }"));
        QCOMPARE(opaque.qss, QStringLiteral("QLabel { color: %1; }").arg(role("danger")));
        const auto alpha = QT::translate(QStringLiteral("QLabel { color: rgba(100%, 0%, 0%, 40%); }"));
        QCOMPARE(alpha.qss, QStringLiteral("QLabel { color: rgba(%1, %2, %3, 40%); }")
                                .arg(d.red()).arg(d.green()).arg(d.blue()));
    }

    void namedColour()
    {
        const auto r = QT::translate(QStringLiteral("QLabel { color: red; font-weight: bold; }"));
        QCOMPARE(r.qss, QStringLiteral("QLabel { color: %1; font-weight: bold; }").arg(role("danger")));
        QVERIFY(r.unknownLiterals.isEmpty());
    }

    void namesOnlyInColourProperties()
    {
        // A font family or a property that holds no colour is not read.
        const QString qss = QStringLiteral(
            "QLabel { font-family: Red; qproperty-text: blue; border: 1px solid tan; }");
        const auto r = QT::translate(qss);
        QCOMPARE(r.qss, qss);
        QCOMPARE(r.unknownLiterals, QStringList{QStringLiteral("tan")});
    }

    void transparentAndNoneStay()
    {
        const auto r = QT::translate(QStringLiteral(
            "QLabel { background: transparent; color: #205070; border: none; outline: none; }"));
        QCOMPARE(r.qss, QStringLiteral(
            "QLabel { background: transparent; color: %1; border: none; outline: none; }")
                            .arg(role("border")));
        QVERIFY(r.unknownLiterals.isEmpty());
    }

    void rowsResolveByPropertyKind()
    {
        // #2a5a8a: the menu's selected item fill, and the edge of the
        // BYO status banner. #4a5a6a: disabled text, and the disabled
        // slider handle.
        const auto r = QT::translate(QStringLiteral(
            "QMenu::item:selected { background: #2a5a8a; }"
            "QLabel { border: 1px solid #2a5a8a; }"
            "QLabel:disabled { color: #4a5a6a; }"
            "QSlider::handle:disabled { background: #4a5a6a; }"));
        QCOMPARE(r.qss, QStringLiteral(
            "QMenu::item:selected { background: %1; }"
            "QLabel { border: 1px solid %2; }"
            "QLabel:disabled { color: %3; }"
            "QSlider::handle:disabled { background: %4; }")
                            .arg(role("selected-top"), role("accent"), role("disabled-text"),
                                 role("border-muted")));
        QVERIFY(role("selected-top") != role("accent"));
        QVERIFY(role("disabled-text") != role("border-muted"));
    }

    void kindRowNotUsedForOtherProperty()
    {
        // #c14848 has a row for borders only; as text it is the
        // connection state pill and is kept.
        const auto r = QT::translate(QStringLiteral(
            "QFrame { border: 1px solid #c14848; } QLabel { color: #c14848; }"));
        QCOMPARE(r.qss, QStringLiteral("QFrame { border: 1px solid %1; } QLabel { color: #c14848; }")
                            .arg(role("danger")));
        QCOMPARE(r.unknownLiterals, QStringList{QStringLiteral("#c14848")});
    }

    void keptLiteralLeftAlone()
    {
        // rgba(0, 112, 192, 180) is the overlay's lit button: kept,
        // although #0070c0 has a legacy row. Another alpha over the
        // same colour still resolves.
        const QColor s = QColor::fromString(role("sel-bg"));
        const auto kept = QT::translate(QStringLiteral(
            "QPushButton { background: rgba(0, 112, 192, 180); color: #ffffff; }"));
        QCOMPARE(kept.qss, QStringLiteral(
            "QPushButton { background: rgba(0, 112, 192, 180); color: #ffffff; }"));
        QCOMPARE(kept.unknownLiterals,
                 (QStringList{QStringLiteral("rgba(0,112,192,180)"), QStringLiteral("#ffffff")}));
        const auto other = QT::translate(QStringLiteral(
            "QPushButton { background: rgba(0, 112, 192, 100); }"));
        QCOMPARE(other.qss, QStringLiteral("QPushButton { background: rgba(%1, %2, %3, 100); }")
                                .arg(s.red()).arg(s.green()).arg(s.blue()));
    }

    void commentsStringsUrlsAndSelectorsUntouched()
    {
        const QString qss = QStringLiteral(
            "QLabel#red, QWidget[tint=\"red\"] {"
            " /* red #205070 rgb(32, 80, 112) */"
            " image: url(:/red/#205070.svg);"
            " font-family: 'red #205070';"
            " color: red; }");
        const auto r = QT::translate(qss);
        QCOMPARE(r.qss, QStringLiteral(
            "QLabel#red, QWidget[tint=\"red\"] {"
            " /* red #205070 rgb(32, 80, 112) */"
            " image: url(:/red/#205070.svg);"
            " font-family: 'red #205070';"
            " color: %1; }").arg(role("danger")));
        QCOMPARE(r.rewritten, 1);
    }

    void unknownLiteralsAreNormalised()
    {
        const auto r = QT::translate(QStringLiteral(
            "QLabel { color: RGBA(1, 2, 3, 4); background: #ABC; border-color: Plum; }"));
        QCOMPARE(r.unknownLiterals, (QStringList{QStringLiteral("rgba(1,2,3,4)"),
                                                 QStringLiteral("#abc"),
                                                 QStringLiteral("plum")}));
        QCOMPARE(r.qss, QStringLiteral(
            "QLabel { color: RGBA(1, 2, 3, 4); background: #ABC; border-color: Plum; }"));
    }

    void retainedOriginalTranslatesTheSameTwice()
    {
        const QString original = QStringLiteral(
            "QPushButton { background: #12202e; color: #e0e8f0; border: 1px solid #2e4e6e; }"
            "QPushButton:hover { background: rgba(37, 53, 69, 200); }"
            "QLabel { color: #888; border-color: #80205070; }");
        const auto first = QT::translate(original);
        const auto second = QT::translate(original);
        QCOMPARE(second.qss, first.qss);
        QCOMPARE(second.unknownLiterals, first.unknownLiterals);
        QCOMPARE(first.rewritten, 6);
    }

    void tableNamesExistingRolesAndNeverShadowsALegacyRow()
    {
        const auto& palette = Workspace::WorkspacePalette::glassAndDepth();
        QSet<QString> legacy;
        for (const auto& row : Workspace::WorkspacePalette::legacyRows()) {
            legacy.insert(QString::fromLatin1(row.legacyHex).toLower());
        }
        QSet<QString> seen;
        for (const auto& row : Workspace::StylesheetColours::rows()) {
            const QString hex = QString::fromLatin1(row.hex);
            const QString kind = QString::fromLatin1(row.kind);
            QVERIFY2(palette.contains(QString::fromLatin1(row.role)), row.role);
            QVERIFY2(!legacy.contains(hex), row.hex);
            QVERIFY2(hex == hex.toLower() && hex.size() == 7, row.hex);
            QVERIFY2(kind == QLatin1String("background") || kind == QLatin1String("border")
                         || kind == QLatin1String("text"),
                     row.kind);
            QVERIFY2(!seen.contains(hex + kind), row.hex);
            seen.insert(hex + kind);
        }
        for (const auto& k : Workspace::StylesheetColours::kept()) {
            const QString lit = QString::fromLatin1(k.literal);
            QVERIFY2(!legacy.contains(lit), k.literal);
            QVERIFY2(lit == lit.toLower() && !lit.contains(QLatin1Char(' ')), k.literal);
            QVERIFY2(qstrlen(k.reason) > 0, k.literal);
        }
    }
};

QTEST_MAIN(TestQssTranslatorForms)
#include "tst_qss_translator_forms.moc"
