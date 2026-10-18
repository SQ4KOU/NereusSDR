#pragma once

// =================================================================
// src/gui/PanModels.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. A window's own per-band model for
// each of its pans; Thetis has one window and one display per receiver.
//
// Display pill plan Task 9 (D29, D19, R-DSP-27, R-DSP-38, R-DSP-49).
// PanadapterModel holds the per-band dB Max and Min, the per-band noise
// estimate and the per-band 3D Floor. This class gives every pan of one
// window a PanadapterModel of its own and wires it to that pan's
// SpectrumWidget, so the pan's band follows the pan's centre frequency and
// the band's stored values reach the pan.
//
// The models are the window's. They are not RadioModel::panadapters():
// that list is mirrored on the station link under an index-only pan name
// that cannot tell two windows apart, and the Core has no pan object.
//
// Modification history (NereusSDR):
//   2026-10-10 - Created. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
// =================================================================

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include "gui/NereusGuiExport.h"

#include <map>
#include <memory>

namespace NereusSDR {

class PanadapterModel;
class SpectrumWidget;

class NEREUS_GUI_EXPORT PanModels : public QObject {
    Q_OBJECT

public:
    explicit PanModels(QObject* parent = nullptr);
    ~PanModels() override;

    // The pan's model. Created (parented to this object) and wired to
    // `widget` on the first call; a later call for the same pan and widget
    // returns the same model. A call with a different widget (the pan was
    // rebuilt under its old id) retires the old model first. Null when
    // panId is empty or widget is null.
    //
    // The approved native picture step is owned by SpectrumWidget. The
    // historical dB Step wiring item below is superseded by that scope.
    // The wiring:
    //   - the model's centre follows the widget's receive centre, so
    //     PanadapterModel::band() is the pan's band;
    //   - the model's range (the band's stored dB Max and Min) sets the
    //     widget's receive range, once here and on every change. This is
    //     the only writer of the widget's dB range from settings;
    //   - a band change starts the pan's noise-floor fast attack;
    //   - the model's dB Step (one for every pan and band) sets the spacing
    //     of the widget's horizontal dB grid lines, once here and on every
    //     change;
    //   - the per-band 3D Floor is recalled and saved
    //     (MainWindow::wireDss3DFloorRecallForTest, the one bridge).
    PanadapterModel* ensure(const QString& panId, SpectrumWidget* widget);

    // Null when the pan has no model.
    PanadapterModel* model(const QString& panId) const;

    // Deletes the pan's model; every connection ensure() made goes with it.
    void retire(const QString& panId);

    // Every model, in pan id order.
    QList<PanadapterModel*> all() const;

    // A per-band dB Max or Min setting changed underneath (a value from
    // the Core, or Setup): every model re-reads it
    // (PanadapterModel::applyStationGridSetting).
    void applyStationGridSetting(const QString& key);

signals:
    // The pan's band, or its dB Max or Min, changed.
    void bandGridChanged(const QString& panId);

private:
    struct Entry {
        std::unique_ptr<PanadapterModel> model;
        QPointer<SpectrumWidget> widget;
    };
    std::map<QString, Entry> m_entries;
};

} // namespace NereusSDR
