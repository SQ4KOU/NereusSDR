#include <QtTest/QtTest>
#include "gui/widgets/VfoWidget.h"
#include <QAbstractButton>
#include <QSlider>
#include <QComboBox>

using namespace NereusSDR;

class TestVfoTooltipCoverage : public QObject {
    Q_OBJECT
private slots:
    void everyEnabledControlHasTooltip()
    {
        VfoWidget w;

        auto check = [](QWidget* child) -> QString {
            if (!child->isEnabled()) {
                return {};
            }
            const QString tt = child->toolTip();
            if (tt.isEmpty()) {
                return QStringLiteral("%1 (%2) has no tooltip")
                    .arg(child->objectName(),
                         child->metaObject()->className());
            }
            return {};
        };

        QStringList failures;
        for (auto* btn : w.findChildren<QAbstractButton*>()) {
            if (auto e = check(btn); !e.isEmpty()) {
                failures << e;
            }
        }
        for (auto* sl : w.findChildren<QSlider*>()) {
            if (auto e = check(sl); !e.isEmpty()) {
                failures << e;
            }
        }
        for (auto* cmb : w.findChildren<QComboBox*>()) {
            if (auto e = check(cmb); !e.isEmpty()) {
                failures << e;
            }
        }

        QVERIFY2(failures.isEmpty(),
                  qPrintable(failures.join(QStringLiteral("\n"))));
    }
};

QTEST_MAIN(TestVfoTooltipCoverage)
#include "tst_vfo_tooltip_coverage.moc"
