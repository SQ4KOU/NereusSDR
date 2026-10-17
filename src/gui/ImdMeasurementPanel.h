#pragma once
// no-port-check: NereusSDR-original cached GUI measurement presentation; no analyzer.
// Modification history (NereusSDR):
//   2026-10-13: J.J. Boyd (KG4VCF), OpenAI Codex; rail and optional bench inspector.
#include "gui/ImdMeasurementFrame.h"
#include "gui/NereusGuiExport.h"
#include <QString>
#include <QWidget>
#include <array>
class QLabel;
class QPushButton;
class QGridLayout;
class QResizeEvent;
class QKeyEvent;
namespace NereusSDR {
class NEREUS_GUI_EXPORT ImdMeasurementPanel : public QWidget {
    Q_OBJECT
public:
    enum class Presentation { Rail, Inspector, Window };
    explicit ImdMeasurementPanel(QWidget* parent = nullptr);
    void setPresentation(Presentation presentation);
    void setFrame(const ImdMeasurementFrame& frame);
    void setSelectedProduct(ImdProduct product);
    void setGuidesVisible(bool visible);
    void setInspectorVisible(bool visible);
    void setDisplayDuplexCapability(bool available, const QString& unavailableReason = {});
    ImdProduct selectedProduct() const { return m_selected; }
    const ImdMeasurementFrame& frame() const { return m_frame; }
    static QString statusText(ImdStatus status);
signals:
    void productSelected(ImdProduct product);
    void guidesVisibleChanged(bool visible);
    void inspectorVisibleChanged(bool visible);
    void hideMeasurementsRequested();
    void enableDisplayDuplexRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
private:
    void refresh();
    void reflow();
    void chooseProduct(ImdProduct product);
    Presentation m_presentation{Presentation::Rail};
    ImdMeasurementFrame m_frame;
    ImdProduct m_selected{ImdProduct::ThirdLower};
    QLabel* m_status;
    QLabel* m_source;
    QLabel* m_third;
    QLabel* m_fifth;
    QLabel* m_selection;
    QLabel* m_reference;
    QLabel* m_diagnostics;
    QWidget* m_details;
    QWidget* m_controls;
    QPushButton* m_enableDuplex;
    QPushButton* m_guides;
    QPushButton* m_inspector;
    QPushButton* m_hideMeasurements;
    QGridLayout* m_summaryLayout;
    QGridLayout* m_pickerLayout;
    std::array<QPushButton*, 6> m_products;
    std::array<std::array<QLabel*, 3>, 6> m_values;
    bool m_displayDuplexAvailable{false};
    QString m_displayDuplexUnavailableReason;
};
} // namespace NereusSDR
