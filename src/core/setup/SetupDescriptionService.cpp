// no-port-check: NereusSDR-original Setup description transport.
#include "core/setup/SetupDescriptionService.h"
#include "core/session/SessionCommandDispatcher.h"
#include "core/session/MirrorSchema.h"
#include "core/session/MirrorPolicy.h"
#include "models/StationTciModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "models/NotchModel.h"
#include "models/Band.h"
#include "core/accessories/AlexAntennaFacade.h"
#include "core/session/TransmitStateFacade.h"
#include "core/SkuUiProfile.h"
#include "core/ControlRanges.h"
#include "core/settings/SettingsScope.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <cmath>

static void initializeSetupResources()
{
    Q_INIT_RESOURCE(setup);
}

namespace NereusSDR {
namespace {

QJsonObject expectedAntennaRowsTable(bool tx, HPSDRModel model)
{
    const SkuUiProfile sku = skuUiProfileFor(model);
    QJsonArray columns;
    for (int antenna = 1; antenna <= 3; ++antenna) {
        const QString id = (tx ? QStringLiteral("tx%1") : QStringLiteral("rx%1"))
                               .arg(antenna);
        columns.append(QJsonObject{{QStringLiteral("id"), id},
                                   {QStringLiteral("field"), tx ? QStringLiteral("tx")
                                                               : QStringLiteral("rx")},
                                   {QStringLiteral("antenna"), antenna},
                                   {QStringLiteral("label"), tx ? QStringLiteral("Ant %1").arg(antenna)
                                                                : QString::number(antenna)}});
    }
    if (!tx) {
        for (int antenna = 1; antenna <= 3; ++antenna) {
            columns.append(QJsonObject{{QStringLiteral("id"),
                                        QStringLiteral("rxOnly%1").arg(antenna)},
                                       {QStringLiteral("field"), QStringLiteral("rxOnly")},
                                       {QStringLiteral("antenna"), antenna},
                                       {QStringLiteral("label"), sku.rxOnlyLabels[antenna - 1]}});
        }
    }
    QJsonArray rows;
    // One row per entry of the antenna lists, in their order: 160m .. XVTR,
    // then 2 m (R-IOS-26). A row carries its band's number (2 m is 27).
    for (int slot = 0; slot < AlexAntennaFacade::kBandCount; ++slot) {
        const int band = static_cast<int>(bandFromPerBandStateSlot(slot));
        const QString label = bandLabel(static_cast<Band>(band));
        QJsonArray cells;
        for (const QJsonValue& rawColumn : columns) {
            const QJsonObject column = rawColumn.toObject();
            const int antenna = column.value(QStringLiteral("antenna")).toInt();
            const QString field = column.value(QStringLiteral("field")).toString();
            const QString tooltip = field == QLatin1String("tx")
                ? QStringLiteral("TX Ant %1 for %2").arg(antenna).arg(label)
                : field == QLatin1String("rx")
                ? QStringLiteral("RX1 Ant %1 for %2").arg(antenna).arg(label)
                : QStringLiteral("RX-only %1 for %2")
                      .arg(sku.rxOnlyLabels[antenna - 1], label);
            cells.append(QJsonObject{{QStringLiteral("column"), column.value(QStringLiteral("id"))},
                                     {QStringLiteral("tooltip"), tooltip}});
        }
        rows.append(QJsonObject{{QStringLiteral("band"), band},
                                {QStringLiteral("label"), label},
                                {QStringLiteral("cells"), cells}});
    }
    QJsonObject gate{{QStringLiteral("capability"), QStringLiteral("radioAntennaRowsVersion")},
                     {QStringLiteral("min"), 1}};
    if (tx) { gate.insert(QStringLiteral("offAir"), true); }
    QJsonObject control{{QStringLiteral("id"), tx ? QStringLiteral("hardware.antenna.txRows")
                                                   : QStringLiteral("hardware.antenna.rxRows")},
                        {QStringLiteral("label"), tx ? QStringLiteral("TX Antenna per Band")
                                                      : QStringLiteral("RX1 / RX2 Antenna per Band")},
                        {QStringLiteral("tooltip"), QString()},
                        {QStringLiteral("kind"), QStringLiteral("table")},
                        {QStringLiteral("binding"), QJsonObject{{QStringLiteral("antennaRows"),
                            QJsonObject{{QStringLiteral("object"), QStringLiteral("alexAntennas")},
                                        {QStringLiteral("mode"), tx ? QStringLiteral("tx")
                                                                     : QStringLiteral("rx")}}}}},
                        {QStringLiteral("applies"), QStringLiteral("live")},
                        {QStringLiteral("requiresDescriptionVersion"), 6},
                        {QStringLiteral("gate"), gate},
                        {QStringLiteral("rows"), rows},
                        {QStringLiteral("columns"), columns}};
    if (!tx) {
        control.insert(QStringLiteral("columnGroups"), QJsonArray{
            QJsonObject{{QStringLiteral("label"), QStringLiteral("RX1")},
                        {QStringLiteral("columns"), QJsonArray{QStringLiteral("rx1"),
                                                                QStringLiteral("rx2"),
                                                                QStringLiteral("rx3")}}},
            QJsonObject{{QStringLiteral("label"), QStringLiteral("RX-only")},
                        {QStringLiteral("columns"), QJsonArray{QStringLiteral("rxOnly1"),
                                                                QStringLiteral("rxOnly2"),
                                                                QStringLiteral("rxOnly3")}}}});
    }
    return control;
}

QJsonObject expectedSettingsHygienePanel()
{
    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("diagnostics.settingsValidation.health")},
        {QStringLiteral("label"), QStringLiteral("Validation Issues")},
        {QStringLiteral("tooltip"), QString()},
        {QStringLiteral("kind"), QStringLiteral("settingsHygiene")},
        {QStringLiteral("requiresDescriptionVersion"), 3},
        {QStringLiteral("binding"), QJsonObject{{QStringLiteral("settingsHygiene"),
            QJsonObject{{QStringLiteral("version"), 1}}}}},
        {QStringLiteral("applies"), QStringLiteral("live")},
        {QStringLiteral("gate"), QJsonObject{
            {QStringLiteral("capability"), QStringLiteral("settingsHygieneVersion")},
            {QStringLiteral("min"), 1}}},
        {QStringLiteral("actions"), QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("validate")},
                        {QStringLiteral("label"), QStringLiteral("Re-validate")}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("reset")},
                        {QStringLiteral("label"), QStringLiteral("Reset to Defaults")},
                        {QStringLiteral("enabled"), false},
                        {QStringLiteral("reason"), QStringLiteral(
                            "Reset to defaults is not available on this Core.")}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("forget")},
                        {QStringLiteral("label"), QStringLiteral("Forget This Radio")},
                        {QStringLiteral("paired"), true},
                        {QStringLiteral("offAir"), true},
                        {QStringLiteral("confirmation"), QJsonObject{
                            {QStringLiteral("title"), QStringLiteral("Forget Radio")},
                            {QStringLiteral("message"), QStringLiteral(
                                "Forget all settings for this radio?")},
                            {QStringLiteral("default"), QStringLiteral("cancel")}}}}}}};
}

bool validDiagnosticsEnvelope(const QJsonObject& root)
{
    const QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
    if (root.size() != 3 || root.value(QStringLiteral("version")) != QJsonValue(3)
        || root.value(QStringLiteral("category")) != QJsonValue(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("diagnostics")},
            {QStringLiteral("title"), QStringLiteral("Diagnostics")},
            {QStringLiteral("where"), QStringLiteral("station")},
            {QStringLiteral("coverage"), QStringLiteral("partial")}})
        || pages.size() != 1) {
        return false;
    }
    const QJsonObject page = pages.first().toObject();
    const QJsonArray sections = page.value(QStringLiteral("sections")).toArray();
    if (page.size() != 5
        || page.value(QStringLiteral("id")) != QJsonValue(QStringLiteral("diagnostics.settingsValidation"))
        || page.value(QStringLiteral("title")) != QJsonValue(QStringLiteral("Settings Validation"))
        || page.value(QStringLiteral("where")) != QJsonValue(QStringLiteral("station"))
        || page.value(QStringLiteral("coverage")) != QJsonValue(QStringLiteral("partial"))
        || sections.size() != 1) {
        return false;
    }
    const QJsonObject section = sections.first().toObject();
    const QJsonArray controls = section.value(QStringLiteral("controls")).toArray();
    return section.size() == 2
        && section.value(QStringLiteral("title")) == QJsonValue(QStringLiteral("Validation Issues"))
        && controls.size() == 1
        && SetupDescription::validateSettingsHygienePanel(controls.first().toObject());
}

bool validAppearanceEnvelope(const QJsonObject& root)
{
    const QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
    if (root.size() != 3 || root.value(QStringLiteral("version")) != QJsonValue(7)
        || root.value(QStringLiteral("category")) != QJsonValue(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("appearance")},
            {QStringLiteral("title"), QStringLiteral("Appearance")},
            {QStringLiteral("where"), QStringLiteral("phone")},
            {QStringLiteral("coverage"), QStringLiteral("partial")}})
        || pages.size() != 2) { return false; }
    const QJsonObject page = pages.first().toObject();
    const QJsonArray sections = page.value(QStringLiteral("sections")).toArray();
    if (page.size() != 5 || page.value(QStringLiteral("id")) != QJsonValue(QStringLiteral("appearance.colorsTheme"))
        || page.value(QStringLiteral("title")) != QJsonValue(QStringLiteral("Colors & Theme"))
        || page.value(QStringLiteral("where")) != QJsonValue(QStringLiteral("phone"))
        || page.value(QStringLiteral("coverage")) != QJsonValue(QStringLiteral("partial"))
        || sections.size() != 1) { return false; }
    const QJsonObject section = sections.first().toObject();
    if (section.size() != 2
        || section.value(QStringLiteral("title")) != QJsonValue(QStringLiteral("Spectrum"))
        || section.value(QStringLiteral("controls")).toArray().size() != 10) { return false; }
    const QJsonObject meterPage = pages.at(1).toObject();
    const QJsonArray meterSections = meterPage.value(QStringLiteral("sections")).toArray();
    if (meterPage.size() != 5
        || meterPage.value(QStringLiteral("id")) != QJsonValue(QStringLiteral("appearance.meterStyles"))
        || meterPage.value(QStringLiteral("title")) != QJsonValue(QStringLiteral("Meter Styles"))
        || meterPage.value(QStringLiteral("where")) != QJsonValue(QStringLiteral("phone"))
        || meterPage.value(QStringLiteral("coverage")) != QJsonValue(QStringLiteral("partial"))
        || meterSections.size() != 1) { return false; }
    const QJsonObject meterSection = meterSections.first().toObject();
    return meterSection.size() == 2
        && meterSection.value(QStringLiteral("title")) == QJsonValue(QStringLiteral("S-Meter"))
        && meterSection.value(QStringLiteral("controls")).toArray().size() == 3;
}

QString loadCategory(const QString& id, const BoardCapabilities& caps, HPSDRModel model)
{
    QFile resource(QStringLiteral(":/setup/%1.json").arg(id));
    if (!resource.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(resource.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    QJsonObject root = document.object();
    if ((root.value(QStringLiteral("version")).toInt() != 1
         && root.value(QStringLiteral("version")).toInt() != 2
         && !(id == QLatin1String("diagnostics")
              && root.value(QStringLiteral("version")) == QJsonValue(3))
         && !(id == QLatin1String("display")
              && root.value(QStringLiteral("version")) == QJsonValue(11))
         && !(id == QLatin1String("appearance")
              && root.value(QStringLiteral("version")) == QJsonValue(7))
         && !(id == QLatin1String("pa")
              && root.value(QStringLiteral("version")) == QJsonValue(5))
         && !(id == QLatin1String("hardware")
              && root.value(QStringLiteral("version")) == QJsonValue(6)))
        || root.value(QStringLiteral("category")).toObject()
               .value(QStringLiteral("id")).toString() != id
        || root.value(QStringLiteral("pages")).toArray().isEmpty()
        || (id == QLatin1String("diagnostics") && !validDiagnosticsEnvelope(root))
        || (id == QLatin1String("appearance") && !validAppearanceEnvelope(root))) {
        return {};
    }
    QSet<QString> ids;
    int antennaTables = 0;
    for (const QJsonValue& rawPage : root.value(QStringLiteral("pages")).toArray()) {
        const QJsonObject page = rawPage.toObject();
        const QString pageId = page.value(QStringLiteral("id")).toString();
        if (pageId.isEmpty() || ids.contains(pageId)
            || page.value(QStringLiteral("title")).toString().isEmpty()
                    || page.value(QStringLiteral("sections")).toArray().isEmpty()) {
            return {};
        }
        ids.insert(pageId);
        for (const QJsonValue& rawSection : page.value(QStringLiteral("sections")).toArray()) {
            const QJsonObject section = rawSection.toObject();
            if (section.value(QStringLiteral("title")).toString().isEmpty()
                || section.value(QStringLiteral("controls")).toArray().isEmpty()) {
                return {};
            }
            for (const QJsonValue& rawControl : section.value(QStringLiteral("controls")).toArray()) {
                const QJsonObject control = rawControl.toObject();
                const QString controlId = control.value(QStringLiteral("id")).toString();
                if (controlId.isEmpty() || ids.contains(controlId)
                    || control.value(QStringLiteral("label")).toString().isEmpty()
                    || !control.value(QStringLiteral("binding")).isObject()
                    || (id != QLatin1String("display")
                        && ((control.contains(QStringLiteral("options"))
                             && !(id == QLatin1String("appearance")
                                  && SetupDescription::validateAppearanceMeterStyleBinding(control)))
                            || control.contains(QStringLiteral("enabledWhen"))))
                    || (control.contains(QStringLiteral("requiresDescriptionVersion"))
                        && (root.value(QStringLiteral("version")).toInt()
                                < control.value(QStringLiteral("requiresDescriptionVersion")).toInt()
                            || (control.value(QStringLiteral("requiresDescriptionVersion"))
                                    != QJsonValue(2)
                                && control.value(QStringLiteral("requiresDescriptionVersion"))
                                    != QJsonValue(3)
                                && !(id == QLatin1String("display")
                                     && control.value(QStringLiteral("requiresDescriptionVersion"))
                                         == QJsonValue(4))
                                && !(id == QLatin1String("pa")
                                     && control.value(QStringLiteral("requiresDescriptionVersion"))
                                         == QJsonValue(5))
                                && !(id == QLatin1String("hardware")
                                     && control.value(QStringLiteral("requiresDescriptionVersion"))
                                         == QJsonValue(6))
                                && !(id == QLatin1String("appearance")
                                     && control.value(QStringLiteral("requiresDescriptionVersion"))
                                         == QJsonValue(7))
                                && !(id == QLatin1String("display")
                                     && control.value(QStringLiteral("requiresDescriptionVersion"))
                                         == QJsonValue(8))
                                && !(id == QLatin1String("display")
                                     && control.value(QStringLiteral("requiresDescriptionVersion"))
                                         == QJsonValue(9))
                                && !(id == QLatin1String("display")
                                     && control.value(QStringLiteral("requiresDescriptionVersion"))
                                         == QJsonValue(10))
                                && !(id == QLatin1String("display")
                                     && control.value(QStringLiteral("requiresDescriptionVersion"))
                                         == QJsonValue(11)))))
                    || (control.value(QStringLiteral("kind")) == QJsonValue(QStringLiteral("table"))
                        && !((id == QLatin1String("dsp")
                              && SetupDescription::validateTnfTable(control))
                             || (id == QLatin1String("hardware")
                                 && SetupDescription::validateAntennaRowsTable(control))))
                    || (control.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("table"))
                        && (control.value(QStringLiteral("binding")).toObject().contains(QStringLiteral("table"))
                            || control.value(QStringLiteral("binding")).toObject()
                                   .contains(QStringLiteral("antennaRows"))))
                    || !SetupDescription::validateSettingToggleEncoding(control)
                    || (id == QLatin1String("dsp")
                        && !control.value(QStringLiteral("binding")).toObject()
                                .contains(QStringLiteral("property"))
                        && !control.value(QStringLiteral("binding")).toObject()
                                .contains(QStringLiteral("command"))
                        && !control.value(QStringLiteral("binding")).toObject()
                                .contains(QStringLiteral("table"))
                        && !control.value(QStringLiteral("binding")).toObject()
                                .contains(QStringLiteral("setting")))
                    || (id == QLatin1String("dsp")
                        && control.value(QStringLiteral("binding")).toObject()
                               .contains(QStringLiteral("setting"))
                        && !SetupDescription::validateDspSettingBinding(control))
                    || (id == QLatin1String("display")
                        && !SetupDescription::validateDisplaySettingBinding(control)
                        && !SetupDescription::validateDisplayPhoneBinding(control))
                    || (id == QLatin1String("dsp")
                        && control.value(QStringLiteral("binding")).toObject()
                               .contains(QStringLiteral("property"))
                        && !SetupDescription::validateActiveSlicePropertyBinding(control))
                    || (id == QLatin1String("transmit")
                        && !SetupDescription::validateTransmitPropertyBinding(control)
                        && !SetupDescription::validateTransmitSettingBinding(control))
                    || (id == QLatin1String("hardware")
                        && !SetupDescription::validateHardwarePropertyBinding(control)
                        && !SetupDescription::validateAntennaRowsTable(control))
                    || (id == QLatin1String("pa")
                        && !SetupDescription::validatePaReadoutBinding(control)
                        && !SetupDescription::validatePaDriveReadoutBinding(control)
                        && !SetupDescription::validatePaTelemetryReadoutBinding(control)
                        && !SetupDescription::validatePaBypassBinding(control))
                    || (id == QLatin1String("audio")
                        && !SetupDescription::validateAudioPropertyBinding(control))
                    || (id == QLatin1String("appearance")
                        && !SetupDescription::validateAppearanceColourBinding(control)
                        && !SetupDescription::validateAppearanceMeterStyleBinding(control))
                    || (id == QLatin1String("diagnostics")
                        && !SetupDescription::validateSettingsHygienePanel(control))
                    || (control.value(QStringLiteral("binding")).toObject().contains(QStringLiteral("command"))
                        && !SetupDescription::validateCommandBinding(control))) {
                    return {};
                }
                if (id == QLatin1String("hardware")
                    && control.value(QStringLiteral("kind")) == QJsonValue(QStringLiteral("table"))) {
                    ++antennaTables;
                }
                ids.insert(controlId);
            }
        }
    }
    if (id == QLatin1String("hardware") && antennaTables != 2) { return {}; }
    // This partial page is built only when the connected board has ALEX
    // filters. A board change must retire its old controls altogether.
    if (id == QLatin1String("hardware") && !caps.hasAlexFilters) {
        return {};
    }
    if (id == QLatin1String("pa") && (!caps.hasPaProfile || caps.isRxOnlySku)) {
        return {};
    }
    if (id == QLatin1String("pa") && !caps.showsBypassPaSettingsUi) {
        QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
        for (int p = 0; p < pages.size(); ++p) {
            if (pages.at(p).toObject().value(QStringLiteral("id"))
                == QJsonValue(QStringLiteral("pa.gain"))) {
                pages.removeAt(p);
                break;
            }
        }
        root.insert(QStringLiteral("pages"), pages);
    }
    if (id == QLatin1String("pa")) {
        QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
        for (int p = 0; p < pages.size(); ++p) {
            QJsonObject page = pages.at(p).toObject();
            if (page.value(QStringLiteral("id")) != QJsonValue(QStringLiteral("pa.values"))) {
                continue;
            }
            QJsonArray sections = page.value(QStringLiteral("sections")).toArray();
            for (int s = 0; s < sections.size(); ++s) {
                QJsonObject section = sections.at(s).toObject();
                QJsonArray controls = section.value(QStringLiteral("controls")).toArray();
                for (int c = 0; c < controls.size(); ++c) {
                    const QString controlId = controls.at(c).toObject().value(QStringLiteral("id")).toString();
                    if ((controlId == QLatin1String("pa.values.paCurrent") && !caps.hasPaAmpsTelemetry)
                        || (controlId == QLatin1String("pa.values.dcVoltage") && !caps.hasPaVoltsTelemetry)) {
                        controls.removeAt(c--);
                    }
                }
                if (controls.isEmpty()) { sections.removeAt(s--); }
                else { section.insert(QStringLiteral("controls"), controls); sections[s] = section; }
            }
            page.insert(QStringLiteral("sections"), sections);
            pages[p] = page;
        }
        root.insert(QStringLiteral("pages"), pages);
    }
    if (id == QLatin1String("hardware")) {
        const SkuUiProfile sku = skuUiProfileFor(model);
        QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
        for (int p = 0; p < pages.size(); ++p) {
            QJsonObject page = pages.at(p).toObject();
            QJsonArray sections = page.value(QStringLiteral("sections")).toArray();
            for (int s = 0; s < sections.size(); ++s) {
                QJsonObject section = sections.at(s).toObject();
                QJsonArray controls = section.value(QStringLiteral("controls")).toArray();
                for (int c = 0; c < controls.size(); ++c) {
                    QJsonObject control = controls.at(c).toObject();
                    if (control.value(QStringLiteral("kind")) == QJsonValue(QStringLiteral("table"))) {
                        const bool tx = control.value(QStringLiteral("id"))
                            == QJsonValue(QStringLiteral("hardware.antenna.txRows"));
                        control = expectedAntennaRowsTable(tx, model);
                        if (!SetupDescription::validateAntennaRowsTable(control, model)) {
                            return {};
                        }
                        controls[c] = control;
                        continue;
                    }
                    const QString name = control.value(QStringLiteral("binding")).toObject()
                        .value(QStringLiteral("property")).toObject()
                        .value(QStringLiteral("name")).toString();
                    const bool visible = name == QLatin1String("rxOutOnTx")
                        ? sku.hasRxOutOnTx : name == QLatin1String("ext1OutOnTx")
                        ? sku.hasExt1OutOnTx : name == QLatin1String("ext2OutOnTx")
                        ? sku.hasExt2OutOnTx : name == QLatin1String("rxOutOverride")
                        ? sku.hasRxBypassUi : true;
                    if (!visible) {
                        controls.removeAt(c--);
                        continue;
                    }
                    if (name == QLatin1String("ext1OutOnTx")) {
                        control.insert(QStringLiteral("label"), sku.ext1OutOnTxLabel);
                    } else if (name == QLatin1String("ext2OutOnTx")) {
                        control.insert(QStringLiteral("label"), sku.ext2OutOnTxLabel);
                        control.insert(QStringLiteral("tooltip"), sku.ext2OutOnTxTooltip);
                    }
                    if (!SetupDescription::validateHardwarePropertyBinding(control, model)) {
                        return {};
                    }
                    controls[c] = control;
                }
                section.insert(QStringLiteral("controls"), controls);
                sections[s] = section;
            }
            page.insert(QStringLiteral("sections"), sections);
            pages[p] = page;
        }
        root.insert(QStringLiteral("pages"), pages);
    }
    // General > Options is the only Task-43 control with a board-dependent
    // range. The desktop reads this same BoardCapabilities row.
    if (id == QLatin1String("general")) {
        QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
        for (int p = 0; p < pages.size(); ++p) {
            QJsonObject page = pages.at(p).toObject();
            QJsonArray sections = page.value(QStringLiteral("sections")).toArray();
            for (int s = 0; s < sections.size(); ++s) {
                QJsonObject section = sections.at(s).toObject();
                QJsonArray controls = section.value(QStringLiteral("controls")).toArray();
                for (int c = 0; c < controls.size(); ++c) {
                    QJsonObject control = controls.at(c).toObject();
                    const QString boardGate = control.value(QStringLiteral("gate"))
                        .toObject().value(QStringLiteral("board")).toString();
                    if (boardGate == QLatin1String("attenuator.present")
                        && !caps.attenuator.present) {
                        controls.removeAt(c--);
                        continue;
                    }
                    if (control.value(QStringLiteral("rangeSource")).toString()
                        == QLatin1String("board.attenuator")) {
                        control.insert(QStringLiteral("min"), caps.attenuator.minDb);
                        control.insert(QStringLiteral("max"), caps.attenuator.maxDb);
                        control.insert(QStringLiteral("step"), caps.attenuator.stepDb);
                        control.remove(QStringLiteral("rangeSource"));
                        controls[c] = control;
                    }
                }
                section.insert(QStringLiteral("controls"), controls);
                sections[s] = section;
            }
            page.insert(QStringLiteral("sections"), sections);
            pages[p] = page;
        }
        root.insert(QStringLiteral("pages"), pages);
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

} // namespace

bool SetupDescription::validateSettingToggleEncoding(const QJsonObject& control)
{
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    const bool settingToggle = control.value(QStringLiteral("kind"))
            == QJsonValue(QStringLiteral("toggle"))
        && binding.contains(QStringLiteral("setting"));
    if (!settingToggle) {
        return !control.contains(QStringLiteral("valueEncoding"));
    }
    // SettingsProxyServer broadcasts the stored QString. These keys' readers
    // compare capitalized strings, so the wire value must use that spelling.
    const QJsonValue key = binding.value(QStringLiteral("setting"));
    const QJsonValue encoding = control.value(QStringLiteral("valueEncoding"));
    if (binding.size() != 1 || !key.isString() || key.toString().isEmpty()
        || classifySettingsKey(key.toString()) != SettingsScope::Station
        || !encoding.isObject()) {
        return false;
    }
    const QJsonObject values = encoding.toObject();
    return values.size() == 2
        && values.value(QStringLiteral("true")) == QJsonValue(QStringLiteral("True"))
        && values.value(QStringLiteral("false")) == QJsonValue(QStringLiteral("False"));
}

bool SetupDescription::validateDisplaySettingBinding(const QJsonObject& control)
{
    struct Spec { const char* id; const char* key; const char* kind; bool tx; int version; };
    static constexpr Spec specs[] = {
        {"display.spectrumDefaults.fftSize", "DisplayFftSize", "slider", false, 4},
        {"display.spectrumDefaults.window", "DisplayFftWindow", "choice", false, 1},
        {"display.spectrumDefaults.hzPerBinTarget", "DisplayHzPerBinTarget", "decimal", false, 1},
        {"display.spectrumDefaults.fps", "DisplaySpectrumFps", "slider", false, 1},
        {"display.multimeter.pollingDelay", "MultimeterDelayMs", "integer", false, 1},
        {"display.txDisplay.fftSize", "DisplayTxFftSize", "slider", true, 4},
        {"display.txDisplay.window", "DisplayTxWindowType", "choice", true, 1},
        {"display.txDisplay.panDetector", "DisplayTxPanDetector", "choice", true, 1},
        {"display.txDisplay.panAveraging", "DisplayTxPanAveraging", "choice", true, 1},
        {"display.txDisplay.panAvTime", "DisplayTxPanAvTimeMs", "integer", true, 1},
        {"display.txDisplay.panNormalize", "DisplayTxPanNormalize", "toggle", true, 4},
        {"display.txDisplay.wfDetector", "DisplayTxWfDetector", "choice", true, 1},
        {"display.txDisplay.wfAveraging", "DisplayTxWfAveraging", "choice", true, 1},
        {"display.txDisplay.wfAvTime", "DisplayTxWfAvTimeMs", "integer", true, 1},
    };
    const QString id = control.value(QStringLiteral("id")).toString();
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    const QJsonObject gate = control.value(QStringLiteral("gate")).toObject();
    for (const Spec& spec : specs) {
        if (id != QLatin1String(spec.id)) { continue; }
        if (binding.size() != 1 || binding.value(QStringLiteral("setting")) != QJsonValue(QLatin1String(spec.key))
            || classifySettingsKey(QString::fromLatin1(spec.key)) != SettingsScope::Station
            || control.value(QStringLiteral("kind")) != QJsonValue(QLatin1String(spec.kind))
            || control.value(QStringLiteral("applies"))
                != QJsonValue(spec.tx || id == QLatin1String("display.multimeter.pollingDelay")
                                  ? QStringLiteral("live") : QStringLiteral("subscription"))
            || (spec.tx ? gate != QJsonObject{{QStringLiteral("capability"), QStringLiteral("txDisplayVersion")},
                                              {QStringLiteral("min"), 2}} : !gate.isEmpty())
            || (spec.version == 4 ? control.value(QStringLiteral("requiresDescriptionVersion")) != QJsonValue(4)
                                  : control.contains(QStringLiteral("requiresDescriptionVersion")))) {
            return false;
        }
        if (id == QLatin1String("display.spectrumDefaults.fftSize")
            || id == QLatin1String("display.txDisplay.fftSize")) {
            const QJsonArray options = control.value(QStringLiteral("options")).toArray();
            if (options.size() != 7 || control.contains(QStringLiteral("min"))
                || control.contains(QStringLiteral("max")) || control.contains(QStringLiteral("step"))
                || control.contains(QStringLiteral("enabledWhen"))
                || control.value(QStringLiteral("default"))
                    != QJsonValue(spec.tx ? 32768 : 4096)) { return false; }
            for (int i = 0; i < 7; ++i) {
                const int value = 4096 << i;
                if (options.at(i) != QJsonValue(QJsonObject{
                        {QStringLiteral("value"), value},
                        {QStringLiteral("label"), QString::number(value)}})) { return false; }
            }
            return true;
        }
        if (id == QLatin1String("display.txDisplay.panNormalize")) {
            return !control.contains(QStringLiteral("options"))
                && control.value(QStringLiteral("default")) == QJsonValue(false)
                && control.value(QStringLiteral("enabledWhen")) == QJsonValue(QJsonObject{
                    {QStringLiteral("setting"), QStringLiteral("DisplayTxPanDetector")},
                    {QStringLiteral("oneOf"), QJsonArray{QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4")}}})
                && validateSettingToggleEncoding(control);
        }
        return !control.contains(QStringLiteral("options"))
            && !control.contains(QStringLiteral("enabledWhen"));
    }
    return false;
}

bool SetupDescription::validateDisplayPhoneBinding(const QJsonObject& control)
{
    struct RendererSpec {
        const char* id;
        const char* key;
        const char* label;
        const char* tooltip;
        const char* kind;
        const char* applies;
        int defaultValue;
        int minimum;
        int maximum;
        int step;
        const char* unit;
    };
    static constexpr RendererSpec rendererSpecs[] = {
        {"display.spectrumDefaults.panFill", "DisplayPanFill", "Fill under trace",
         "Check to fill the panadapter display line below the data.", "toggle", "live", 1, 0, 0, 0, ""},
        {"display.spectrumDefaults.fillAlpha", "DisplayFftFillAlpha", "Fill Alpha:",
         "Opacity of the fill area under the spectrum trace (0 = transparent, 100 = opaque).",
         "slider", "live", 70, 0, 100, 1, "%"},
        {"display.spectrumDefaults.gradient", "DisplayGradientEnabled", "Trace gradient",
         "When checked, the spectrum trace line renders with the gradient color applied.",
         "toggle", "live", 0, 0, 0, 0, ""},
        {"display.spectrumDefaults.peakHold", "DisplayPeakHoldEnabled", "Peak hold",
         "When enabled, the highest signal level seen at each frequency bin is held on the display.",
         "toggle", "live", 0, 0, 0, 0, ""},
        {"display.spectrumDefaults.peakDelay", "DisplayPeakHoldResetMs", "Peak Delay:",
         "Time in milliseconds before a held peak begins to decay back toward the live trace.",
         "integer", "live", 2000, 100, 10000, 100, "ms"},
        {"display.waterfallDefaults.updatePeriod", "DisplayWfUpdatePeriodMs", "Update Period:",
         "How often to update (scroll another pixel line) on the waterfall display.  Note that this is tamed by the FPS setting.",
         "slider", "subscription", 30, 10, 500, 1, "ms"},
        {"display.waterfallDefaults.stopOnTx", "WaterfallStopOnTx", "Stop on TX",
         "Pause the waterfall while transmitting. Resumes automatically when TX ends.",
         "toggle", "live", 0, 0, 0, 0, ""},
        {"display.waterfallDefaults.opacity", "DisplayWfOpacity", "Opacity:",
         "Waterfall opacity (0 = fully transparent, 100 = fully opaque). Blends the waterfall over the spectrum background.",
         "slider", "live", 100, 0, 100, 1, "%"},
        {"display.waterfallDefaults.showRxFilter", "DisplayShowRxFilterOnWaterfall",
         "Show RX filter on waterfall",
         "Overlay the current RX passband filter boundaries on the waterfall display.",
         "toggle", "live", 0, 0, 0, 0, ""},
        {"display.waterfallDefaults.showTxFilter", "DisplayShowTxFilterOnRxWaterfall",
         "Show TX filter on RX waterfall",
         "Overlay the TX passband filter boundaries on the RX waterfall display.",
         "toggle", "live", 1, 0, 0, 0, ""},
        {"display.waterfallDefaults.showRxZeroLine", "DisplayShowRxZeroLine",
         "Show RX zero line on waterfall",
         "Draw a line on the waterfall at the RX center frequency (zero-beat reference).",
         "toggle", "live", 0, 0, 0, 0, ""},
        {"display.waterfallDefaults.showTxZeroLine", "DisplayShowTxZeroLine",
         "Show TX zero line on waterfall",
         "Draw a line on the waterfall at the TX center frequency (zero-beat reference).",
         "toggle", "live", 0, 0, 0, 0, ""},
    };
    for (const RendererSpec& spec : rendererSpecs) {
        if (control.value(QStringLiteral("id")) != QJsonValue(QLatin1String(spec.id))) {
            continue;
        }
        const bool toggle = QLatin1String(spec.kind) == QLatin1String("toggle");
        const int requiredVersion = QLatin1String(spec.id).startsWith(
            QLatin1String("display.waterfallDefaults.show")) ? 10 : 9;
        if (control.size() != (toggle ? 8 : 12)
            || control.value(QStringLiteral("binding")) != QJsonValue(QJsonObject{
                   {QStringLiteral("phone"), QLatin1String(spec.key)}})
            || control.value(QStringLiteral("label")) != QJsonValue(QLatin1String(spec.label))
            || control.value(QStringLiteral("tooltip")) != QJsonValue(QLatin1String(spec.tooltip))
            || control.value(QStringLiteral("kind")) != QJsonValue(QLatin1String(spec.kind))
            || control.value(QStringLiteral("applies")) != QJsonValue(QLatin1String(spec.applies))
            || control.value(QStringLiteral("requiresDescriptionVersion")) != QJsonValue(requiredVersion)
            || control.value(QStringLiteral("default"))
                != (toggle ? QJsonValue(spec.defaultValue != 0) : QJsonValue(spec.defaultValue))) {
            return false;
        }
        return toggle || (control.value(QStringLiteral("min")) == QJsonValue(spec.minimum)
            && control.value(QStringLiteral("max")) == QJsonValue(spec.maximum)
            && control.value(QStringLiteral("step")) == QJsonValue(spec.step)
            && control.value(QStringLiteral("unit")) == QJsonValue(QLatin1String(spec.unit)));
    }
    // Version 11: Spectrum Peaks, in the native page's order. The desktop
    // draws the peak hold trace and the blobs from the frames it already has
    // (SpectrumWidget::updateReducedSpectrumOverlays); the phone asks the
    // Core for the same computation through its display extras
    // (peakBlobs / activePeakHold), so every row needs displayExtrasVersion 1;
    // the peak hold's Hold duration and Update during TX need 3, the Core
    // that honours holdMs and activePeakHold.onTx.
    struct PeaksSpec {
        const char* id;
        const char* key;
        const char* label;
        const char* tooltip;
        const char* kind;
        const char* applies;
        int defaultValue;
        const char* defaultColour;
        int minimum;
        int maximum;
        int step;
        const char* unit;
        int extrasVersion;
    };
    static constexpr PeaksSpec peaksSpecs[] = {
        {"display.spectrumPeaks.activePeakHold", "DisplayActivePeakHoldEnabled",
         "Enable per-bin peak trace with decay",
         "Display a secondary trace of the highest recent level at each frequency bin. "
         "Each bin holds its peak for the hold duration after it was last raised, then falls "
         "at the drop rate.",
         "toggle", "subscription", 0, "", 0, 0, 0, "", 1},
        {"display.spectrumPeaks.activePeakHoldTime", "DisplayActivePeakHoldDurationMs",
         "Hold duration:",
         "How long (ms) a peak bin is held at its maximum before starting to decay.",
         "integer", "subscription", 2000, "", 100, 60000, 100, "ms", 3},
        {"display.spectrumPeaks.activePeakHoldDropRate", "DisplayActivePeakHoldDropDbPerSec",
         "Drop rate:", "Rate at which a held peak falls once its hold duration has passed.",
         "integer", "subscription", 6, "", 1, 60, 1, "dB/s", 1},
        {"display.spectrumPeaks.activePeakHoldFill", "DisplayActivePeakHoldFill",
         "Fill area between peak trace and current trace",
         "Shade the region between the live spectrum and the peak-hold trace.",
         "toggle", "live", 0, "", 0, 0, 0, "", 1},
        {"display.spectrumPeaks.activePeakHoldOnTx", "DisplayActivePeakHoldOnTx",
         "Update during TX",
         "Keep the peak trace running while this panadapter transmits. "
         "When off, the trace is hidden and paused until transmit ends.",
         "toggle", "subscription", 0, "", 0, 0, 0, "", 3},
        {"display.spectrumPeaks.activePeakHoldColor", "DisplayActivePeakHoldColor",
         "Trace color:",
         "Color of the dashed Active Peak Hold trace. Set this to a hue different from the "
         "live data-line color so the peak trace stays visible (e.g. after Reset to Smooth "
         "Defaults paints the live trace white).",
         "colour", "live", 0, "#FFD700FF", 0, 0, 0, "", 1},
        {"display.spectrumPeaks.peakBlobs", "DisplayPeakBlobsEnabled",
         "Show top-N peak markers",
         "Display small circle markers at the top-N highest signal peaks in the spectrum.",
         "toggle", "subscription", 0, "", 0, 0, 0, "", 1},
        {"display.spectrumPeaks.peakBlobCount", "DisplayPeakBlobsCount",
         "Number of peaks:", "Number of peak markers to display (1 to 20).",
         "integer", "subscription", 3, "", 1, 20, 1, "", 1},
        {"display.spectrumPeaks.peakBlobInsideFilter", "DisplayPeakBlobsInsideFilterOnly",
         "Only show peaks inside the RX filter passband",
         "Restrict peak blobs to frequencies within the current RX filter passband.",
         "toggle", "subscription", 0, "", 0, 0, 0, "", 1},
        {"display.spectrumPeaks.peakBlobHold", "DisplayPeakBlobsHoldEnabled",
         "Hold peaks before decay",
         "Keep each blob at its peak position for the hold duration before falling.",
         "toggle", "subscription", 0, "", 0, 0, 0, "", 1},
        {"display.spectrumPeaks.peakBlobHoldTime", "DisplayPeakBlobsHoldMs",
         "Hold duration:", "How long (ms) a blob is held at its peak before falling.",
         "integer", "subscription", 500, "", 100, 60000, 100, "ms", 1},
        {"display.spectrumPeaks.peakBlobHoldDrop", "DisplayPeakBlobsHoldDrop",
         "Decay after hold (off = hard cut)",
         "When on, blobs decay at the fall rate after the hold. "
         "When off, blobs disappear instantly after the hold duration.",
         "toggle", "subscription", 0, "", 0, 0, 0, "", 1},
        {"display.spectrumPeaks.peakBlobFallRate", "DisplayPeakBlobsFallDbPerSec",
         "Fall rate:", "Rate at which blobs fall after the hold duration.",
         "integer", "subscription", 6, "", 1, 60, 1, "dB/s", 1},
        {"display.spectrumPeaks.peakBlobColor", "DisplayPeakBlobColor",
         "Blob color:", "Color of the peak blob circles.",
         "colour", "live", 0, "#FF4500FF", 0, 0, 0, "", 1},
        {"display.spectrumPeaks.peakBlobTextColor", "DisplayPeakBlobTextColor",
         "Text color:", "Color of the dBm readout text on each peak blob.",
         "colour", "live", 0, "#7FFF00FF", 0, 0, 0, "", 1},
    };
    for (const PeaksSpec& spec : peaksSpecs) {
        if (control.value(QStringLiteral("id")) != QJsonValue(QLatin1String(spec.id))) {
            continue;
        }
        const QLatin1String kind(spec.kind);
        const bool integer = kind == QLatin1String("integer");
        const bool hasUnit = spec.unit[0] != '\0';
        const QJsonValue expectedDefault = kind == QLatin1String("toggle")
            ? QJsonValue(spec.defaultValue != 0)
            : kind == QLatin1String("colour") ? QJsonValue(QLatin1String(spec.defaultColour))
                                              : QJsonValue(spec.defaultValue);
        if (control.size() != (integer ? (hasUnit ? 13 : 12) : 9)
            || control.value(QStringLiteral("binding")) != QJsonValue(QJsonObject{
                   {QStringLiteral("phone"), QLatin1String(spec.key)}})
            || control.value(QStringLiteral("label")) != QJsonValue(QLatin1String(spec.label))
            || control.value(QStringLiteral("tooltip")) != QJsonValue(QLatin1String(spec.tooltip))
            || control.value(QStringLiteral("kind")) != QJsonValue(kind)
            || control.value(QStringLiteral("applies")) != QJsonValue(QLatin1String(spec.applies))
            || control.value(QStringLiteral("requiresDescriptionVersion")) != QJsonValue(11)
            || control.value(QStringLiteral("gate")) != QJsonValue(QJsonObject{
                   {QStringLiteral("capability"), QStringLiteral("displayExtrasVersion")},
                   {QStringLiteral("min"), spec.extrasVersion}})
            || control.value(QStringLiteral("default")) != expectedDefault) {
            return false;
        }
        return !integer || (control.value(QStringLiteral("min")) == QJsonValue(spec.minimum)
            && control.value(QStringLiteral("max")) == QJsonValue(spec.maximum)
            && control.value(QStringLiteral("step")) == QJsonValue(spec.step)
            && (!hasUnit || control.value(QStringLiteral("unit")) == QJsonValue(QLatin1String(spec.unit))));
    }
    struct Spec {
        const char* id;
        const char* phone;
        const char* kind;
        const char* capability;
        int capabilityVersion;
        int defaultValue;
        int minimum;
        int maximum;
        int step;
        const char* unit;
        QStringList choices;
    };
    static const Spec specs[] = {
        {"display.spectrumDefaults.detector", "DisplaySpectrumDetector", "choice",
         "remoteMediaVersion", 1, ControlRanges::kDisplaySpectrumDetectorDefault, 0, 0, 0, "",
         {"Peak", "Rosenfell", "Average", "Sample", "RMS"}},
        {"display.spectrumDefaults.averaging", "DisplaySpectrumAveraging", "choice",
         "displayExtrasVersion", 1, ControlRanges::kDisplaySpectrumAveragingDefault, 0, 0, 0, "",
         {"None", "Recursive", "Time Window", "Log Recursive"}},
        {"display.spectrumDefaults.averageTime", "DisplaySpectrumAverageTimeMs", "integer",
         "displayExtrasVersion", 1, ControlRanges::kDisplaySpectrumAvgTimeDefaultMs,
         ControlRanges::kDisplayAvgTimeMinMs, ControlRanges::kDisplayAvgTimeMaxMs,
         ControlRanges::kDisplayAvgTimeStepMs, "ms", {}},
        {"display.spectrumDefaults.decimation", "decimation", "integer",
         "spectrumGrantVersion", 2, ControlRanges::kDisplayDecimationDefault,
         ControlRanges::kDisplayDecimationMin, ControlRanges::kDisplayDecimationMax,
         ControlRanges::kDisplayDecimationStep, "", {}},
        {"display.waterfallDefaults.detector", "DisplayWaterfallDetector", "choice",
         "remoteMediaVersion", 1, ControlRanges::kDisplayWaterfallDetectorDefault, 0, 0, 0, "",
         {"Peak", "Rosenfell", "Average", "Sample"}},
        {"display.waterfallDefaults.averaging", "DisplayWaterfallAveraging", "choice",
         "displayExtrasVersion", 1, ControlRanges::kDisplayWaterfallAveragingDefault, 0, 0, 0, "",
         {"None", "Recursive", "Time Window", "Log Recursive"}},
        {"display.waterfallDefaults.averageTime", "DisplayWaterfallAverageTimeMs", "integer",
         "displayExtrasVersion", 1, ControlRanges::kDisplayWaterfallAvgTimeDefaultMs,
         ControlRanges::kDisplayAvgTimeMinMs, ControlRanges::kDisplayAvgTimeMaxMs,
         ControlRanges::kDisplayAvgTimeStepMs, "ms", {}}
    };
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    for (const Spec& spec : specs) {
        if (control.value(QStringLiteral("id")) != QJsonValue(QLatin1String(spec.id))) {
            continue;
        }
        if (binding != QJsonObject{{QStringLiteral("phone"), QLatin1String(spec.phone)}}
            || control.value(QStringLiteral("kind")) != QJsonValue(QLatin1String(spec.kind))
            || control.value(QStringLiteral("applies")) != QJsonValue(QStringLiteral("subscription"))
            || control.value(QStringLiteral("requiresDescriptionVersion")) != QJsonValue(8)
            || control.value(QStringLiteral("gate")) != QJsonValue(QJsonObject{
                   {QStringLiteral("capability"), QLatin1String(spec.capability)},
                   {QStringLiteral("min"), spec.capabilityVersion}})
            || control.value(QStringLiteral("default")) != QJsonValue(spec.defaultValue)
            || !control.value(QStringLiteral("label")).isString()
            || control.value(QStringLiteral("label")).toString().isEmpty()
            || !control.value(QStringLiteral("tooltip")).isString()) {
            return false;
        }
        if (!spec.choices.isEmpty()) {
            const QJsonArray options = control.value(QStringLiteral("options")).toArray();
            if (control.size() != 10 || options.size() != spec.choices.size()) { return false; }
            for (int i = 0; i < options.size(); ++i) {
                if (options.at(i) != QJsonValue(QJsonObject{
                        {QStringLiteral("value"), i},
                        {QStringLiteral("label"), spec.choices.at(i)}})) { return false; }
            }
            return true;
        }
        const bool hasUnit = spec.unit[0] != '\0';
        return control.size() == (hasUnit ? 13 : 12)
            && control.value(QStringLiteral("min")) == QJsonValue(spec.minimum)
            && control.value(QStringLiteral("max")) == QJsonValue(spec.maximum)
            && control.value(QStringLiteral("step")) == QJsonValue(spec.step)
            && (!hasUnit || control.value(QStringLiteral("unit")) == QJsonValue(QLatin1String(spec.unit)));
    }
    return false;
}

bool SetupDescription::validateAppearanceColourBinding(const QJsonObject& control)
{
    struct Swatch { const char* id; const char* phoneKey; const char* defaultRgba; };
    static constexpr Swatch swatches[] = {
        {"traceFillColor", "DisplayFillColor", "#00E5FFFF"},
        {"gridColor", "DisplayGridColor", "#FFFFFF28"},
        {"gridFineColor", "DisplayGridFineColor", "#FFFFFF14"},
        {"hGridColor", "DisplayHGridColor", "#FFFFFF28"},
        {"gridTextColor", "DisplayGridTextColor", "#FFFF00FF"},
        {"bandEdgeColor", "DisplayBandEdgeColor", "#FF0000FF"},
        {"rxZeroLineColor", "DisplayRxZeroLineColor", "#FF0000FF"},
        {"txZeroLineColor", "DisplayTxZeroLineColor", "#FFB800FF"},
        {"rxFilterColor", "DisplayRxFilterColor", "#00B4D850"},
        {"txFilterColor", "DisplayTxFilterColor", "#FF783C2E"},
    };
    const QString id = control.value(QStringLiteral("id")).toString();
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    const QString defaultColour = control.value(QStringLiteral("default")).toString();
    if (control.size() != 7 || control.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("colour"))
        || control.value(QStringLiteral("applies")) != QJsonValue(QStringLiteral("live"))
        || control.value(QStringLiteral("label")).toString().isEmpty()
        || !control.value(QStringLiteral("tooltip")).isString()
        || binding.size() != 1 || !binding.value(QStringLiteral("phone")).isString()
        || defaultColour.size() != 9 || defaultColour.front() != QLatin1Char('#')) { return false; }
    for (const QChar digit : defaultColour.sliced(1)) {
        if (!digit.isDigit() && (digit < QLatin1Char('A') || digit > QLatin1Char('F'))) {
            return false;
        }
    }
    for (const Swatch& swatch : swatches) {
        if (id == QStringLiteral("appearance.colorsTheme.") + QLatin1String(swatch.id)) {
            return binding.value(QStringLiteral("phone")) == QJsonValue(QLatin1String(swatch.phoneKey))
                && defaultColour == QLatin1String(swatch.defaultRgba);
        }
    }
    return false;
}

bool SetupDescription::validateAppearanceMeterStyleBinding(const QJsonObject& control)
{
    struct StyleControl {
        const char* id;
        const char* phoneKey;
        const char* label;
        const char* tooltip;
        const char* kind;
        int defaultChoice;
        QStringList options;
    };
    static const StyleControl styles[] = {
        {"face", "SMeter_FaceStyle", "Face:", "The S-meter's face", "choice", 0,
         {"Aged Cream", "VU Amber", "Collins White", "Blackface", "Carbon", "Ice", "Classic (flat)"}},
        {"peakHold", "PeakHoldEnabled", "Peak hold", "Hold the S-meter's peak reading", "toggle", 0, {}},
        {"peakDecay", "PeakDecayRate", "Decay Rate:", "How fast the held peak falls back", "choice", 1,
         {"Fast (20 dB/s)", "Medium (10 dB/s)", "Slow (5 dB/s)"}},
    };
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    if (binding.size() != 1
        || control.value(QStringLiteral("applies")) != QJsonValue(QStringLiteral("live"))
        || control.value(QStringLiteral("requiresDescriptionVersion")) != QJsonValue(7)) {
        return false;
    }
    for (const StyleControl& style : styles) {
        if (control.value(QStringLiteral("id"))
            != QJsonValue(QStringLiteral("appearance.meterStyles.") + QLatin1String(style.id))) {
            continue;
        }
        if (control.value(QStringLiteral("label")) != QJsonValue(QLatin1String(style.label))
            || control.value(QStringLiteral("tooltip")) != QJsonValue(QLatin1String(style.tooltip))
            || control.value(QStringLiteral("kind")) != QJsonValue(QLatin1String(style.kind))
            || binding.value(QStringLiteral("phone")) != QJsonValue(QLatin1String(style.phoneKey))) {
            return false;
        }
        if (style.options.isEmpty()) {
            return control.size() == 8 && !control.contains(QStringLiteral("options"))
                && control.value(QStringLiteral("default")) == QJsonValue(true);
        }
        const QJsonArray options = control.value(QStringLiteral("options")).toArray();
        if (control.size() != 9 || options.size() != style.options.size()
            || control.value(QStringLiteral("default")) != QJsonValue(style.defaultChoice)) {
            return false;
        }
        int index = 0;
        for (const QString& label : style.options) {
            const QJsonObject option = options.at(index).toObject();
            if (option.size() != 2 || option.value(QStringLiteral("value")) != QJsonValue(index)
                || option.value(QStringLiteral("label")) != QJsonValue(label)) {
                return false;
            }
            ++index;
        }
        return true;
    }
    return false;
}

bool SetupDescription::validateActiveSlicePropertyBinding(const QJsonObject& control)
{
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    if (binding.size() != 1 || !binding.value(QStringLiteral("property")).isObject()) {
        return false;
    }
    const QJsonObject ref = binding.value(QStringLiteral("property")).toObject();
    if (ref.size() != 2) {
        return false;
    }
    const QString object = ref.value(QStringLiteral("object")).toString();
    const QMetaObject* meta = object == QLatin1String("slice:active")
        ? &SliceModel::staticMetaObject
        : object == QLatin1String("transmit")
          ? &TransmitModel::staticMetaObject
          : object == QLatin1String("notches")
            ? &NotchModel::staticMetaObject : nullptr;
    const QByteArray policyClass = object == QLatin1String("slice:active")
        ? QByteArrayLiteral("SliceModel")
        : object == QLatin1String("transmit")
          ? QByteArrayLiteral("TransmitModel") : QByteArrayLiteral("NotchModel");
    if (!meta) {
        return false;
    }
    const QByteArray name = ref.value(QStringLiteral("name")).toString().toUtf8();
    const MirrorProperty* property = MirrorSchema::forMetaObject(meta).byName(name);
    if (!property) {
        return false;
    }
    if (object == QLatin1String("transmit")) {
        const QJsonObject gate = control.value(QStringLiteral("gate")).toObject();
        if (gate.value(QStringLiteral("offAir")) != QJsonValue(true)
            || gate.value(QStringLiteral("capability")) != QJsonValue(QStringLiteral("transmitSettingsVersion"))
            || gate.value(QStringLiteral("min")).toInt() < 4) {
            return false;
        }
    }
    const QString kind = control.value(QStringLiteral("kind")).toString();
    const QJsonObject gate = control.value(QStringLiteral("gate")).toObject();
    if (kind == QLatin1String("readout")) {
        return object == QLatin1String("slice:active")
            && name == QByteArrayLiteral("minNotchWidthHz")
            && gate.value(QStringLiteral("capability")) == QJsonValue(QStringLiteral("dspInfoVersion"))
            && gate.value(QStringLiteral("min")).toInt() >= 1
            && property->kind == MirrorWireKind::Float64
            && MirrorPolicy::hasExplicitEntry(policyClass, name)
            && MirrorPolicy::directionFor(policyClass, name) == MirrorDirection::Outbound;
    }
    if (!property->isWritable || !MirrorPolicy::inboundAllowed(policyClass, name)) {
        return false;
    }
    if (object == QLatin1String("notches")
        && (name != QByteArrayLiteral("autoIncrease")
            || gate.value(QStringLiteral("capability")) != QJsonValue(QStringLiteral("notchControlVersion"))
            || gate.value(QStringLiteral("min")).toInt() < 1)) {
        return false;
    }
    const MirrorWireKind expected = kind == QLatin1String("toggle") ? MirrorWireKind::Bool
        : kind == QLatin1String("decimal") ? MirrorWireKind::Float64
        : kind == QLatin1String("choice") ? MirrorWireKind::Enum
        : kind == QLatin1String("integer") || kind == QLatin1String("slider")
          ? MirrorWireKind::Int64 : MirrorWireKind::Unsupported;
    return property->kind == expected
        || (kind == QLatin1String("choice") && property->kind == MirrorWireKind::Int64);
}

bool SetupDescription::validateTransmitPropertyBinding(const QJsonObject& control)
{
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    if (binding.size() != 1 || !binding.value(QStringLiteral("property")).isObject()) {
        return false;
    }
    const QJsonObject ref = binding.value(QStringLiteral("property")).toObject();
    if (ref.size() != 2 || ref.value(QStringLiteral("object")) != QJsonValue(QStringLiteral("transmit"))) {
        return false;
    }
    const QByteArray name = ref.value(QStringLiteral("name")).toString().toUtf8();
    static const QSet<QByteArray> kDexpSettings{
        "dexpEnabled", "dexpAttackTimeMs", "voxHangTimeMs", "dexpReleaseTimeMs",
        "voxThresholdDb", "dexpExpansionRatioDb", "dexpHysteresisRatioDb",
        "dexpDetectorTauMs", "dexpLookAheadEnabled", "dexpLookAheadMs",
        "dexpSideChannelFilterEnabled", "dexpLowCutHz", "dexpHighCutHz",
        "antiVoxRun", "antiVoxGainDb", "antiVoxTauMs"};
    const QJsonObject gate = control.value(QStringLiteral("gate")).toObject();
    if ((name != QByteArrayLiteral("power") && !kDexpSettings.contains(name))
        || gate.value(QStringLiteral("transmit")) != QJsonValue(true)
        || gate.value(QStringLiteral("capability")) != QJsonValue(QStringLiteral("transmitSettingsVersion"))
        || gate.value(QStringLiteral("min")).toInt() < 5
        || gate.value(QStringLiteral("offAir")) != QJsonValue(true)) {
        return false;
    }
    const MirrorProperty* property = MirrorSchema::forMetaObject(&TransmitModel::staticMetaObject).byName(name);
    if (!property || !property->isWritable
        || !MirrorPolicy::inboundAllowed(QByteArrayLiteral("TransmitModel"), name)) {
        return false;
    }
    const QString kind = control.value(QStringLiteral("kind")).toString();
    const MirrorWireKind expected = kind == QLatin1String("toggle") ? MirrorWireKind::Bool
        : kind == QLatin1String("integer") ? MirrorWireKind::Int64
        : kind == QLatin1String("slider") ? MirrorWireKind::Int64
        : kind == QLatin1String("decimal") ? MirrorWireKind::Float64
        : MirrorWireKind::Unsupported;
    return expected != MirrorWireKind::Unsupported && property->kind == expected;
}

bool SetupDescription::validateHardwarePropertyBinding(const QJsonObject& control, HPSDRModel model)
{
    // Closed scalar set: source binding, safety gate, and projected SKU text
    // must agree before a description can be published.
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    if (binding.size() != 1 || !binding.value(QStringLiteral("property")).isObject()
        || control.size() != 7
        || control.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("toggle"))
        || control.value(QStringLiteral("applies")) != QJsonValue(QStringLiteral("live"))) {
        return false;
    }
    const QJsonObject ref = binding.value(QStringLiteral("property")).toObject();
    if (ref.size() != 2
        || ref.value(QStringLiteral("object")) != QJsonValue(QStringLiteral("alexAntennas"))) {
        return false;
    }
    const QByteArray name = ref.value(QStringLiteral("name")).toString().toUtf8();
    const bool receive = name == QByteArrayLiteral("useTxAntennaForRx");
    const bool block2 = name == QByteArrayLiteral("blockTxAnt2");
    const bool block3 = name == QByteArrayLiteral("blockTxAnt3");
    const bool rxOut = name == QByteArrayLiteral("rxOutOnTx");
    const bool ext1 = name == QByteArrayLiteral("ext1OutOnTx");
    const bool ext2 = name == QByteArrayLiteral("ext2OutOnTx");
    const bool override = name == QByteArrayLiteral("rxOutOverride");
    if (!receive && !block2 && !block3 && !rxOut && !ext1 && !ext2 && !override) {
        return false;
    }
    const SkuUiProfile sku = skuUiProfileFor(model);
    if (model != HPSDRModel::FIRST
        && ((rxOut && !sku.hasRxOutOnTx) || (ext1 && !sku.hasExt1OutOnTx)
            || (ext2 && !sku.hasExt2OutOnTx) || (override && !sku.hasRxBypassUi))) {
        return false;
    }
    const QString id = QStringLiteral("hardware.antennaAlex.") + QString::fromUtf8(name);
    const QString label = receive ? QStringLiteral("Use TX antenna for RX")
        : block2 ? QStringLiteral("Block TX on Ant 2")
        : block3 ? QStringLiteral("Block TX on Ant 3")
        : rxOut ? QStringLiteral("RX Bypass on TX")
        : ext1 ? sku.ext1OutOnTxLabel
        : ext2 ? sku.ext2OutOnTxLabel
               : QStringLiteral("Disable RX Bypass relay");
    const QString tooltip = receive
        ? QStringLiteral("Use the TX antenna for RX instead of the RX antenna.")
        : block2
          ? QStringLiteral("Prevents transmit assignments to Antenna Port 2. Use when Ant 2 is wired for receive only.")
        : block3 ? QStringLiteral("Prevents transmit assignments to Antenna Port 3. Use when Ant 3 is wired for receive only.")
        : rxOut ? QStringLiteral("Enable RX Bypass Out relay on transmit.")
        : ext1 ? QStringLiteral("Route Ext 1 to receive path during transmit.")
        : ext2 ? sku.ext2OutOnTxTooltip
               : QStringLiteral("Disable the RX Bypass Out relay (chkDisableRXOut in Thetis).");
    if (control.value(QStringLiteral("id")) != QJsonValue(id)
        || control.value(QStringLiteral("label")) != QJsonValue(label)
        || control.value(QStringLiteral("tooltip")) != QJsonValue(tooltip)) {
        return false;
    }
    // hasAlexFilters is projected by this service, not repeated as a client
    // gate: that BoardCapabilities flag is absent from the station catalogue.
    QJsonObject expectedGate{{QStringLiteral("capability"), QStringLiteral("radioHardwareVersion")},
                             {QStringLiteral("min"), receive ? 2 : rxOut ? 5 : 6}};
    if (!receive) {
        expectedGate.insert(QStringLiteral("offAir"), true);
    }
    if (control.value(QStringLiteral("gate")).toObject() != expectedGate) {
        return false;
    }
    const MirrorProperty* property = MirrorSchema::forMetaObject(
        &AlexAntennaFacade::staticMetaObject).byName(name);
    return property && property->isWritable && property->kind == MirrorWireKind::Bool
        && MirrorPolicy::inboundAllowed(QByteArrayLiteral("AlexAntennaFacade"), name);
}

bool SetupDescription::validatePaReadoutBinding(const QJsonObject& control)
{
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    const QJsonObject ref = binding.value(QStringLiteral("property")).toObject();
    const QJsonObject gate = control.value(QStringLiteral("gate")).toObject();
    if (control.size() != 9 || binding.size() != 1 || ref.size() != 2
        || ref.value(QStringLiteral("object")) != QJsonValue(QStringLiteral("txState"))
        || control.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("readout"))
        || control.value(QStringLiteral("applies")) != QJsonValue(QStringLiteral("live"))) {
        return false;
    }
    const QByteArray name = ref.value(QStringLiteral("name")).toString().toUtf8();
    const bool derivedPower = name == QByteArrayLiteral("forwardRawPowerWatts");
    const bool volts = name == QByteArrayLiteral("forwardAdcVolts")
        || name == QByteArrayLiteral("reflectedAdcVolts");
    const bool derived = derivedPower || volts;
    if (gate != QJsonObject{{QStringLiteral("capability"), QStringLiteral("txReadingsVersion")},
                            {QStringLiteral("min"), derived ? 2 : 1}}) {
        return false;
    }
    const bool power = name == QByteArrayLiteral("forwardPowerWatts")
        || name == QByteArrayLiteral("reflectedPowerWatts");
    const bool swr = name == QByteArrayLiteral("swr");
    const bool raw = name == QByteArrayLiteral("forwardAdcRaw")
        || name == QByteArrayLiteral("reflectedAdcRaw");
    if (!power && !swr && !raw && !derived) { return false; }
    const QString id = name == QByteArrayLiteral("forwardPowerWatts")
        ? QStringLiteral("pa.values.forwardCalibrated")
        : derivedPower ? QStringLiteral("pa.values.forwardRawPower")
        : name == QByteArrayLiteral("reflectedPowerWatts")
          ? QStringLiteral("pa.values.reflectedPower")
        : swr ? QStringLiteral("pa.values.swr")
        : name == QByteArrayLiteral("forwardAdcVolts") ? QStringLiteral("pa.values.forwardVoltage")
        : name == QByteArrayLiteral("reflectedAdcVolts") ? QStringLiteral("pa.values.reflectedVoltage")
        : name == QByteArrayLiteral("forwardAdcRaw")
          ? QStringLiteral("pa.values.forwardAdc") : QStringLiteral("pa.values.reflectedAdc");
    const QString label = name == QByteArrayLiteral("forwardPowerWatts")
        ? QStringLiteral("Forward (calibrated):")
        : derivedPower ? QStringLiteral("Forward (raw):")
        : name == QByteArrayLiteral("reflectedPowerWatts") ? QStringLiteral("Reflected:")
        : swr ? QStringLiteral("SWR:")
        : name == QByteArrayLiteral("forwardAdcVolts") ? QStringLiteral("FWD Voltage:")
        : name == QByteArrayLiteral("reflectedAdcVolts") ? QStringLiteral("REV Voltage:")
        : name == QByteArrayLiteral("forwardAdcRaw") ? QStringLiteral("FWD ADC:")
                                                       : QStringLiteral("REV ADC:");
    const QJsonValue decimals = control.value(QStringLiteral("decimals"));
    if (!decimals.isDouble() || std::floor(decimals.toDouble()) != decimals.toDouble()
        || decimals.toInt(-1) != (raw ? 0 : 2)
        || control.value(QStringLiteral("id")) != QJsonValue(id)
        || control.value(QStringLiteral("label")) != QJsonValue(label)
        || control.value(QStringLiteral("tooltip")) != QJsonValue(QString())
        || control.value(QStringLiteral("unit")) != QJsonValue(
               power || derivedPower ? QStringLiteral("W") : volts ? QStringLiteral("V") : QString())) {
        return false;
    }
    const MirrorProperty* property = MirrorSchema::forMetaObject(
        &TransmitState::staticMetaObject).byName(name);
    return property && !property->isWritable
        && property->kind == (raw ? MirrorWireKind::Int64 : MirrorWireKind::Float64)
        && MirrorPolicy::hasExplicitEntry(QByteArrayLiteral("TransmitState"), name)
        && MirrorPolicy::directionFor(QByteArrayLiteral("TransmitState"), name)
            == MirrorDirection::Outbound;
}

bool SetupDescription::validatePaDriveReadoutBinding(const QJsonObject& control)
{
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    const QJsonObject ref = binding.value(QStringLiteral("property")).toObject();
    if (control.size() != 9 || binding.size() != 1 || ref.size() != 2
        || ref != QJsonObject{{QStringLiteral("object"), QStringLiteral("transmit")},
                              {QStringLiteral("name"), QStringLiteral("power")}}
        || control.value(QStringLiteral("id")) != QJsonValue(QStringLiteral("pa.values.drive"))
        || control.value(QStringLiteral("label")) != QJsonValue(QStringLiteral("Drive:"))
        || control.value(QStringLiteral("tooltip")) != QJsonValue(QString())
        || control.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("readout"))
        || control.value(QStringLiteral("applies")) != QJsonValue(QStringLiteral("live"))
        || control.value(QStringLiteral("gate")).toObject()
            != QJsonObject{{QStringLiteral("capability"), QStringLiteral("transmitSettingsVersion")},
                           {QStringLiteral("min"), 1}}
        || control.value(QStringLiteral("decimals")) != QJsonValue(0)
        || control.value(QStringLiteral("unit")) != QJsonValue(QStringLiteral("W"))) {
        return false;
    }
    const QByteArray name = QByteArrayLiteral("power");
    const MirrorProperty* property = MirrorSchema::forMetaObject(
        &TransmitModel::staticMetaObject).byName(name);
    return property && property->isWritable && property->kind == MirrorWireKind::Int64
        && MirrorPolicy::hasExplicitEntry(QByteArrayLiteral("TransmitModel"), name)
        && MirrorPolicy::directionFor(QByteArrayLiteral("TransmitModel"), name)
            == MirrorDirection::Bidirectional;
}

bool SetupDescription::validatePaTelemetryReadoutBinding(const QJsonObject& control)
{
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    const QJsonObject ref = binding.value(QStringLiteral("telemetry")).toObject();
    if (control.size() != 10 || binding.size() != 1 || ref.size() != 2
        || ref.value(QStringLiteral("object")) != QJsonValue(QStringLiteral("radio"))
        || control.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("readout"))
        || control.value(QStringLiteral("applies")) != QJsonValue(QStringLiteral("live"))
        || control.value(QStringLiteral("tooltip")) != QJsonValue(QString())
        || control.value(QStringLiteral("requiresDescriptionVersion")) != QJsonValue(5)
        || control.value(QStringLiteral("gate")).toObject()
            != QJsonObject{{QStringLiteral("capability"), QStringLiteral("stationTelemetryVersion")},
                           {QStringLiteral("min"), 4}}) {
        return false;
    }
    const QString name = ref.value(QStringLiteral("name")).toString();
    if (name == QLatin1String("paCurrentAmps")) {
        return control.value(QStringLiteral("id")) == QJsonValue(QStringLiteral("pa.values.paCurrent"))
            && control.value(QStringLiteral("label")) == QJsonValue(QStringLiteral("PA Current:"))
            && control.value(QStringLiteral("decimals")) == QJsonValue(2)
            && control.value(QStringLiteral("unit")) == QJsonValue(QStringLiteral("A"));
    }
    if (name == QLatin1String("supplyVolts")) {
        return control.value(QStringLiteral("id")) == QJsonValue(QStringLiteral("pa.values.dcVoltage"))
            && control.value(QStringLiteral("label")) == QJsonValue(QStringLiteral("DC Voltage:"))
            && control.value(QStringLiteral("decimals")) == QJsonValue(1)
            && control.value(QStringLiteral("unit")) == QJsonValue(QStringLiteral("V"));
    }
    return false;
}

bool SetupDescription::validatePaBypassBinding(const QJsonObject& control)
{
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    const QJsonObject ref = binding.value(QStringLiteral("property")).toObject();
    if (control.size() != 7 || binding.size() != 1 || ref.size() != 2
        || ref.value(QStringLiteral("object")) != QJsonValue(QStringLiteral("transmit"))
        || ref.value(QStringLiteral("name")) != QJsonValue(QStringLiteral("paSettingsBypass"))
        || control.value(QStringLiteral("id")) != QJsonValue(QStringLiteral("pa.gain.bypassPaSettings"))
        || control.value(QStringLiteral("label")) != QJsonValue(QStringLiteral("Bypass ANAN PA Settings"))
        || control.value(QStringLiteral("tooltip")) != QJsonValue(QStringLiteral(
            "Bypass the board-specific PA calibration table (BP PA). "
            "When checked, the generic Hermes gain row is used instead "
            "of the ANAN-G2E factory row. Useful if you have not yet "
            "calibrated PA gain for this radio."))
        || control.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("toggle"))
        || control.value(QStringLiteral("applies")) != QJsonValue(QStringLiteral("live"))
        || control.value(QStringLiteral("gate")).toObject() != QJsonObject{
            {QStringLiteral("capability"), QStringLiteral("transmitSettingsVersion")},
            {QStringLiteral("min"), 6}, {QStringLiteral("offAir"), true}}) {
        return false;
    }
    const QByteArray name = QByteArrayLiteral("paSettingsBypass");
    const MirrorProperty* property = MirrorSchema::forMetaObject(
        &TransmitModel::staticMetaObject).byName(name);
    return property && property->isWritable && property->kind == MirrorWireKind::Bool
        && MirrorPolicy::hasExplicitEntry(QByteArrayLiteral("TransmitModel"), name)
        && MirrorPolicy::inboundAllowed(QByteArrayLiteral("TransmitModel"), name);
}

bool SetupDescription::validateTransmitSettingBinding(const QJsonObject& control)
{
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    if (binding.size() != 1 || !binding.value(QStringLiteral("setting")).isString()) {
        return false;
    }
    const QString key = binding.value(QStringLiteral("setting")).toString();
    const QJsonObject gate = control.value(QStringLiteral("gate")).toObject();
    if (classifySettingsKey(key) != SettingsScope::Station
        || gate.value(QStringLiteral("transmit")) != QJsonValue(true)
        || gate.value(QStringLiteral("capability")) != QJsonValue(QStringLiteral("transmitSettingsVersion"))
        || gate.value(QStringLiteral("min")).toInt() < 5
        || gate.value(QStringLiteral("offAir")) != QJsonValue(true)) {
        return false;
    }
    if (key == QLatin1String("SwrProtectionLimit")) {
        return control.value(QStringLiteral("kind")) == QJsonValue(QStringLiteral("decimal"))
            && control.value(QStringLiteral("min")) == QJsonValue(1.0)
            && control.value(QStringLiteral("max")) == QJsonValue(5.0)
            && control.value(QStringLiteral("step")) == QJsonValue(0.1);
    }
    if (key == QLatin1String("TunePowerSwrIgnore")) {
        return control.value(QStringLiteral("kind")) == QJsonValue(QStringLiteral("integer"))
            && control.value(QStringLiteral("min")) == QJsonValue(5)
            && control.value(QStringLiteral("max")) == QJsonValue(50)
            && control.value(QStringLiteral("step")) == QJsonValue(1);
    }
    static const QSet<QString> kToggles{
        QStringLiteral("SwrProtectionEnabled"), QStringLiteral("SwrTuneProtectionEnabled"),
        QStringLiteral("WindBackPowerSwr"), QStringLiteral("TxInhibitMonitorEnabled"),
        QStringLiteral("TxInhibitMonitorReversed")};
    return kToggles.contains(key) && validateSettingToggleEncoding(control);
}

bool SetupDescription::validateAudioPropertyBinding(const QJsonObject& control)
{
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    if (binding.size() != 1 || !binding.value(QStringLiteral("property")).isObject()) {
        return false;
    }
    const QJsonObject ref = binding.value(QStringLiteral("property")).toObject();
    if (ref.size() != 2 || ref.value(QStringLiteral("object")) != QJsonValue(QStringLiteral("transmit"))) {
        return false;
    }
    const QByteArray name = ref.value(QStringLiteral("name")).toString().toUtf8();
    const int version = name == QByteArrayLiteral("filterLow")
        || name == QByteArrayLiteral("filterHigh") ? 1
        : name == QByteArrayLiteral("amCarrierLevel") ? 2 : 0;
    const QJsonObject gate = control.value(QStringLiteral("gate")).toObject();
    if (version == 0 || control.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("integer"))
        || gate.value(QStringLiteral("transmit")) != QJsonValue(true)
        || gate.value(QStringLiteral("capability")) != QJsonValue(QStringLiteral("transmitSettingsVersion"))
        || gate.value(QStringLiteral("min")) != QJsonValue(version)
        || gate.value(QStringLiteral("offAir")) != QJsonValue(true)) {
        return false;
    }
    const MirrorProperty* property = MirrorSchema::forMetaObject(&TransmitModel::staticMetaObject).byName(name);
    return property && property->isWritable && property->kind == MirrorWireKind::Int64
        && MirrorPolicy::inboundAllowed(QByteArrayLiteral("TransmitModel"), name);
}

bool SetupDescription::validateDspSettingBinding(const QJsonObject& control)
{
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    if (binding.size() != 1 || !binding.value(QStringLiteral("setting")).isString()) {
        return false;
    }
    const QString key = binding.value(QStringLiteral("setting")).toString();
    if (classifySettingsKey(key) != SettingsScope::Station) {
        return false;
    }
    const QString kind = control.value(QStringLiteral("kind")).toString();
    if (key == QLatin1String("DspOptionsCacheImpulse")
        || key == QLatin1String("DspOptionsCacheImpulseSaveRestore")) {
        return kind == QLatin1String("toggle")
            && validateSettingToggleEncoding(control);
    }
    if (kind != QLatin1String("choice")) {
        return false;
    }
    for (const QString& family : {QStringLiteral("BufferSize"), QStringLiteral("FilterSize"),
                                  QStringLiteral("FilterType")}) {
        for (const QString& mode : {QStringLiteral("Phone"), QStringLiteral("Fm"),
                                    QStringLiteral("Cw"), QStringLiteral("Dig")}) {
            for (const QString& side : {QStringLiteral("Rx"), QStringLiteral("Tx")}) {
                if (mode == QLatin1String("Cw") && side == QLatin1String("Tx")) {
                    continue;
                }
                if (key == QStringLiteral("DspOptions") + family + mode + side) {
                    const QJsonArray choices = control.value(QStringLiteral("choices")).toArray();
                    const QStringList expected = family == QLatin1String("BufferSize")
                        ? QStringList{QStringLiteral("64"), QStringLiteral("128"),
                                      QStringLiteral("256"), QStringLiteral("512"),
                                      QStringLiteral("1024")}
                        : family == QLatin1String("FilterSize")
                          ? QStringList{QStringLiteral("1024"), QStringLiteral("2048"),
                                        QStringLiteral("4096"), QStringLiteral("8192"),
                                        QStringLiteral("16384")}
                          : QStringList{QStringLiteral("Linear Phase"),
                                        QStringLiteral("Low Latency")};
                    const QJsonObject gate = control.value(QStringLiteral("gate")).toObject();
                    if (choices.size() != expected.size()
                        || (side == QLatin1String("Tx")
                            ? gate.value(QStringLiteral("offAir")) != QJsonValue(true)
                            : gate.contains(QStringLiteral("offAir")))
                        || (side == QLatin1String("Tx")
                            && (gate.value(QStringLiteral("capability")) != QJsonValue(QStringLiteral("transmitSettingsVersion"))
                                || gate.value(QStringLiteral("min")).toInt() < 1))) {
                        return false;
                    }
                    for (int i = 0; i < expected.size(); ++i) {
                        if (choices.at(i).toString() != expected.at(i)) {
                            return false;
                        }
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

bool SetupDescription::validateCommandBinding(const QJsonObject& control, QString* error)
{
    const auto fail = [error](const QString& why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    const QString kind = control.value(QStringLiteral("kind")).toString();
    if (control.value(QStringLiteral("id")) == QJsonValue(QStringLiteral("dsp.tnf.add"))) {
        const QJsonObject expected{{QStringLiteral("id"), QStringLiteral("dsp.tnf.add")},
            {QStringLiteral("label"), QStringLiteral("Add")},
            {QStringLiteral("tooltip"), QStringLiteral("Add a notch")},
            {QStringLiteral("kind"), QStringLiteral("button")},
            {QStringLiteral("requiresDescriptionVersion"), 2},
            {QStringLiteral("binding"), QJsonObject{{QStringLiteral("command"), QJsonObject{
                {QStringLiteral("verb"), QStringLiteral("notch.addAtSlice")},
                {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("sliceId"),
                    QJsonObject{{QStringLiteral("$selectedOwnedSliceId"), true}}}}}}}}},
            {QStringLiteral("applies"), QStringLiteral("live")},
            {QStringLiteral("gate"), QJsonObject{{QStringLiteral("capability"),
                QStringLiteral("notchControlVersion")}, {QStringLiteral("min"), 2}}}};
        if (control != expected) {
            return fail(QStringLiteral("TNF Add must use the exact selected owned slice contract"));
        }
    }
    const MirrorWireKind controlKind = kind == QLatin1String("toggle") ? MirrorWireKind::Bool
        : kind == QLatin1String("integer") || kind == QLatin1String("slider")
          || kind == QLatin1String("choice") ? MirrorWireKind::Int64
        : kind == QLatin1String("decimal") ? MirrorWireKind::Float64
        : kind == QLatin1String("text") || kind == QLatin1String("colour")
          ? MirrorWireKind::Utf8 : MirrorWireKind::Unsupported;
    const QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
    if (binding.size() != 1 || !binding.value(QStringLiteral("command")).isObject()) {
        return fail(QStringLiteral("a command control needs exactly one command binding"));
    }
    const QJsonObject command = binding.value(QStringLiteral("command")).toObject();
    const QByteArray verb = command.value(QStringLiteral("verb")).toString().toUtf8();
    const CommandVerbSpec* spec = nullptr;
    for (const CommandVerbSpec& candidate : SessionCommandDispatcher::verbSpecs()) {
        if (candidate.verb == verb) {
            spec = &candidate;
            break;
        }
    }
    if (!spec) {
        return fail(QStringLiteral("unknown command verb"));
    }
    const QJsonObject gate = control.value(QStringLiteral("gate")).toObject();
    if (!spec->capability.isEmpty()
        && (gate.value(QStringLiteral("capability")).toString().toUtf8() != spec->capability
            || gate.value(QStringLiteral("min")).toInt() < spec->capabilityVersion)) {
        return fail(QStringLiteral("command capability gate is missing or too low"));
    }
    const auto propertyKind = [](const QJsonValue& raw) {
        if (!raw.isObject()) {
            return MirrorWireKind::Unsupported;
        }
        const QJsonObject ref = raw.toObject();
        if (ref.size() != 2 || ref.value(QStringLiteral("object")).toString().isEmpty()
            || ref.value(QStringLiteral("name")).toString().isEmpty()) {
            return MirrorWireKind::Unsupported;
        }
        // Every static object named here has an allowlisted mirror schema.
        // Future categories extend this map alongside their object bindings.
        const QString object = ref.value(QStringLiteral("object")).toString();
        const QMetaObject* meta = object == QLatin1String("stationTci")
            ? &StationTciModel::staticMetaObject
            : object == QLatin1String("slice:active")
              ? &SliceModel::staticMetaObject : nullptr;
        if (!meta) {
            return MirrorWireKind::Unsupported;
        }
        const MirrorProperty* property = MirrorSchema::forMetaObject(meta).byName(
            ref.value(QStringLiteral("name")).toString().toUtf8());
        return property ? property->kind : MirrorWireKind::Unsupported;
    };
    if (command.contains(QStringLiteral("valueProperty"))
        && propertyKind(command.value(QStringLiteral("valueProperty"))) != controlKind) {
        return fail(QStringLiteral("command valueProperty is missing or has the wrong type"));
    }
    if (!command.value(QStringLiteral("arguments")).isObject()) {
        return fail(QStringLiteral("command arguments are missing"));
    }
    const QJsonObject args = command.value(QStringLiteral("arguments")).toObject();
    int required = 0;
    for (const CommandArgumentSpec& expected : spec->arguments) {
        const QString name = QString::fromUtf8(expected.name);
        if (!expected.optional) {
            ++required;
        }
        if (!args.contains(name)) {
            if (expected.optional) { continue; }
            return fail(QStringLiteral("required command argument is missing: %1").arg(name));
        }
        const QJsonValue value = args.value(name);
        MirrorWireKind supplied = MirrorWireKind::Unsupported;
        if (value.isObject()) {
            const QJsonObject source = value.toObject();
            if (source.size() != 1) {
                return fail(QStringLiteral("command argument source must have one marker"));
            }
            if (source.contains(QStringLiteral("$controlValue"))) {
                if (source.value(QStringLiteral("$controlValue")) != QJsonValue(true)) {
                    return fail(QStringLiteral("$controlValue must be true"));
                }
                supplied = controlKind;
            } else if (source.contains(QStringLiteral("$selectedOwnedSliceId"))) {
                if (control.value(QStringLiteral("id")) != QJsonValue(QStringLiteral("dsp.tnf.add"))
                    || control.value(QStringLiteral("requiresDescriptionVersion")) != QJsonValue(2)
                    || verb != "notch.addAtSlice" || name != QLatin1String("sliceId")
                    || source.value(QStringLiteral("$selectedOwnedSliceId")) != QJsonValue(true)) {
                    return fail(QStringLiteral("invalid selected owned slice source"));
                }
                supplied = MirrorWireKind::Int64;
            } else if (source.contains(QStringLiteral("$property"))) {
                supplied = propertyKind(source.value(QStringLiteral("$property")));
            } else {
                return fail(QStringLiteral("unknown command argument source"));
            }
        } else if (value.isBool()) {
            supplied = MirrorWireKind::Bool;
        } else if (value.isString()) {
            supplied = MirrorWireKind::Utf8;
        } else if (value.isDouble()) {
            supplied = std::floor(value.toDouble()) == value.toDouble()
                ? MirrorWireKind::Int64 : MirrorWireKind::Float64;
        }
        if (supplied != expected.kind) {
            return fail(QStringLiteral("command argument has wrong type: %1").arg(name));
        }
    }
    if (args.size() < required || args.size() > spec->arguments.size()) {
        return fail(QStringLiteral("command has extra or missing arguments"));
    }
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        bool known = false;
        for (const CommandArgumentSpec& expected : spec->arguments) {
            known |= it.key().toUtf8() == expected.name;
        }
        if (!known) {
            return fail(QStringLiteral("command has an unknown argument"));
        }
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool SetupDescription::validateSettingsHygienePanel(const QJsonObject& control)
{
    // This panel is a fixed description of the existing two hygiene verbs,
    // not a command binding or a new generic result/argument language.
    return control == expectedSettingsHygienePanel();
}

bool SetupDescription::validateAntennaRowsTable(const QJsonObject& control, HPSDRModel model)
{
    const QString id = control.value(QStringLiteral("id")).toString();
    const bool tx = id == QLatin1String("hardware.antenna.txRows");
    if (!tx && id != QLatin1String("hardware.antenna.rxRows")) { return false; }
    if (control != expectedAntennaRowsTable(tx, model)) { return false; }
    // The closed table maps the existing 14-value CSV mirrors to only the
    // already admitted per-band verbs. No table-supplied command is parsed.
    const MirrorSchema& schema = MirrorSchema::forMetaObject(
        &AlexAntennaFacade::staticMetaObject);
    const auto hasSource = [&schema](const QByteArray& name, MirrorWireKind kind) {
        const MirrorProperty* property = schema.byName(name);
        return property && property->kind == kind
            && MirrorPolicy::hasExplicitEntry(QByteArrayLiteral("AlexAntennaFacade"), name);
    };
    return hasSource("txAntennas", MirrorWireKind::Utf8)
        && hasSource("rxAntennas", MirrorWireKind::Utf8)
        && hasSource("rxOnlyAntennas", MirrorWireKind::Utf8)
        && hasSource("blockTxAnt2", MirrorWireKind::Bool)
        && hasSource("blockTxAnt3", MirrorWireKind::Bool);
}

bool SetupDescription::validateTnfTable(const QJsonObject& control, QString* error)
{
    const auto fail = [error](const QString& why) {
        if (error) { *error = why; }
        return false;
    };
    const QJsonObject expectedBinding{{QStringLiteral("table"), QJsonObject{
        {QStringLiteral("valueProperty"), QJsonObject{{QStringLiteral("object"), QStringLiteral("notches")},
                                                     {QStringLiteral("name"), QStringLiteral("listJson")}}},
        {QStringLiteral("revisionProperty"), QJsonObject{{QStringLiteral("object"), QStringLiteral("notches")},
                                                        {QStringLiteral("name"), QStringLiteral("revision")}}},
        {QStringLiteral("format"), QStringLiteral("json-array")},
        {QStringLiteral("rowKey"), QStringLiteral("id")},
        {QStringLiteral("maxRows"), NotchModel::kMaxNotches}}}};
    if (control.value(QStringLiteral("id")) != QJsonValue(QStringLiteral("dsp.tnf.list"))
        || control.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("table"))
        || control.size() != 10
        || control.value(QStringLiteral("label"))
            != QJsonValue(QStringLiteral("Tunable Notch Filter"))
        || control.value(QStringLiteral("tooltip")) != QJsonValue(QStringLiteral(""))
        || control.value(QStringLiteral("requiresDescriptionVersion")) != QJsonValue(2)
        || control.value(QStringLiteral("applies")) != QJsonValue(QStringLiteral("live"))
        || control.value(QStringLiteral("gate")).toObject()
            != QJsonObject{{QStringLiteral("capability"), QStringLiteral("notchControlVersion")},
                           {QStringLiteral("min"), 1}}
        || control.value(QStringLiteral("binding")).toObject() != expectedBinding) {
        return fail(QStringLiteral("TNF table source, version or gate is invalid"));
    }
    const MirrorSchema& schema = MirrorSchema::forMetaObject(&NotchModel::staticMetaObject);
    const MirrorProperty* list = schema.byName("listJson");
    const MirrorProperty* revision = schema.byName("revision");
    if (!list || list->kind != MirrorWireKind::Utf8
        || !revision || revision->kind != MirrorWireKind::Int64
        || MirrorPolicy::directionFor("NotchModel", "listJson") != MirrorDirection::Outbound
        || MirrorPolicy::directionFor("NotchModel", "revision") != MirrorDirection::Outbound) {
        return fail(QStringLiteral("TNF table source is not an outbound mirrored list"));
    }
    const QJsonArray columns = control.value(QStringLiteral("columns")).toArray();
    if (columns.size() != 4) { return fail(QStringLiteral("TNF table needs four columns")); }
    const QJsonArray expectedColumns{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("dsp.tnf.list.centreHz")},
                    {QStringLiteral("field"), QStringLiteral("centreHz")},
                    {QStringLiteral("label"), QStringLiteral("Center Frequency (Hz)")},
                    {QStringLiteral("tooltip"), QStringLiteral("Center frequency of the notch")},
                    {QStringLiteral("kind"), QStringLiteral("decimal")},
                    {QStringLiteral("min"), NotchModel::kMinNotchCentreHz},
                    {QStringLiteral("max"), NotchModel::kMaxNotchCentreHz},
                    {QStringLiteral("step"), 1}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("dsp.tnf.list.widthHz")},
                    {QStringLiteral("field"), QStringLiteral("widthHz")},
                    {QStringLiteral("label"), QStringLiteral("Width (Hz)")},
                    {QStringLiteral("tooltip"), QStringLiteral("Bandwdith of the notch")},
                    {QStringLiteral("kind"), QStringLiteral("decimal")},
                    {QStringLiteral("min"), 0}, {QStringLiteral("max"), NotchModel::kMaxNotchWidthHz},
                    {QStringLiteral("step"), 1}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("dsp.tnf.list.active")},
                    {QStringLiteral("field"), QStringLiteral("active")},
                    {QStringLiteral("label"), QStringLiteral("Active")},
                    {QStringLiteral("tooltip"), QStringLiteral("Checked if the notch is active")},
                    {QStringLiteral("kind"), QStringLiteral("toggle")}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("dsp.tnf.list.delete")},
                    {QStringLiteral("rowAction"), QStringLiteral("delete")},
                    {QStringLiteral("label"), QStringLiteral("Delete")},
                    {QStringLiteral("tooltip"), QStringLiteral("Delete the current notch index")},
                    {QStringLiteral("kind"), QStringLiteral("button")}}};
    if (columns != expectedColumns) { return fail(QStringLiteral("TNF table column schema is invalid")); }
    const QJsonArray actions = control.value(QStringLiteral("rowActions")).toArray();
    if (actions.size() != 3) { return fail(QStringLiteral("TNF table needs three row actions")); }
    const auto source = [](const QString& marker, const QString& field) {
        return QJsonObject{{marker, field}};
    };
    const QJsonArray expectedActions{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("move")},
                    {QStringLiteral("command"), QJsonObject{
                        {QStringLiteral("verb"), QStringLiteral("notch.move")},
                        {QStringLiteral("arguments"), QJsonObject{
                            {QStringLiteral("id"), source(QStringLiteral("$row"), QStringLiteral("id"))},
                            {QStringLiteral("centreHz"), source(QStringLiteral("$edit"), QStringLiteral("centreHz"))},
                            {QStringLiteral("widthHz"), source(QStringLiteral("$edit"), QStringLiteral("widthHz"))}}}}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("active")},
                    {QStringLiteral("command"), QJsonObject{
                        {QStringLiteral("verb"), QStringLiteral("notch.setActive")},
                        {QStringLiteral("arguments"), QJsonObject{
                            {QStringLiteral("id"), source(QStringLiteral("$row"), QStringLiteral("id"))},
                            {QStringLiteral("active"), source(QStringLiteral("$edit"), QStringLiteral("active"))}}}}}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("delete")},
                    {QStringLiteral("command"), QJsonObject{
                        {QStringLiteral("verb"), QStringLiteral("notch.delete")},
                        {QStringLiteral("arguments"), QJsonObject{
                            {QStringLiteral("id"), source(QStringLiteral("$row"), QStringLiteral("id"))}}}}}}};
    if (actions != expectedActions) { return fail(QStringLiteral("TNF row action schema is invalid")); }
    const QList<CommandVerbSpec>& specs = SessionCommandDispatcher::verbSpecs();
    const auto hasVerb = [&specs](const QByteArray& verb,
                                  const QList<QByteArray>& names,
                                  const QList<MirrorWireKind>& kinds) {
        for (const CommandVerbSpec& spec : specs) {
            if (spec.verb != verb || spec.capability != "notchControlVersion"
                || spec.capabilityVersion != 1 || spec.arguments.size() != kinds.size()) {
                continue;
            }
            bool matched = true;
            for (int i = 0; i < kinds.size(); ++i) {
                matched &= !spec.arguments.at(i).optional
                    && spec.arguments.at(i).name == names.at(i)
                    && spec.arguments.at(i).kind == kinds.at(i);
            }
            if (matched) { return true; }
        }
        return false;
    };
    if (!hasVerb("notch.move", {"id", "centreHz", "widthHz"},
                 {MirrorWireKind::Int64, MirrorWireKind::Float64,
                                 MirrorWireKind::Float64})
        || !hasVerb("notch.setActive", {"id", "active"},
                    {MirrorWireKind::Int64, MirrorWireKind::Bool})
        || !hasVerb("notch.delete", {"id"}, {MirrorWireKind::Int64})) {
        return fail(QStringLiteral("TNF row verbs no longer match typed Core commands"));
    }
    if (error) { error->clear(); }
    return true;
}

QString SetupDescription::fitCategoryForVersion(const QString& description, int version,
                                                bool antennaRowsAvailable)
{
    if (version < 1 || description.isEmpty()) { return {}; }
    const QJsonDocument document = QJsonDocument::fromJson(description.toUtf8());
    if (!document.isObject()) { return {}; }
    QJsonObject category = document.object();
    const QString categoryId = category.value(QStringLiteral("category")).toObject()
        .value(QStringLiteral("id")).toString();
    QJsonArray pages;
    for (const QJsonValue& rawPage : category.value(QStringLiteral("pages")).toArray()) {
        QJsonObject page = rawPage.toObject();
        QJsonArray sections;
        for (const QJsonValue& rawSection : page.value(QStringLiteral("sections")).toArray()) {
            QJsonObject section = rawSection.toObject();
            QJsonArray controls;
            for (const QJsonValue& rawControl : section.value(QStringLiteral("controls")).toArray()) {
                QJsonObject control = rawControl.toObject();
                if (control.value(QStringLiteral("requiresDescriptionVersion")).toInt(1) <= version
                    && (antennaRowsAvailable || !control.value(QStringLiteral("binding"))
                            .toObject().contains(QStringLiteral("antennaRows")))) {
                    if (version < 4 && (categoryId == QLatin1String("display")
                                        || categoryId == QLatin1String("appearance"))) {
                        control.remove(QStringLiteral("default"));
                        if (control.value(QStringLiteral("kind")) == QJsonValue(QStringLiteral("decimal"))) {
                            control.remove(QStringLiteral("decimals"));
                        }
                    }
                    controls.append(control);
                }
            }
            if (!controls.isEmpty()) {
                section.insert(QStringLiteral("controls"), controls);
                sections.append(section);
            }
        }
        if (!sections.isEmpty()) {
            page.insert(QStringLiteral("sections"), sections);
            pages.append(page);
        }
    }
    if (pages.isEmpty()) { return {}; }
    category.insert(QStringLiteral("pages"), pages);
    const int ceiling = categoryId == QLatin1String("hardware") ? 6
        : categoryId == QLatin1String("pa") ? 5
        : categoryId == QLatin1String("appearance") ? 7
        : categoryId == QLatin1String("display") ? 11 : 3;
    category.insert(QStringLiteral("version"),
                    categoryId == QLatin1String("appearance") && version < 7
                        ? qMin(version, 4)
                        : categoryId == QLatin1String("display") && version < 8
                            ? qMin(version, 4) : qMin(version, ceiling));
    if (version >= 2 && category.value(QStringLiteral("category")).toObject()
            .value(QStringLiteral("id")) == QJsonValue(QStringLiteral("dsp"))) {
        category.insert(QStringLiteral("coverage"), QStringLiteral(
            "partial: Filter Presets and other listed page controls remain incomplete"));
        QJsonArray fittedPages = category.value(QStringLiteral("pages")).toArray();
        for (int i = 0; i < fittedPages.size(); ++i) {
            QJsonObject page = fittedPages.at(i).toObject();
            if (page.value(QStringLiteral("id")) == QJsonValue(QStringLiteral("dsp.tnf"))) {
                page.insert(QStringLiteral("coverage"), QStringLiteral(
                    "partial: Visual Notch is a phone-local display preference"));
                fittedPages[i] = page;
                break;
            }
        }
        category.insert(QStringLiteral("pages"), fittedPages);
    }
    return QString::fromUtf8(QJsonDocument(category).toJson(QJsonDocument::Compact));
}

SetupDescription::SetupDescription(QObject* parent) : QObject(parent)
{
    initializeSetupResources();
    rebuild();
}

QJsonObject SetupDescription::category(const QString& id) const
{
    const QString* value = nullptr;
    if (id == QLatin1String("general")) { value = &m_general; }
    else if (id == QLatin1String("hardware")) { value = &m_hardware; }
    else if (id == QLatin1String("audio")) { value = &m_audio; }
    else if (id == QLatin1String("dsp")) { value = &m_dsp; }
    else if (id == QLatin1String("display")) { value = &m_display; }
    else if (id == QLatin1String("transmit")) { value = &m_transmit; }
    else if (id == QLatin1String("appearance")) { value = &m_appearance; }
    else if (id == QLatin1String("catNetwork")) { value = &m_catNetwork; }
    else if (id == QLatin1String("test")) { value = &m_test; }
    else if (id == QLatin1String("diagnostics")) { value = &m_diagnostics; }
    else if (id == QLatin1String("pa")) { value = &m_pa; }
    return value == nullptr || value->isEmpty() ? QJsonObject{}
        : QJsonDocument::fromJson(value->toUtf8()).object();
}

void SetupDescription::setBoardCapabilities(const BoardCapabilities& caps)
{
    m_caps = caps;
    m_model = HPSDRModel::FIRST;
    rebuild();
}

void SetupDescription::setRadioContext(const BoardCapabilities& caps, HPSDRModel model)
{
    m_caps = caps;
    m_model = model;
    rebuild();
}

void SetupDescription::rebuild()
{
    bool changed = false;
    const auto update = [this, &changed](const QString& id, QString& target) {
        const QString description = loadCategory(id, m_caps, m_model);
        if (description != target) {
            target = description;
            changed = true;
        }
    };
    update(QStringLiteral("general"), m_general);
    update(QStringLiteral("hardware"), m_hardware);
    update(QStringLiteral("audio"), m_audio);
    update(QStringLiteral("dsp"), m_dsp);
    update(QStringLiteral("display"), m_display);
    update(QStringLiteral("transmit"), m_transmit);
    update(QStringLiteral("appearance"), m_appearance);
    update(QStringLiteral("catNetwork"), m_catNetwork);
    update(QStringLiteral("test"), m_test);
    update(QStringLiteral("diagnostics"), m_diagnostics);
    update(QStringLiteral("pa"), m_pa);
    if (changed) {
        ++m_revision;
        emit descriptionsChanged();
    }
}

} // namespace NereusSDR
