#pragma once
#include <QWidget>

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

// Stub — populated in Phase 3I Task 19.
class RadioInfoTab : public QWidget {
    Q_OBJECT
public:
    explicit RadioInfoTab(RadioModel* model, QWidget* parent = nullptr);
    // Called by HardwarePage when the connected radio changes.
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
private:
    RadioModel* m_model{nullptr};
};

} // namespace NereusSDR
