#pragma once
#include <QWidget>

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

// Stub — populated in Phase 3I Task 20.
class PureSignalTab : public QWidget {
    Q_OBJECT
public:
    explicit PureSignalTab(RadioModel* model, QWidget* parent = nullptr);
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
private:
    RadioModel* m_model{nullptr};
};

} // namespace NereusSDR
