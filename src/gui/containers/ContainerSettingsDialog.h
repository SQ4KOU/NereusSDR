#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QSplitter;
class QFormLayout;
class QVBoxLayout;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QLabel;

namespace NereusSDR {

class ContainerWidget;
class MeterWidget;
class MeterItem;
class BarItem;
class NeedleItem;
class TextItem;
class ScaleItem;
class SolidColourItem;
class LEDItem;

class ContainerSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ContainerSettingsDialog(ContainerWidget* container,
                                     QWidget* parent = nullptr);
    ~ContainerSettingsDialog() override;

private slots:
    void onItemSelectionChanged();
    void onAddItem();
    void onRemoveItem();
    void onMoveItemUp();
    void onMoveItemDown();
    void onLoadPreset();
    void onExport();
    void onImport();

private:
    void buildLayout();
    void buildLeftPanel(QWidget* parent);
    void buildCenterPanel(QWidget* parent);
    void buildRightPanel(QWidget* parent);
    void buildContainerPropertiesSection(QVBoxLayout* parentLayout);
    void buildButtonBar();

    static MeterItem* createItemFromSerialized(const QString& data);
    void refreshItemList();
    static QString typeTagDisplayName(const QString& tag);

    void addNewItem(const QString& typeTag);
    static MeterItem* createDefaultItem(const QString& typeTag);
    void loadPresetByName(const QString& name);

    void populateItemList();
    void updatePreview();
    void applyToContainer();

    // Common property editor (Task 4)
    void buildCommonPropsPage();
    void loadCommonProperties(int row);
    void saveCommonProperties(int row);
    void populateBindingCombo();
    QWidget* buildTypeSpecificEditor(MeterItem* item);

    // Type-specific editors (Task 5)
    QWidget* buildBarItemEditor(BarItem* item);
    QWidget* buildNeedleItemEditor(NeedleItem* item);
    QWidget* buildTextItemEditor(TextItem* item);
    QWidget* buildScaleItemEditor(ScaleItem* item);
    QWidget* buildSolidItemEditor(SolidColourItem* item);
    QWidget* buildLedItemEditor(LEDItem* item);

    ContainerWidget* m_container{nullptr};
    QVector<MeterItem*> m_workingItems;

    QSplitter* m_splitter{nullptr};

    // Left panel
    QListWidget* m_itemList{nullptr};
    QPushButton* m_btnAdd{nullptr};
    QPushButton* m_btnRemove{nullptr};
    QPushButton* m_btnMoveUp{nullptr};
    QPushButton* m_btnMoveDown{nullptr};

    // Center panel
    QStackedWidget* m_propertyStack{nullptr};
    QWidget* m_emptyPage{nullptr};

    // Common property controls
    QWidget* m_commonPropsPage{nullptr};
    QDoubleSpinBox* m_propX{nullptr};
    QDoubleSpinBox* m_propY{nullptr};
    QDoubleSpinBox* m_propW{nullptr};
    QDoubleSpinBox* m_propH{nullptr};
    QSpinBox* m_propZOrder{nullptr};
    QComboBox* m_propBinding{nullptr};

    // Type-specific property area
    QWidget* m_typePropsContainer{nullptr};
    QVBoxLayout* m_typePropsLayout{nullptr};
    QWidget* m_currentTypeEditor{nullptr};

    // Right panel
    MeterWidget* m_previewWidget{nullptr};

    // Container properties
    QLineEdit* m_titleEdit{nullptr};
    QPushButton* m_bgColorBtn{nullptr};
    QCheckBox* m_borderCheck{nullptr};
    QComboBox* m_rxSourceCombo{nullptr};
    QCheckBox* m_showOnRxCheck{nullptr};
    QCheckBox* m_showOnTxCheck{nullptr};

    // Bottom buttons
    QPushButton* m_btnPreset{nullptr};
    QPushButton* m_btnImport{nullptr};
    QPushButton* m_btnExport{nullptr};
    QPushButton* m_btnOk{nullptr};
    QPushButton* m_btnCancel{nullptr};
    QPushButton* m_btnApply{nullptr};
};

} // namespace NereusSDR
