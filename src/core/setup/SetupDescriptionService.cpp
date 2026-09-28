// no-port-check: NereusSDR-original Setup description transport.
#include "core/setup/SetupDescriptionService.h"
#include "core/session/SessionCommandDispatcher.h"
#include "core/session/MirrorSchema.h"
#include "core/session/MirrorPolicy.h"
#include "models/StationTciModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "models/NotchModel.h"
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

QString loadCategory(const QString& id, const BoardCapabilities& caps)
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
    if (root.value(QStringLiteral("version")).toInt() != 1
        || root.value(QStringLiteral("category")).toObject()
               .value(QStringLiteral("id")).toString() != id
        || root.value(QStringLiteral("pages")).toArray().isEmpty()) {
        return {};
    }
    QSet<QString> ids;
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
                    || !SetupDescription::validateSettingToggleEncoding(control)
                    || (id == QLatin1String("dsp")
                        && !control.value(QStringLiteral("binding")).toObject()
                                .contains(QStringLiteral("property"))
                        && !control.value(QStringLiteral("binding")).toObject()
                                .contains(QStringLiteral("command"))
                        && !control.value(QStringLiteral("binding")).toObject()
                                .contains(QStringLiteral("setting")))
                    || (id == QLatin1String("dsp")
                        && control.value(QStringLiteral("binding")).toObject()
                               .contains(QStringLiteral("setting"))
                        && !SetupDescription::validateDspSettingBinding(control))
                    || (id == QLatin1String("dsp")
                        && control.value(QStringLiteral("binding")).toObject()
                               .contains(QStringLiteral("property"))
                        && !SetupDescription::validateActiveSlicePropertyBinding(control))
                    || (id == QLatin1String("transmit")
                        && !SetupDescription::validateTransmitPropertyBinding(control)
                        && !SetupDescription::validateTransmitSettingBinding(control))
                    || (id == QLatin1String("audio")
                        && !SetupDescription::validateAudioPropertyBinding(control))
                    || (control.value(QStringLiteral("binding")).toObject().contains(QStringLiteral("command"))
                        && !SetupDescription::validateCommandBinding(control))) {
                    return {};
                }
                ids.insert(controlId);
            }
        }
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
        || gate.contains(QStringLiteral("offAir"))) {
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
        || gate.contains(QStringLiteral("offAir"))) {
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
        || gate.contains(QStringLiteral("offAir"))) {
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
    return value == nullptr || value->isEmpty() ? QJsonObject{}
        : QJsonDocument::fromJson(value->toUtf8()).object();
}

void SetupDescription::setBoardCapabilities(const BoardCapabilities& caps)
{
    m_caps = caps;
    rebuild();
}

void SetupDescription::rebuild()
{
    bool changed = false;
    const auto update = [this, &changed](const QString& id, QString& target) {
        const QString description = loadCategory(id, m_caps);
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
    if (changed) {
        ++m_revision;
        emit descriptionsChanged();
    }
}

} // namespace NereusSDR
