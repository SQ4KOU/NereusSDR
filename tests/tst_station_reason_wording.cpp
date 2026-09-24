// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_station_reason_wording.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan, Task 4b (R-IOS-01, R-R3-21): every reason the station
// sends an app is plain operator words. The phone shows a reason exactly as
// sent, so a reason worded for developers (a function or class name, a
// requirement or phase name such as "R4", or "session", "revision",
// "capability", "actuation" and the like) is a bug the operator reads.
//
// How the reasons are found: a source scan, not a table, because the
// reasons are written where each refusal is decided (the dispatcher, the
// server, the facades, the accessory and settings code, the media
// controller), and a table would be a second copy that can drift from them.
//
//   * Each file in kReasonSources is read with its comments removed and
//     adjacent literals joined. In a whole-file entry every string literal
//     with a space in it outside a log statement is a reason and is
//     checked; a literal there that never reaches an app is named in the
//     entry's notReasons, by its start, with why. In a function entry only
//     the named functions' bodies are read (RadioModel.cpp and the models
//     mix the station's reasons with this app's own text).
//   * The guard: every function in src/core and src/models whose name says
//     it words a reason (…Reason, …Refusal, …ForStation, …FromStation,
//     applyMirroredValue), and every file that sends a reason to an app
//     (commandResult, sessionEnd, authResult, settingsReject,
//     propertyResult, emitResult, dropPeer, sendRejected,
//     sendAllocationResult, rejectAllocation), must be scanned here or
//     named in kAppSideReasons with why. A new reason site fails until it
//     is placed.
//   * What the station actually sent: every reason in a station message of
//     the link's session fixtures (tests/data/link/v1/sessions) is checked
//     the same way.
//
// Machine-readable codes are not reasons and keep their spelling: the
// receiver audio context's reasons (client-disabled, receiver-limit, ...),
// displayBudgetReason, and the two display retire reasons windows already
// in use compare as they are ("slice removed", "slice stream binding
// changed"). The last two are plain as written and are scanned anyway.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 4b (R-IOS-01,
//                                    R-R3-21): created. AI-assisted via
//                                    Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include "OperatorWording.h"

using namespace NereusSDR;

namespace {

QString sourcePath(const QString& relative)
{
    return QDir(QStringLiteral(NEREUS_SOURCE_DIR)).filePath(relative);
}

bool identifierChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

// The end (one past the closing quote) of the string or character literal
// that opens at `i`.
qsizetype literalEnd(const QString& text, qsizetype i)
{
    const QChar quote = text.at(i);
    qsizetype j = i + 1;
    while (j < text.size() && text.at(j) != quote && text.at(j) != QLatin1Char('\n')) {
        j += text.at(j) == QLatin1Char('\\') ? 2 : 1;
    }
    return qMin(j + 1, text.size());
}

bool opensCharLiteral(const QString& text, qsizetype i)
{
    return text.at(i) == QLatin1Char('\'') && (i == 0 || !identifierChar(text.at(i - 1)));
}

// A source file's code with its comments blanked and adjacent string
// literals joined ("a" "b" -> "ab"), as the compiler reads it.
QString codeOf(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QString text = QString::fromUtf8(file.readAll());
    QString code;
    code.reserve(text.size());
    qsizetype i = 0;
    qsizetype lastLiteralEnd = -1;
    while (i < text.size()) {
        const QChar c = text.at(i);
        const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar();
        if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            while (i < text.size() && text.at(i) != QLatin1Char('\n')) {
                ++i;
            }
            code += QLatin1Char(' ');
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            const qsizetype end = text.indexOf(QStringLiteral("*/"), i + 2);
            i = end < 0 ? text.size() : end + 2;
            code += QLatin1Char(' ');
            continue;
        }
        if (c == QLatin1Char('"') || opensCharLiteral(text, i)) {
            const qsizetype end = literalEnd(text, i);
            QString literal = text.mid(i, end - i);
            if (c == QLatin1Char('"') && lastLiteralEnd >= 0
                && code.mid(lastLiteralEnd).trimmed().isEmpty()) {
                code.truncate(lastLiteralEnd - 1);
                literal.remove(0, 1);
            }
            code += literal;
            lastLiteralEnd = c == QLatin1Char('"') ? code.size() : -1;
            i = end;
            continue;
        }
        if (!c.isSpace()) {
            lastLiteralEnd = -1;
        }
        code += c;
        ++i;
    }
    return code;
}

// The statements of `code`, split at ';' and '}' outside literals.
QStringList statementsOf(const QString& code)
{
    QStringList statements;
    QString current;
    qsizetype i = 0;
    while (i < code.size()) {
        const QChar c = code.at(i);
        if (c == QLatin1Char('"') || opensCharLiteral(code, i)) {
            const qsizetype end = literalEnd(code, i);
            current += code.mid(i, end - i);
            i = end;
            continue;
        }
        if (c == QLatin1Char(';') || c == QLatin1Char('}')) {
            statements.append(current);
            current.clear();
        } else {
            current += c;
        }
        ++i;
    }
    statements.append(current);
    return statements;
}

// Every string literal with a space in `code`, outside log statements, as
// written (escapes kept).
QStringList reasonLiteralsIn(const QString& code)
{
    static const QRegularExpression log(QStringLiteral(
        "\\bq(C(Warning|Info|Debug|Critical)|Warning|Debug|Info|Critical)\\b"));
    static const QRegularExpression literal(QStringLiteral("\"((?:[^\"\\\\\\n]|\\\\.)*)\""));
    QStringList found;
    for (QString statement : statementsOf(code)) {
        const QRegularExpressionMatch logged = log.match(statement);
        if (logged.hasMatch()) {
            statement.truncate(logged.capturedStart());
        }
        QRegularExpressionMatchIterator it = literal.globalMatch(statement);
        while (it.hasNext()) {
            const QString text = it.next().captured(1);
            if (text.contains(QLatin1Char(' ')) && !found.contains(text)) {
                found.append(text);
            }
        }
    }
    return found;
}

// The index one past the bracket that closes the one at `open`.
qsizetype closingOf(const QString& code, qsizetype open, QChar opener, QChar closer)
{
    int depth = 0;
    qsizetype i = open;
    while (i < code.size()) {
        const QChar c = code.at(i);
        if (c == QLatin1Char('"') || opensCharLiteral(code, i)) {
            i = literalEnd(code, i);
            continue;
        }
        if (c == opener) {
            ++depth;
        } else if (c == closer && --depth == 0) {
            return i + 1;
        }
        ++i;
    }
    return code.size();
}

struct FunctionBody {
    QString name;
    QString body;
};

// Every function defined in `code` (a body follows its parameter list)
// whose name matches `names`, with its body.
QList<FunctionBody> functionsIn(const QString& code, const QRegularExpression& names)
{
    static const QRegularExpression qualifiers(
        QStringLiteral("^\\s*(?:(?:const|noexcept|override|final)\\b\\s*)*"));
    QList<FunctionBody> found;
    static const QRegularExpression definition(
        QStringLiteral("\\b(?:[A-Za-z_][A-Za-z0-9_]*::)*([A-Za-z_][A-Za-z0-9_]*)\\s*\\("));
    QRegularExpressionMatchIterator it = definition.globalMatch(code);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString name = match.captured(1);
        if (!names.match(name).hasMatch()) {
            continue;
        }
        const qsizetype afterParams = closingOf(code, match.capturedEnd() - 1,
                                                QLatin1Char('('), QLatin1Char(')'));
        const QRegularExpressionMatch tail = qualifiers.match(code.mid(afterParams, 64));
        const qsizetype brace = afterParams + tail.capturedLength();
        if (brace >= code.size() || code.at(brace) != QLatin1Char('{')) {
            continue;
        }
        const qsizetype end = closingOf(code, brace, QLatin1Char('{'), QLatin1Char('}'));
        found.append({name, code.mid(brace, end - brace)});
    }
    return found;
}

// Developer wording beyond the internal terms: identifiers (camelCase,
// Class::member, call()), dotted verb names, requirement and phase names,
// and words that describe the code rather than what happened.
QString developerWordingIn(const QString& text)
{
    static const QRegularExpression names(QStringLiteral(
        "\\b(?!dB\\b|dBm\\b|kHz\\b)[a-z]+[A-Z][A-Za-z0-9]*\\b|::|\\w\\(\\)|\\b[a-z]+\\.[a-z][A-Za-z0-9]+\\b"
        "|\\bR\\d+\\b|\\bPhase \\d|\\bTask \\d|\\bPS3\\b|\\b[TP]GXL\\b"));
    static const QRegularExpression words(QStringLiteral(
        "\\bactuat|\\bargument|\\bdaemon|\\bhook\\b|\\binbound|\\boutbound|\\bschema"
        "|\\ballocator|\\bcohost|\\bmirror|\\bidentit|\\bmalformed|\\bverb\\b|-scoped\\b"),
        QRegularExpression::CaseInsensitiveOption);
    for (const QRegularExpression* pattern : {&names, &words}) {
        const QRegularExpressionMatch match = pattern->match(text);
        if (match.hasMatch()) {
            return match.captured(0);
        }
    }
    return {};
}

// Why `text` is not plain operator words, or an empty string.
QString wordingProblemIn(const QString& text)
{
    if (text.trimmed().isEmpty()) {
        return QStringLiteral("empty");
    }
    const QString term = OperatorWording::internalTermIn(text);
    return term.isEmpty() ? developerWordingIn(text) : term;
}

struct ReasonSource {
    const char* file;
    QStringList functions;   // Empty: the whole file.
    QStringList notReasons;  // Literals here that never reach an app, by their start.
    int atLeast;             // So the scan cannot pass on nothing.
};

const QList<ReasonSource>& reasonSources()
{
    static const QList<ReasonSource> sources{
        // session.end, auth.result, property.result, settings.reject and
        // the refusals of a verb an older app sends.
        {"src/core/session/StationServer.cpp", {},
         {// start(): the Core's own setup error and the WebSocket server's
          // name, and the pairing banner nereusd prints on its console.
          "Qt reports no working TLS backend", "No authentication token available",
          "NereusSDR station", "\\n  ====="},
         30},
        // command.result for every verb.
        {"src/core/session/SessionCommandDispatcher.cpp", {}, {}, 30},
        // property.result for a write the mirror refuses.
        {"src/core/session/StateMirror.cpp", {}, {}, 5},
        // PureSignal's command.result and its lastActionError.
        {"src/core/session/PureSignalSessionFacade.cpp", {}, {}, 15},
        // Model and correction files; the store's and validator's own
        // messages are detail for the log unless isOperatorMessage says
        // otherwise (DspAssetService::rejectDetail).
        {"src/core/dsp/DspAssetService.cpp", {}, {}, 30},
        {"src/core/dsp/DspAssetValidation.cpp", {QStringLiteral("isOperatorMessage")}, {}, 6},
        {"src/core/dsp/NnrAdapter.cpp", {}, {}, 6},
        {"src/core/accessories/AlexAntennaFacade.cpp", {}, {}, 6},
        {"src/core/StepAttenuatorFacade.cpp", {}, {}, 6},
        {"src/core/IoBoardHl2Facade.cpp", {}, {}, 1},
        {"src/core/SliceStreamAllocator.cpp", {}, {}, 4},
        {"src/core/StationAccessoryData.cpp", {}, {}, 4},
        {"src/core/StationTciController.cpp", {}, {}, 1},
        {"src/core/settings/SettingsProxyServer.cpp", {}, {}, 3},
        {"src/core/settings/SettingsScope.cpp", {QStringLiteral("modelOwnedSettingsRefusal")},
         {}, 5},
        // The display refusals and retirements (rejected, allocation-result).
        {"src/core/session/media/DaemonMediaController.cpp", {},
         {// statsSummary(): a log line's text.
          "largestKeyframe="},
         18},
        {"src/core/session/media/SpectrumEndpoint.h", {}, {}, 3},
        {"src/models/AccessoryDataModel.cpp", {QStringLiteral("readOnlyReason")}, {}, 1},
        {"src/models/AmplifierModel.cpp",
         {QStringLiteral("readOnlyReason"), QStringLiteral("receiveOnlyOperateReason")}, {}, 2},
        {"src/models/RfKitModel.cpp", {QStringLiteral("readOnlyReason")}, {}, 1},
        {"src/models/StationTciModel.cpp", {QStringLiteral("readOnlyReason")}, {}, 1},
        {"src/models/SliceModel.cpp",
         {QStringLiteral("activeWriteReason"), QStringLiteral("applyMirroredValue")}, {}, 5},
        {"src/models/TunerModel.cpp", {QStringLiteral("applyMirroredValue")}, {}, 2},
        {"src/models/RadioModel.cpp",
         {QStringLiteral("applyMirroredValue"), QStringLiteral("setFourO3AEnabledForStation"),
          QStringLiteral("setStationTciForStation"),
          QStringLiteral("setTxInterlockPolicyForStation"),
          QStringLiteral("setPgxlPowerCapForStation"),
          QStringLiteral("clearAccessoryFaultsForStation"),
          QStringLiteral("configureTgxlForStation"), QStringLiteral("disconnectTgxlForStation"),
          QStringLiteral("configurePgxlForStation"), QStringLiteral("disconnectPgxlForStation"),
          QStringLiteral("setPgxlConnectionSettingsForStation"),
          QStringLiteral("setRfKitEnabledForStation"), QStringLiteral("configureRfKitForStation"),
          QStringLiteral("disconnectRfKitForStation"), QStringLiteral("setNnrDiagnosticMode"),
          QStringLiteral("applyNnrModelSelection"), QStringLiteral("addNotchFromStation"),
          QStringLiteral("moveNotchFromStation"), QStringLiteral("setNotchActiveFromStation"),
          QStringLiteral("deleteNotchFromStation"), QStringLiteral("requestIoBoardProbe"),
          QStringLiteral("nr3CannotRunReason")},
         {// This app's own branch in a remote window (role Remote), shown
          // through OperatorReasonText; never sent by the Core.
          "There is no station session."},
         20},
    };
    return sources;
}

// Reason sites that never send to an app, with why.
struct AppSideReason {
    const char* file;
    const char* function;  // Empty: every match in the file.
    const char* why;
};

const QList<AppSideReason>& appSideReasons()
{
    static const QList<AppSideReason> sites{
        {"src/core/session/StationClient.cpp", "",
         "the app's end of the link: its own reasons are shown through OperatorReasonText"},
        {"src/core/session/StationClient.h", "", "the app's end of the link"},
        {"src/core/session/SessionMessages.cpp", "", "encodes a reason it is given"},
        {"src/core/session/SessionMessages.h", "", "declares the messages"},
        {"src/core/session/SessionCommandDispatcher.h", "", "declares emitResult"},
        {"src/core/session/StationServer.h", "", "declares dropPeer"},
        {"src/core/session/media/DaemonMediaController.h", "", "declares sendRejected"},
        {"src/core/audio/RemoteVaxFeeder.cpp", "lastStopReason",
         "a remote window's VAX feeder: why its own audio stopped"},
        {"src/core/session/media/MediaPeer.cpp", "lastStartRefusal",
         "a code the window reads (StartRefusal), not text"},
        {"src/core/HardwareProfile.cpp", "profileForStation", "returns a hardware profile, not text"},
        {"src/core/StepAttenuatorFacade.h", "windowUnavailableReason",
         "a remote window's own reason the attenuator rows show"},
        {"src/core/accessories/AlexAntennaFacade.h", "windowUnavailableReason",
         "a remote window's own reason the antenna rows show"},
        {"src/core/TciServer.h", "operatorNoticeReason", "a remote window's own TCI notice"},
        {"src/models/RadioModel.cpp", "noStationReason",
         "a remote window's own notice when it has no link to the Core"},
        {"src/models/RadioModel.h", "rxFilter0Reason",
         "the filter badge's status label (AlexController), not a refusal"},
        {"src/models/RadioModel.h", "rxFilter1Reason",
         "the filter badge's status label (AlexController), not a refusal"},
    };
    return sites;
}

QStringList sourceFiles(const QStringList& roots)
{
    QStringList files;
    for (const QString& root : roots) {
        QDirIterator it(sourcePath(root), {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            files.append(QDir(QStringLiteral(NEREUS_SOURCE_DIR)).relativeFilePath(it.next()));
        }
    }
    files.sort();
    return files;
}

bool scanned(const QString& file, const QString& function)
{
    for (const ReasonSource& source : reasonSources()) {
        if (file == QLatin1String(source.file)
            && (source.functions.isEmpty() || source.functions.contains(function))) {
            return true;
        }
    }
    return false;
}

bool appSide(const QString& file, const QString& function)
{
    for (const AppSideReason& site : appSideReasons()) {
        if (file == QLatin1String(site.file)
            && (QLatin1String(site.function).isEmpty() || function == QLatin1String(site.function))) {
            return true;
        }
    }
    return false;
}

// Every reason string inside a message: `reason` values at any depth
// (property.result carries one per property).
void collectReasons(const QJsonValue& value, QStringList* reasons)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.key() == QLatin1String("reason") && it.value().isString()) {
                reasons->append(it.value().toString());
            } else {
                collectReasons(it.value(), reasons);
            }
        }
    } else if (value.isArray()) {
        for (const QJsonValue& item : value.toArray()) {
            collectReasons(item, reasons);
        }
    }
}

} // namespace

class TestStationReasonWording : public QObject {
    Q_OBJECT

private slots:
    void theCheckCatchesDeveloperWording()
    {
        // The rule, on the reasons this task retired.
        for (const char* old :
             {"Remote PureSignal actuation requires R4 transmit support.",
              "Model application requires the current selection revision.",
              "missing sliceId argument", "disconnectTgxl takes no arguments",
              "dspAssets.chunk has invalid or missing fields.",
              "SliceModel::%1 has no inbound mirror translation",
              "C-Tune centre is invalid for this stream's cohosts",
              "session display budget exceeded", "handshake deadline expired",
              "key is not Station-scoped"}) {
            QVERIFY2(!wordingProblemIn(QString::fromUtf8(old)).isEmpty(), old);
        }
        for (const char* plain :
             {"PureSignal cannot be run from a remote window yet.",
              "The Core could not read this request.",
              "Update this app to set up the Power Genius on this Core.",
              "Choose an SWR protection limit from %1 to %2."}) {
            QVERIFY2(wordingProblemIn(QString::fromUtf8(plain)).isEmpty(), plain);
        }
    }

    void everyStationReasonIsPlain()
    {
        static const QRegularExpression anyName(QStringLiteral("."));
        QStringList failures;
        int checked = 0;
        for (const ReasonSource& source : reasonSources()) {
            const QString code = codeOf(sourcePath(QString::fromLatin1(source.file)));
            QVERIFY2(!code.isEmpty(), source.file);
            QStringList literals;
            if (source.functions.isEmpty()) {
                literals = reasonLiteralsIn(code);
            } else {
                QStringList seen;
                for (const FunctionBody& function : functionsIn(code, anyName)) {
                    if (!source.functions.contains(function.name)) {
                        continue;
                    }
                    seen.append(function.name);
                    for (const QString& text : reasonLiteralsIn(function.body)) {
                        if (!literals.contains(text)) {
                            literals.append(text);
                        }
                    }
                }
                for (const QString& function : source.functions) {
                    QVERIFY2(seen.contains(function),
                             qPrintable(QStringLiteral("%1: %2 not found")
                                            .arg(QLatin1String(source.file), function)));
                }
            }
            int reasons = 0;
            for (const QString& text : literals) {
                const bool exempt = std::any_of(
                    source.notReasons.cbegin(), source.notReasons.cend(),
                    [&text](const QString& start) { return text.startsWith(start); });
                if (exempt) {
                    continue;
                }
                ++reasons;
                const QString problem = wordingProblemIn(text);
                if (!problem.isEmpty()) {
                    failures.append(QStringLiteral("%1: \"%2\" [%3]")
                                        .arg(QLatin1String(source.file), text, problem));
                }
            }
            QVERIFY2(reasons >= source.atLeast,
                     qPrintable(QStringLiteral("%1: %2 reasons")
                                    .arg(QLatin1String(source.file)).arg(reasons)));
            checked += reasons;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
        QVERIFY2(checked >= 250, qPrintable(QString::number(checked)));
    }

    void everyReasonSiteIsScannedOrOnTheAppSide()
    {
        static const QRegularExpression reasonFunction(
            QStringLiteral("(Reason|Refusal|ForStation|FromStation)$|^applyMirroredValue$"));
        static const QRegularExpression sender(QStringLiteral(
            "\\b(commandResult|sessionEnd|authResult|settingsReject|propertyResult|emitResult"
            "|dropPeer|sendRejected|sendAllocationResult|rejectAllocation)\\s*\\("));
        QStringList unplaced;
        int functions = 0;
        for (const QString& file :
             sourceFiles({QStringLiteral("src/core"), QStringLiteral("src/models")})) {
            const QString code = codeOf(sourcePath(file));
            for (const FunctionBody& function : functionsIn(code, reasonFunction)) {
                ++functions;
                if (!scanned(file, function.name) && !appSide(file, function.name)) {
                    unplaced.append(file + QStringLiteral(": ") + function.name);
                }
            }
            if (sender.match(code).hasMatch() && !scanned(file, QString())
                && !appSide(file, QString())) {
                unplaced.append(file + QStringLiteral(": sends a reason"));
            }
        }
        QVERIFY2(functions >= 30, qPrintable(QString::number(functions)));
        QVERIFY2(unplaced.isEmpty(), qPrintable(unplaced.join(QLatin1Char('\n'))));
    }

    void everyReasonTheFixturesRecordIsPlain()
    {
        const QDir sessions(QStringLiteral(NEREUS_SOURCE_DIR "/tests/data/link/v1/sessions"));
        const QStringList files = sessions.entryList({QStringLiteral("*.json")}, QDir::Files);
        QVERIFY(files.size() >= 25);
        QStringList failures;
        int checked = 0;
        for (const QString& name : files) {
            QFile file(sessions.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QJsonObject fixture = QJsonDocument::fromJson(file.readAll()).object();
            for (const QJsonValue& step : fixture.value(QStringLiteral("steps")).toArray()) {
                const QJsonObject object = step.toObject();
                if (object.value(QStringLiteral("from")).toString() != QLatin1String("station")) {
                    continue;
                }
                QStringList reasons;
                collectReasons(object.value(QStringLiteral("message")), &reasons);
                for (const QString& reason : reasons) {
                    // Empty on success; "$..." is a placeholder the matcher fills.
                    if (reason.isEmpty() || reason.startsWith(QLatin1Char('$'))) {
                        continue;
                    }
                    ++checked;
                    const QString problem = wordingProblemIn(reason);
                    if (!problem.isEmpty()) {
                        failures.append(QStringLiteral("%1: \"%2\" [%3]").arg(name, reason, problem));
                    }
                }
            }
        }
        QVERIFY2(checked >= 60, qPrintable(QString::number(checked)));
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
    }
};

QTEST_GUILESS_MAIN(TestStationReasonWording)
#include "tst_station_reason_wording.moc"
