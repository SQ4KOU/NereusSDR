// no-port-check: NereusSDR-original test helper.
//
// TestFunctionGroups: one QtTest binary run as several ctest entries, so a
// test whose cases add up near ctest's 120 s limit is not one long run
// against it (R-R3-49 load round). tests/CMakeLists.txt's
// nereus_split_test() adds an entry per group, the same binary with an
// environment variable naming the group, and gives the test's own entry
// `rest`. The test's main() hands its arguments through arguments() below.
//
// A group names whole test functions, never single data rows: QtTest
// cannot list a function's rows before it runs them, so a row added later
// would run in no entry. `rest` runs every test function no group names.
// The variable unset, or a run that names its own functions or options,
// runs as asked. A group naming something that is not a test function
// stops the run, so a rename cannot quietly drop a case from every entry.
//
// Modification history (NereusSDR):
//   2026-09-27: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code (R-R3-49).
#pragma once

#include <QList>
#include <QMetaMethod>
#include <QMetaObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <optional>

namespace NereusSDR::TestFunctionGroups {

struct Group {
    QString name;
    /// Whole test functions.
    QStringList functions;
};

/// The test functions QtTest would run: private slots with no arguments,
/// other than the four fixtures and the _data functions.
inline QStringList testFunctions(const QMetaObject* meta)
{
    QStringList names;
    for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        const QString name = QString::fromLatin1(method.name());
        if (method.methodType() != QMetaMethod::Slot
            || method.access() != QMetaMethod::Private || method.parameterCount() != 0
            || name.endsWith(QStringLiteral("_data"))
            || name == QStringLiteral("initTestCase") || name == QStringLiteral("cleanupTestCase")
            || name == QStringLiteral("init") || name == QStringLiteral("cleanup")) {
            continue;
        }
        names.append(name);
    }
    return names;
}

/// The arguments to hand QTest::qExec() for the group `variable` names;
/// nullopt (after a message) when the group or one of its functions is
/// unknown.
inline std::optional<QStringList> arguments(const QMetaObject* meta,
                                            const QStringList& appArguments,
                                            const char* variable,
                                            const QList<Group>& groups)
{
    const QString chosen = qEnvironmentVariable(variable);
    if (chosen.isEmpty() || appArguments.size() != 1) {
        return appArguments;
    }
    const QStringList all = testFunctions(meta);
    QStringList grouped;
    for (const Group& group : groups) {
        for (const QString& function : group.functions) {
            if (!all.contains(function)) {
                qCritical("%s group %s names %s, which is not a test function", variable,
                          qPrintable(group.name), qPrintable(function));
                return std::nullopt;
            }
            grouped.append(function);
        }
    }
    QStringList out = appArguments;
    if (chosen == QStringLiteral("rest")) {
        for (const QString& name : all) {
            if (!grouped.contains(name)) {
                out.append(name);
            }
        }
        return out;
    }
    for (const Group& group : groups) {
        if (group.name == chosen) {
            out += group.functions;
            return out;
        }
    }
    qCritical("%s names no group: %s", variable, qPrintable(chosen));
    return std::nullopt;
}

} // namespace NereusSDR::TestFunctionGroups
