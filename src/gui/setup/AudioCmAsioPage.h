#pragma once

#include "gui/SetupPage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace NereusSDR {

// Windows-only, operator-selected cmASIO Speakers output.
class AudioCmAsioPage final : public SetupPage {
    Q_OBJECT
public:
    explicit AudioCmAsioPage(RadioModel* model, QWidget* parent = nullptr);

private:
    void rescanDrivers();
    void saveSelection();
    void updateStatus(const QString& note = {});
    QCheckBox* m_enabled{};
    QCheckBox* m_txEnabled{};
    QComboBox* m_inputPair{};
    QComboBox* m_inputMode{};
    QComboBox* m_driver{};
    QComboBox* m_outputPair{};
    QComboBox* m_block{};
    QSpinBox* m_blockNum{};
    QLabel* m_status{};
    QLabel* m_dll{};
    bool m_loading{false};
};

} // namespace NereusSDR
