// no-port-check: NereusSDR-original test for the workspace preview's
// restricted QSS translator (src/gui/styles/QssTranslator) and the
// scoped subtree pass (src/gui/styles/WorkspacePreview).
//
// Covers the translator checks of the Longpath theme design: complete
// tokens, six versus eight digits, alpha and url() preservation,
// selectors, unknown literals, one left-to-right pass, and overlapping
// mappings generated afresh from the retained original. Also covers
// the component treatment (src/gui/styles/WorkspaceComponents): raised,
// pressed, selected and sunken controls, semantic colours kept, the
// treatment idempotent, states that render distinct, and size hints
// that do not change at an equal font size (the slider takes Longpath's
// handle, JJ's decision).
//
// Modification history (NereusSDR):
//   2026-10-10 - The component slider checks JJ's D29 track and its
//                 disabled rule. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-12 - Compare component geometry at the approved font floor.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.

#include <QtTest/QtTest>

#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>

#include "gui/styles/QssTranslator.h"
#include "gui/styles/WorkspaceComponents.h"
#include "gui/styles/WorkspacePalette.h"
#include "gui/styles/WorkspacePreview.h"

using namespace NereusSDR;
namespace QT = NereusSDR::QssTranslator;
namespace WC = NereusSDR::Workspace::Components;

namespace {

// The colour at the centre of a widget, rendered offscreen.
QColor centreColour(QWidget& w)
{
    const QImage img = w.grab().toImage();
    return img.pixelColor(img.width() / 2, img.height() / 2);
}

// The colour a few pixels below the top edge, where the raised and the
// pressed (flipped) fills differ most; at mid-height they are equal.
QColor upperColour(QWidget& w)
{
    const QImage img = w.grab().toImage();
    return img.pixelColor(img.width() / 2, 4);
}

int distance(const QColor& a, const QColor& b)
{
    return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green())
        + std::abs(a.blue() - b.blue());
}

} // namespace

class TestWorkspaceQssTranslator : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        WorkspacePreview::setEnabledForTest(false);
        WorkspacePreview::resetUnknownLiteralCounts();
    }

    void defaultTableRolesExistAndAmbiguousHasNoRow()
    {
        const auto& palette = Workspace::WorkspacePalette::glassAndDepth();
        for (const QT::Rule& rule : QT::defaultTable()) {
            QVERIFY2(palette.contains(rule.role), qPrintable(rule.role));
            QCOMPARE(rule.legacyHex, rule.legacyHex.toLower());
            QVERIFY2(rule.legacyHex != QLatin1String(QT::kAmbiguousHex),
                     "#203040 must not have a blanket row");
        }
    }

    void rewritesCompleteSixDigitTokens()
    {
        const QT::Result r = QT::translate(
            QStringLiteral("QLabel { color: #c8d8e8; border: 1px solid #205070; }"));
        QCOMPARE(r.qss, QStringLiteral("QLabel { color: #dcdce1; border: 1px solid #2c2c31; }"));
        QCOMPARE(r.rewritten, 2);
        QVERIFY(r.unknownLiterals.isEmpty());
    }

    void matchesCaseInsensitively()
    {
        QCOMPARE(QT::translate(QStringLiteral("color: #C8D8E8;")).qss,
                 QStringLiteral("color: #dcdce1;"));
    }

    void neverRewritesEightDigitOrLongerTokens()
    {
        const QT::Result r = QT::translate(QStringLiteral(
            "QWidget { background: #205070cc; color: #2050701; border-color: #205070; }"));
        QCOMPARE(r.qss, QStringLiteral(
            "QWidget { background: #205070cc; color: #2050701; border-color: #2c2c31; }"));
        // The eight-digit colour is reported; seven digits is not a colour.
        QCOMPARE(r.unknownLiterals, QStringList{QStringLiteral("#205070cc")});
    }

    void preservesAlphaFunctionsUrlsStringsAndComments()
    {
        const QString src = QStringLiteral(
            "QPushButton {"
            " background: rgba(32, 80, 112, 128);"
            " image: url(:/icons/#205070.svg);"
            " font-family: \"#c8d8e8\";"
            " /* was #205070 */"
            " color: #c8d8e8; }");
        // rgba() is read as a colour: its rgb part resolves, its alpha is
        // kept as written.
        const QString expected = QStringLiteral(
            "QPushButton {"
            " background: rgba(44, 44, 49, 128);"
            " image: url(:/icons/#205070.svg);"
            " font-family: \"#c8d8e8\";"
            " /* was #205070 */"
            " color: #dcdce1; }");
        QCOMPARE(QT::translate(src).qss, expected);
    }

    void preservesSelectors()
    {
        const QString src = QStringLiteral(
            "QWidget#c8d8e8 > QLabel#ff4444:hover { color: #c8d8e8; }");
        QCOMPARE(QT::translate(src).qss, QStringLiteral(
            "QWidget#c8d8e8 > QLabel#ff4444:hover { color: #dcdce1; }"));
    }

    void braceLessSheetIsDeclarations()
    {
        QCOMPARE(QT::translate(QStringLiteral("background: #0f0f1a; color: #8090a0;")).qss,
                 QStringLiteral("background: #08080a; color: #a6a6ac;"));
    }

    void unknownLiteralsStayAndAreReported()
    {
        const QT::Result r = QT::translate(QStringLiteral(
            "QLabel { color: #123456; background: #fff; border: 1px solid #205070; }"));
        QCOMPARE(r.qss, QStringLiteral(
            "QLabel { color: #123456; background: #fff; border: 1px solid #2c2c31; }"));
        QCOMPARE(r.unknownLiterals,
                 (QStringList{QStringLiteral("#123456"), QStringLiteral("#fff")}));
    }

    void ambiguousColourNeedsAnExplicitRole()
    {
        const QString src = QStringLiteral("QSplitter::handle { background: #203040; }");
        const QT::Result plain = QT::translate(src);
        QCOMPARE(plain.qss, src);
        QCOMPARE(plain.unknownLiterals, QStringList{QStringLiteral("#203040")});

        const QT::Result named = QT::translateWithAmbiguousRole(src, QStringLiteral("button-hover"));
        QCOMPARE(named.qss, QStringLiteral("QSplitter::handle { background: #26262b; }"));
        QVERIFY(named.unknownLiterals.isEmpty());
    }

    void ambiguousColourByContext()
    {
        QT::Options opts;
        opts.resolveAmbiguousByContext = true;
        QCOMPARE(QT::translate(QStringLiteral(
                     "QPushButton:hover { background: #203040; }"), opts).qss,
                 QStringLiteral("QPushButton:hover { background: #26262b; }"));
        QCOMPARE(QT::translate(QStringLiteral(
                     "QSlider::groove:horizontal { background: #203040; }"), opts).qss,
                 QStringLiteral("QSlider::groove:horizontal { background: #1f1f23; }"));
        QCOMPARE(QT::translate(QStringLiteral(
                     "QFrame { border: 1px solid #203040; }"), opts).qss,
                 QStringLiteral("QFrame { border: 1px solid #1f1f23; }"));
        // A slider's filled page is the accent; the empty page and scroll
        // bar pages are groove. Vertical sliders fill on the add-page.
        QCOMPARE(QT::translate(QStringLiteral(
                     "QSlider::sub-page:horizontal { background: #203040; }"), opts).qss,
                 QStringLiteral("QSlider::sub-page:horizontal { background: #4a7ba8; }"));
        QCOMPARE(QT::translate(QStringLiteral(
                     "QSlider::add-page:horizontal { background: #203040; }"), opts).qss,
                 QStringLiteral("QSlider::add-page:horizontal { background: #1f1f23; }"));
        QCOMPARE(QT::translate(QStringLiteral(
                     "QSlider::add-page:vertical { background: #203040; }"), opts).qss,
                 QStringLiteral("QSlider::add-page:vertical { background: #4a7ba8; }"));
        QCOMPARE(QT::translate(QStringLiteral(
                     "QSlider::sub-page:vertical { background: #203040; }"), opts).qss,
                 QStringLiteral("QSlider::sub-page:vertical { background: #1f1f23; }"));
        QCOMPARE(QT::translate(QStringLiteral(
                     "QScrollBar::sub-page:horizontal { background: #203040; }"), opts).qss,
                 QStringLiteral("QScrollBar::sub-page:horizontal { background: #1f1f23; }"));
        // An explicit role at the call site wins over the context rule.
        opts.explicitRoles.insert(QStringLiteral("#203040"), QStringLiteral("button-alt-hover"));
        QCOMPARE(QT::translate(QStringLiteral(
                     "QFrame { border: 1px solid #203040; }"), opts).qss,
                 QStringLiteral("QFrame { border: 1px solid #2f3138; }"));
    }

    // A target value that is also a source key: one pass moves each
    // token at most once, and a second translation of the output would
    // move it again. Translating the retained original is stable.
    void overlappingMappingsUseTheRetainedOriginal()
    {
        const QT::Table table = {
            { QStringLiteral("#111111"), QStringLiteral("app-bg") },   // -> #08080a
            { QStringLiteral("#08080a"), QStringLiteral("panel") },    // -> #0c0c0e
        };
        QT::Options opts;
        opts.table = &table;

        const QString original = QStringLiteral("QWidget { color: #111111; background: #08080a; }");
        const QT::Result once = QT::translate(original, opts);
        QCOMPARE(once.qss, QStringLiteral("QWidget { color: #08080a; background: #0c0c0e; }"));
        QCOMPARE(once.rewritten, 2);

        const QT::Result twice = QT::translate(once.qss, opts);
        QCOMPARE(twice.qss, QStringLiteral("QWidget { color: #0c0c0e; background: #0c0c0e; }"));
        QVERIFY(twice.qss != once.qss);

        QCOMPARE(QT::translate(original, opts).qss, once.qss);
    }

    void switchOffLeavesWidgetsAlone()
    {
        QWidget root;
        root.setStyleSheet(QStringLiteral("background: #0f0f1a;"));
        WorkspacePreview::themeSubtree(&root);
        QCOMPARE(root.styleSheet(), QStringLiteral("background: #0f0f1a;"));
        QVERIFY(!root.property(WorkspacePreview::kOriginalQssProperty).isValid());
    }

    void subtreePassThemesFromTheRetainedOriginal()
    {
        WorkspacePreview::setEnabledForTest(true);
        QWidget root;
        root.setStyleSheet(QStringLiteral("background: #0f0f1a;"));
        auto* label = new QLabel(&root);
        label->setStyleSheet(QStringLiteral("QLabel { color: #c8d8e8; background: #abcdef; }"));
        auto* kept = new QWidget(&root);
        kept->setProperty(WorkspacePreview::kSkipProperty, true);
        kept->setStyleSheet(QStringLiteral("background: #0f0f1a;"));
        auto* keptChild = new QLabel(kept);
        keptChild->setStyleSheet(QStringLiteral("color: #c8d8e8;"));

        WorkspacePreview::themeSubtree(&root);
        QCOMPARE(root.styleSheet(), QStringLiteral("background: #08080a;"));
        QCOMPARE(label->styleSheet(), QStringLiteral("QLabel { color: #dcdce1; background: #abcdef; }"));
        QCOMPARE(kept->styleSheet(), QStringLiteral("background: #0f0f1a;"));
        QCOMPARE(keptChild->styleSheet(), QStringLiteral("color: #c8d8e8;"));
        QCOMPARE(root.property(WorkspacePreview::kOriginalQssProperty).toString(),
                 QStringLiteral("background: #0f0f1a;"));

        // A second pass changes nothing and counts nothing twice.
        WorkspacePreview::themeSubtree(&root);
        QCOMPARE(root.styleSheet(), QStringLiteral("background: #08080a;"));
        QCOMPARE(WorkspacePreview::unknownLiteralCounts().value(QStringLiteral("#abcdef")), 1);

        // The widget restyles itself at run time: the new sheet becomes
        // the original.
        label->setStyleSheet(QStringLiteral("QLabel { color: #ff4444; }"));
        WorkspacePreview::themeSubtree(&root);
        QCOMPARE(label->styleSheet(), QStringLiteral("QLabel { color: #a86b6d; }"));
        QCOMPARE(label->property(WorkspacePreview::kOriginalQssProperty).toString(),
                 QStringLiteral("QLabel { color: #ff4444; }"));
    }

    void showFilterThemesDialogs()
    {
        WorkspacePreview::setEnabledForTest(true);
        QWidget dialog;
        dialog.setStyleSheet(QStringLiteral("background: #0f0f1a;"));
        WorkspacePreview::themeOnShow(&dialog);
        QCOMPARE(dialog.styleSheet(), QStringLiteral("background: #0f0f1a;"));
        dialog.show();
        QCOMPARE(dialog.styleSheet(), QStringLiteral("background: #08080a;"));
    }

    void showFilterInstallsOnce()
    {
        WorkspacePreview::setEnabledForTest(true);
        QWidget dialog;
        WorkspacePreview::themeOnShow(&dialog);
        WorkspacePreview::themeOnShow(&dialog);
        WorkspacePreview::themeOnShow(&dialog);
        int filters = 0;
        for (QObject* child : dialog.children()) {
            if (child->objectName() == QLatin1String("workspacePreviewShowFilter")) { ++filters; }
        }
        QCOMPARE(filters, 1);
    }

    void componentRaisedButton()
    {
        const QString out = WC::componentize(QStringLiteral(
            "QPushButton { background: #1a1a1e; border: 1px solid #2c2c31; border-radius: 3px;"
            " color: #dcdce1; padding: 2px 4px; }"), nullptr);
        QVERIFY2(out.contains(WC::raisedFill()), qPrintable(out));
        QVERIFY(out.contains(QStringLiteral("border-top-color: #38383e")));
        QVERIFY(out.contains(QStringLiteral("border-bottom-color: #1f1f23")));
        // round(22 x 7 / 26) at the default height without an owner.
        QVERIFY2(out.contains(QStringLiteral("border-radius: 6px")), qPrintable(out));
        // Padding and border width stay.
        QVERIFY(out.contains(QStringLiteral("padding: 2px 4px")));
        QVERIFY(out.contains(QStringLiteral("border: 1px solid #2c2c31")));
    }

    void componentCheckedIsGlass()
    {
        const QString out = WC::componentize(QStringLiteral(
            "QPushButton:checked { background: #3576e0; color: #ffffff; border: 1px solid #2a5fbe; }"),
            nullptr);
        QVERIFY2(out.contains(WC::glassFill()), qPrintable(out));
        QVERIFY(out.contains(QStringLiteral("color: #dbe9f8")));
        QVERIFY(out.contains(QStringLiteral("border-color: #3d78b0")));
    }

    void componentKeepsSemanticAndClearBackgrounds()
    {
        // A lit green checked button is JJ's raised bright green now; the
        // other semantic and clear backgrounds stay as they are.
        const QString green = QStringLiteral(
            "QPushButton:checked { background: #1c3a2a; color: #6fa384; border: 1px solid #2c5c44; }");
        const QString lit = WC::componentize(green, nullptr);
        QVERIFY2(lit.contains(QStringLiteral("stop:0 #306247")), qPrintable(lit));
        QVERIFY(lit.contains(QStringLiteral("color: #bbe1c6")));
        const QString clear = QStringLiteral("QPushButton { background: transparent; border: none; }");
        QCOMPARE(WC::componentize(clear, nullptr), clear);
        const QString slice = QStringLiteral("QPushButton { background: #00d4ff; }");
        QCOMPARE(WC::componentize(slice, nullptr), slice);
        const QString translucent = QStringLiteral("QPushButton { background: rgba(20, 30, 45, 240); }");
        QCOMPARE(WC::componentize(translucent, nullptr), translucent);
    }

    void componentFieldsAndChipsAreSunken()
    {
        const QString field = WC::componentize(QStringLiteral(
            "QLineEdit { background: #1a1a1e; border: 1px solid #2c2c31; border-radius: 3px; }"),
            nullptr);
        QVERIFY2(field.contains(QStringLiteral("background: #050507")), qPrintable(field));
        QVERIFY(field.contains(QStringLiteral("border-top-color: rgba(0, 0, 0, 160)")));
        const QString chip = WC::componentize(QStringLiteral(
            "QLabel { background: #050507; border: 1px solid #232329; color: #dcdce1; }"), nullptr);
        QVERIFY2(chip.contains(WC::fieldFill()), qPrintable(chip));
        // A label without a border is text, not a chip.
        const QString text = QStringLiteral("QLabel { background: #0c0c0e; color: #dcdce1; }");
        QCOMPARE(WC::componentize(text, nullptr), text);
    }

    // A chip's text stays readable on the dark chip fill: dark text chosen
    // for a light fill (the HL2 I2C read boxes, black on white) takes the
    // chip's text colour, and dim text a dark fill already carries (the
    // modulation monitor's "NO CARRIER" lamp) keeps its colour.
    void componentChipTextFollowsTheRuleFill()
    {
        const QString light = WC::componentize(QStringLiteral(
            "QLabel { background: white; border: 1px solid #304050; color: black; }"), nullptr);
        QVERIFY2(light.contains(WC::fieldFill()), qPrintable(light));
        QVERIFY2(!light.contains(QStringLiteral("color: black")), qPrintable(light));
        const QString dim = WC::componentize(QStringLiteral(
            "QLabel { background: #0c0c0e; border: 1px solid #232329; color: #58585e; }"), nullptr);
        QVERIFY2(dim.contains(WC::fieldFill()), qPrintable(dim));
        QVERIFY2(dim.contains(QStringLiteral("color: #58585e")), qPrintable(dim));

        // The lamp's own sheet (ModMonitorApplet::setLamp) through the
        // subtree pass: the inactive text stays the inactive colour.
        WorkspacePreview::setEnabledForTest(true);
        QWidget root;
        auto* lamp = new QLabel(QStringLiteral("NO CARRIER"), &root);
        lamp->setStyleSheet(QStringLiteral(
            "QLabel { background:#0a0a18; color:#405060; border:1px solid #1e2e3e; "
            "border-radius:2px; font-weight:bold; font-size:10px; padding:1px 3px; }"));
        WorkspacePreview::themeSubtree(&root);
        QVERIFY2(lamp->styleSheet().contains(QStringLiteral("#58585e")), qPrintable(lamp->styleSheet()));
        QVERIFY2(!lamp->styleSheet().contains(QStringLiteral("#dcdce1")), qPrintable(lamp->styleSheet()));
    }

    void componentSliderIsLongpaths()
    {
        const QString out = WC::componentize(QStringLiteral(
            "QSlider::groove:horizontal { height: 4px; background: #1f1f23; border-radius: 2px; }"
            "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -3px 0;"
            " background: #4a7ba8; border-radius: 5px; }"), nullptr);
        // Longpath's groove and handle, and no filled side. The groove is
        // JJ's D29 track (button fill, text-inactive rim on all four sides)
        // with the disabled track appended (inset, border-muted rim).
        QVERIFY2(out.contains(QStringLiteral("background: #1a1a1e; border-radius: 3px; border: 1px solid #58585e;")),
                 qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral("QSlider::groove:horizontal:disabled { background: #050507;"
                                             " border-color: #3d3d41;")),
                 qPrintable(out));
        QVERIFY(out.contains(WC::raisedFill()));
        QVERIFY(out.contains(QStringLiteral("width: 10px; height: 14px; margin: -6px 0")));
        QVERIFY(!out.contains(QStringLiteral("QSlider::sub-page")));
    }

    void componentIsIdempotent()
    {
        const QString src = QStringLiteral(
            "QPushButton { background: #1a2a3a; border: 1px solid #205070; border-radius: 3px; }"
            "QPushButton:hover { background: #203040; }"
            "QPushButton:pressed { background: #00b4d8; color: #0f0f1a; }"
            "QPushButton:checked { background: #0070c0; color: #ffffff; border: 1px solid #0090e0; }"
            "QPushButton:disabled { background: #1a1a2a; color: #556070; }"
            "QLineEdit, QSpinBox { background: #1a2a3a; border: 1px solid #304050; }"
            "QSlider::groove:horizontal { height: 4px; background: #203040; }"
            "QSlider::handle:horizontal { width: 10px; background: #00b4d8; }");
        WorkspacePreview::setEnabledForTest(true);
        const QString once = WorkspacePreview::lookQss(src);
        QVERIFY(once != src);
        QCOMPARE(WorkspacePreview::lookQss(once), once);
        // Every value the treatment writes is a palette value, never a
        // legacy key the translator would move again.
        for (const QT::Rule& rule : QT::defaultTable()) {
            QVERIFY2(!WC::componentize(once, nullptr).contains(rule.legacyHex), qPrintable(rule.legacyHex));
        }
    }

    void componentSwitchOffIsIdentity()
    {
        const QString src = QStringLiteral("QPushButton { background: #1a2a3a; }");
        QCOMPARE(WorkspacePreview::lookQss(src), src);
    }

    // Component geometry stays the same at an equal effective font size.
    // Small inputs still pass through the real look, which must floor them
    // to 11 px without changing their other font attributes.
    void componentKeepsSizeHints()
    {
        WorkspacePreview::setEnabledForTest(true);
        const QString btnQss = QStringLiteral(
            "QPushButton { background: #1a2a3a; border: 1px solid #205070; border-radius: 3px;"
            " color: #c8d8e8; font-size: 10px; font-weight: bold; padding: 2px 4px; }"
            "QPushButton:checked { background: #0070c0; color: #ffffff; border: 1px solid #0090e0; }");
        QPushButton a(QStringLiteral("TX I/Q"));
        QPushButton b(QStringLiteral("TX I/Q"));
        QString referenceBtnQss = btnQss;
        referenceBtnQss.replace(QStringLiteral("font-size: 10px"), QStringLiteral("font-size: 11px"));
        a.setStyleSheet(referenceBtnQss);
        b.setStyleSheet(WorkspacePreview::lookQss(btnQss, &b));
        a.ensurePolished();
        b.ensurePolished();
        QCOMPARE(b.font().pixelSize(), 11);
        QCOMPARE(b.font(), a.font());
        QVERIFY(b.font().bold());
        QCOMPARE(b.sizeHint(), a.sizeHint());

        const QString fieldQss = QStringLiteral(
            "QSpinBox { background: #1a2a3a; border: 1px solid #304050;"
            " border-radius: 3px; color: #c8d8e8; font-size: 12px; padding: 2px 4px; }");
        QSpinBox s1;
        QSpinBox s2;
        s1.setStyleSheet(fieldQss);
        s2.setStyleSheet(WorkspacePreview::lookQss(fieldQss, &s2));
        s1.ensurePolished();
        s2.ensurePolished();
        QCOMPARE(s2.font().pixelSize(), 12);
        QCOMPARE(s2.font(), s1.font());
        QCOMPARE(s2.sizeHint(), s1.sizeHint());

        // The shared spin box builder's shape, with styled buttons.
        const QString spinQss = QStringLiteral(
            "QAbstractSpinBox { background: #1a2a3a; color: #c8d8e8;"
            " border: 1px solid #205070; border-radius: 3px; padding: 1px 4px; font-size: 10px; }"
            "QAbstractSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right;"
            " width: 16px; border-left: 1px solid #205070; background: #1a2a3a; }"
            "QAbstractSpinBox::down-button { subcontrol-origin: border;"
            " subcontrol-position: bottom right; width: 16px; border-left: 1px solid #205070;"
            " border-top: 1px solid #205070; background: #1a2a3a; }"
            "QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover {"
            " background: #203040; }");
        QSpinBox t1;
        QSpinBox t2;
        QString referenceSpinQss = spinQss;
        referenceSpinQss.replace(QStringLiteral("font-size: 10px"), QStringLiteral("font-size: 11px"));
        t1.setStyleSheet(referenceSpinQss);
        t2.setStyleSheet(WorkspacePreview::lookQss(spinQss, &t2));
        t1.ensurePolished();
        t2.ensurePolished();
        QCOMPARE(t2.font().pixelSize(), 11);
        QCOMPARE(t2.font(), t1.font());
        QCOMPARE(t2.sizeHint(), t1.sizeHint());

        const QString editQss = QStringLiteral(
            "QLineEdit { background: #1a2a3a; border: 1px solid #304050;"
            " border-radius: 3px; color: #c8d8e8; font-size: 12px; padding: 2px 4px; }");
        QLineEdit e1;
        QLineEdit e2;
        e1.setStyleSheet(editQss);
        e2.setStyleSheet(WorkspacePreview::lookQss(editQss, &e2));
        e1.ensurePolished();
        e2.ensurePolished();
        QCOMPARE(e2.font().pixelSize(), 12);
        QCOMPARE(e2.font(), e1.font());
        QCOMPARE(e2.sizeHint(), e1.sizeHint());

        const QString comboQss = QStringLiteral(
            "QComboBox { background: #1a2a3a; border: 1px solid #304050;"
            " border-radius: 3px; color: #c8d8e8; font-size: 12px; padding: 2px 4px; }");
        QComboBox c1;
        QComboBox c2;
        c1.addItem(QStringLiteral("Med"));
        c2.addItem(QStringLiteral("Med"));
        c1.setStyleSheet(comboQss);
        c2.setStyleSheet(WorkspacePreview::lookQss(comboQss, &c2));
        c1.ensurePolished();
        c2.ensurePolished();
        QCOMPARE(c2.font().pixelSize(), 12);
        QCOMPARE(c2.font(), c1.font());
        QCOMPARE(c2.sizeHint(), c1.sizeHint());

        const QString sliderQss = QStringLiteral(
            "QSlider::groove:horizontal { height: 4px; background: #203040; border-radius: 2px; }"
            "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -3px 0;"
            " background: #00b4d8; border-radius: 5px; }");
        // The slider takes Longpath's handle (JJ's decision): its size hint
        // is Longpath's slider's, not today's.
        const QString longpathSliderQss = QStringLiteral(
            "QSlider::groove:horizontal { height: 4px; background: #050507; border: 1px solid #2c2c31;"
            " border-radius: 3px; }"
            "QSlider::handle:horizontal { width: 10px; height: 14px; margin: -6px 0;"
            " background: #1a1a1e; border: 1px solid #4a7ba8; border-radius: 5px; }");
        QSlider l1(Qt::Horizontal);
        QSlider l2(Qt::Horizontal);
        l1.setStyleSheet(longpathSliderQss);
        l2.setStyleSheet(WorkspacePreview::lookQss(sliderQss, &l2));
        QCOMPARE(l2.sizeHint(), l1.sizeHint());
    }

    // Rendered, not assumed: normal, pressed and checked differ, and the
    // treated states win over the original rules.
    void componentStatesRenderDistinct()
    {
        WorkspacePreview::setEnabledForTest(true);
        const QString qss = QStringLiteral(
            "QPushButton { background: #1a2a3a; border: 1px solid #205070; border-radius: 3px; }"
            "QPushButton:pressed { background: #00b4d8; }"
            "QPushButton:checked { background: #0070c0; color: #ffffff; }");
        QPushButton b;
        b.setCheckable(true);
        b.resize(60, 22);
        b.setStyleSheet(WorkspacePreview::lookQss(qss, &b));
        const QColor normal = centreColour(b);
        const QColor normalTop = upperColour(b);
        b.setDown(true);
        const QColor pressed = centreColour(b);
        const QColor pressedTop = upperColour(b);
        b.setDown(false);
        b.setChecked(true);
        const QColor checked = centreColour(b);
        // Normal is the raised grey, checked the glass blue.
        QVERIFY2(normal.blue() - normal.red() < 8, qPrintable(normal.name()));
        QVERIFY2(checked.blue() - checked.red() > 40, qPrintable(checked.name()));
        Q_UNUSED(pressed);
        // Raised is lighter at the top, pressed darker.
        QVERIFY2(normalTop.lightness() > pressedTop.lightness() + 4,
                 qPrintable(normalTop.name() + pressedTop.name()));
        QVERIFY2(distance(normal, checked) > 60, qPrintable(normal.name() + checked.name()));
    }

    // A brace-less sheet on a checkable button: Qt treats it as a "*"
    // rule, and the state rules the treatment adds win over it.
    void componentSelectorlessCheckableButton()
    {
        WorkspacePreview::setEnabledForTest(true);
        QPushButton b;
        b.setCheckable(true);
        b.resize(60, 22);
        b.setStyleSheet(WorkspacePreview::lookQss(
            QStringLiteral("background: #1a2a3a; border: 1px solid #205070; color: #c8d8e8;"), &b));
        QVERIFY(b.styleSheet().startsWith(QStringLiteral("* {")));
        const QColor normal = centreColour(b);
        b.setChecked(true);
        const QColor checked = centreColour(b);
        QVERIFY2(distance(normal, checked) > 60, qPrintable(normal.name() + checked.name()));
        // A higher-specificity original rule keeps its meaning: an
        // id-qualified green checked state stays green.
        QPushButton g;
        g.setObjectName(QStringLiteral("g"));
        g.setCheckable(true);
        g.setChecked(true);
        g.resize(60, 22);
        g.setStyleSheet(WorkspacePreview::lookQss(QStringLiteral(
            "QPushButton#g { background: #1a2a3a; border: 1px solid #205070; }"
            "QPushButton#g:checked { background: #006040; }"), &g));
        const QColor green = centreColour(g);
        QVERIFY2(green.green() > green.blue() && green.green() > green.red(), qPrintable(green.name()));
    }

    void cleanupTestCase()
    {
        WorkspacePreview::setEnabledForTest(false);
    }
};

QTEST_MAIN(TestWorkspaceQssTranslator)
#include "tst_workspace_qss_translator.moc"
