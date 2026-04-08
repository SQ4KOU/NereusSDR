#pragma once

#include <QMainWindow>

namespace NereusSDR {

class RadioModel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUI();
    void buildMenuBar();
    void applyDarkTheme();

    RadioModel* m_radioModel{nullptr};
};

} // namespace NereusSDR
