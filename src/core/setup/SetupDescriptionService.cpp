// no-port-check: NereusSDR-original Setup description transport.
#include "core/setup/SetupDescriptionService.h"
#include "core/session/SessionCommandDispatcher.h"
#include "core/session/MirrorSchema.h"
#include "models/StationTciModel.h"

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
            ? &StationTciModel::staticMetaObject : nullptr;
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
