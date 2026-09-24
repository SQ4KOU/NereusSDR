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
//     checked, and so is every literal of any length in a reason position:
//     a sender's reason argument (emitResult, commandResult, sessionEnd,
//     authResult, settingsReject, dropPeer, sendRejected,
//     sendAllocationResult, rejectAllocation, and the reject, rejectDetail
//     and fail helpers) or the right-hand side of an assignment to a
//     reason (…reason, …refusal, m_lastError, m_lastActionError). A
//     reason of one word is not a sentence an operator can act on and
//     fails. What .arg() inserts into a reason is part of it: an inserted
//     literal is checked as words, and any other inserted expression must
//     be named in the entry's plainInserts. A literal that never reaches
//     an app is named in the entry's notReasons, by its start, with why.
//     In a function entry only the named functions' bodies are read
//     (RadioModel.cpp and the models mix the station's reasons with this
//     app's own text).
//   * The guard: every function in src/core and src/models whose name says
//     it words a reason (…Reason, …Refusal, …ForStation, …FromStation,
//     applyMirroredValue) or that writes one through a QString*
//     out-parameter (…reason, …refusal), and every file that sends a
//     reason to an app (commandResult, sessionEnd, authResult,
//     settingsReject, propertyResult, emitResult, dropPeer, sendRejected,
//     sendAllocationResult, rejectAllocation), must be scanned here or
//     named in kAppSideReasons with why. A new reason site fails until it
//     is placed, whatever file it is in.
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
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Part A fix wave (R-IOS-01,
//                                    R-R3-21): one-word reasons, .arg()
//                                    insertions and out-parameter writers
//                                    in any file. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  Part A re-review (R-IOS-01, R-R3-21):
//                                    a reason passed on without words of
//                                    its own is named with its source;
//                                    forwarding sites; real minimums.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  Lane B takes integration (R-IOS-01,
//                                    R-R3-21): integration's accessory
//                                    settings, filter policy, RF-Kit reset,
//                                    off-network and TCI reason sites.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R3 completion carry, review I1
//                                    (R-R3-21, R-R3-38, R-IOS-01): the
//                                    takeover and version reasons scanned in
//                                    SessionEndReasons.cpp.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R3 completion Task 8 (R-R3-21): a
//                                    reason that calls the Core "the
//                                    station" fails; the ham sense stays.
//                                    AI-assisted via Anthropic Claude Code.
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
        if (c == QLatin1Char('{') && i + 1 < code.size() && code.at(i + 1) == QLatin1Char('}')) {
            // An empty brace pair (QString{}, a default {}) ends nothing.
            current += QStringLiteral("{}");
            i += 2;
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

// One reason as written: its text (escapes kept) and what each .arg()
// after it inserts, as the text inside the .arg( ).
struct ReasonText {
    QString text;
    QStringList inserts;
    bool positioned = false;  // Found in a reason position, not only by its space.
    bool forwarded = false;   // Not words: an expression passed on as a reason.
};

// The calls that send a reason, with the index of their reason argument:
// the messages (commandResult, sessionEnd, authResult, settingsReject), the
// senders that wrap them, and the refusal helpers of the scanned files
// (DspAssetService::reject/rejectDetail, the PureSignal facade's fail).
struct ReasonSender {
    const char* name;
    int index;
};

const QList<ReasonSender>& reasonSenders()
{
    static const QList<ReasonSender> senders{
        {"emitResult", 3},   {"commandResult", 3},        {"sessionEnd", 0},
        {"authResult", 1},   {"settingsReject", 3},       {"dropPeer", 1},
        {"sendRejected", 3}, {"sendAllocationResult", 4}, {"rejectAllocation", 3},
        {"reject", 0},       {"rejectDetail", 1},         {"fail", 0},
        // Text the station sends as a property value (propertyTextSources).
        {"connectionFailed", 0}, {"failIdentityAdmission", 1}, {"setLastLoadError", 0},
        {"setNnrLastError", 0}, {"publish", 1}, {"setReceiveLayoutRestoreStatus", 1},
    };
    return senders;
}

// `text` split at its top-level commas (outside brackets and literals).
QStringList splitTopLevel(const QString& text)
{
    QStringList parts;
    QString current;
    int depth = 0;
    qsizetype i = 0;
    while (i < text.size()) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('"') || opensCharLiteral(text, i)) {
            const qsizetype end = literalEnd(text, i);
            current += text.mid(i, end - i);
            i = end;
            continue;
        }
        if (c == QLatin1Char('(') || c == QLatin1Char('{') || c == QLatin1Char('[')) {
            ++depth;
        } else if (c == QLatin1Char(')') || c == QLatin1Char('}') || c == QLatin1Char(']')) {
            --depth;
        } else if (c == QLatin1Char(',') && depth == 0) {
            parts.append(current);
            current.clear();
            ++i;
            continue;
        }
        current += c;
        ++i;
    }
    parts.append(current);
    return parts;
}

// The parts of `statement` that are a reason: each sender's reason
// argument, and the right-hand side of an assignment to a reason
// (…reason, …Reason, …refusal, m_lastError, m_lastActionError).
QStringList reasonExpressionsIn(const QString& statement)
{
    QStringList expressions;
    for (const ReasonSender& sender : reasonSenders()) {
        const QRegularExpression call(
            QStringLiteral("\\b%1\\s*\\(").arg(QLatin1String(sender.name)));
        QRegularExpressionMatchIterator it = call.globalMatch(statement);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const qsizetype open = match.capturedEnd() - 1;
            const qsizetype close = closingOf(statement, open, QLatin1Char('('), QLatin1Char(')'));
            const QStringList arguments =
                splitTopLevel(statement.mid(open + 1, close - open - 2));
            if (sender.index < arguments.size()) {
                expressions.append(arguments.at(sender.index));
            }
        }
    }
    static const QRegularExpression assignment(QStringLiteral(
        "\\b(?:\\w*[Rr]eason|\\w*[Rr]easonText|\\w*[Rr]efusal|m_lastError|m_lastActionError"
        "|m_error|m_settingsSaveError|m_lastListenError)\\s*=(?!=)"));
    const QRegularExpressionMatch assigned = assignment.match(statement);
    // A bool named for a reason (`const bool plainReason = ...`) is not one.
    static const QRegularExpression boolDeclaration(QStringLiteral("\\bbool\\s+$"));
    if (assigned.hasMatch()
        && !boolDeclaration.match(statement.left(assigned.capturedStart())).hasMatch()) {
        expressions.append(statement.mid(assigned.capturedEnd()));
    }
    return expressions;
}

// What the .arg() calls right after the literal ending at `end` insert:
// the text inside each .arg( ), whitespace simplified. A wrapper's closing
// bracket (QStringLiteral( ), tr( )) may come first.
QStringList insertsAfter(const QString& code, qsizetype end)
{
    QStringList inserts;
    qsizetype i = end;
    const auto skipSpace = [&code, &i]() {
        while (i < code.size() && code.at(i).isSpace()) {
            ++i;
        }
    };
    skipSpace();
    if (i < code.size() && code.at(i) == QLatin1Char(')')) {
        ++i;
    }
    for (;;) {
        skipSpace();
        if (!code.mid(i, 5).startsWith(QStringLiteral(".arg("))) {
            break;
        }
        const qsizetype open = i + 4;
        const qsizetype close = closingOf(code, open, QLatin1Char('('), QLatin1Char(')'));
        inserts.append(code.mid(open + 1, close - open - 2).simplified());
        i = close;
    }
    return inserts;
}

// Every reason written in `code`, outside log statements: each string
// literal with a space in it (as before), and each literal of any length
// in a reason position (reasonExpressionsIn), with what .arg() inserts.
QList<ReasonText> reasonsIn(const QString& code)
{
    static const QRegularExpression log(QStringLiteral(
        "\\bq(C(Warning|Info|Debug|Critical)|Warning|Debug|Info|Critical)\\b"));
    static const QRegularExpression literal(QStringLiteral("\"((?:[^\"\\\\\\n]|\\\\.)*)\""));
    QList<ReasonText> found;
    const auto add = [&found](const QString& text, const QStringList& inserts, bool positioned) {
        for (ReasonText& known : found) {
            if (known.text == text) {
                for (const QString& insert : inserts) {
                    if (!known.inserts.contains(insert)) {
                        known.inserts.append(insert);
                    }
                }
                known.positioned = known.positioned || positioned;
                return;
            }
        }
        found.append({text, inserts, positioned});
    };
    for (QString statement : statementsOf(code)) {
        const QRegularExpressionMatch logged = log.match(statement);
        if (logged.hasMatch()) {
            statement.truncate(logged.capturedStart());
        }
        QStringList positioned;
        static const QRegularExpression empty(
            QStringLiteral("^(?:QString\\(\\)|QString\\{\\}|\\{\\}|QStringLiteral\\(\\)|)$"));
        for (const QString& expression : reasonExpressionsIn(statement)) {
            // An expression with no literal in it passes on words written
            // elsewhere; it must be named where it is scanned.
            const QString bare = expression.simplified();
            // A sender's own declaration (`const QString& reason`) is a
            // parameter, not a call passing something on.
            static const QRegularExpression parameter(
                QStringLiteral("^(?:const\\s+)?QString\\s*&?\\s*\\w+(?:\\s*=.*)?$"));
            if (parameter.match(bare).hasMatch()) {
                continue;
            }
            if (!bare.contains(QLatin1Char('"')) && !empty.match(bare).hasMatch()) {
                const bool known = std::any_of(found.cbegin(), found.cend(),
                    [&bare](const ReasonText& r) { return r.forwarded && r.text == bare; });
                if (!known) {
                    found.append({bare, {}, true, true});
                }
                continue;
            }
            QRegularExpressionMatchIterator it = literal.globalMatch(expression);
            while (it.hasNext()) {
                const QString text = it.next().captured(1);
                if (!text.isEmpty()) {
                    positioned.append(text);
                }
            }
        }
        QRegularExpressionMatchIterator it = literal.globalMatch(statement);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const QString text = match.captured(1);
            const bool inPosition = positioned.contains(text);
            if (text.contains(QLatin1Char(' ')) || inPosition) {
                add(text, insertsAfter(statement, match.capturedEnd()), inPosition);
            }
        }
    }
    return found;
}

struct FunctionBody {
    QString name;
    QString body;
    QString params;  // The text inside its parameter list's brackets.
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
        const qsizetype paramsStart = match.capturedEnd();
        found.append({name, code.mid(brace, end - brace),
                      code.mid(paramsStart, afterParams - 1 - paramsStart)});
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

// Why `text` is not plain operator words, or an empty string. A reason
// calls the Core "the Core", never "the station" (R-R3-21): only the ham
// sense of "station" (the station's amplifier, your station) is kept.
QString wordingProblemIn(const QString& text)
{
    if (text.trimmed().isEmpty()) {
        return QStringLiteral("empty");
    }
    const QString term = OperatorWording::internalTermIn(text);
    if (!term.isEmpty()) {
        return term;
    }
    const QString developer = developerWordingIn(text);
    if (!developer.isEmpty()) {
        return developer;
    }
    const QString station = OperatorWording::coreCalledStationIn(text);
    return station.isEmpty() ? QString() : station + QStringLiteral(" (say the Core)");
}

// Why the words .arg() inserts into a reason are not plain, or an empty
// string. A literal is checked as words (one word is fine: a band, a
// number's unit); anything else must be named in `plainInserts`.
QString insertProblemIn(const QString& insert, const QStringList& plainInserts)
{
    static const QRegularExpression literalOnly(QStringLiteral(
        "^(?:QStringLiteral|QLatin1String|QString|tr)?\\s*\\(?\\s*\"((?:[^\"\\\\]|\\\\.)*)\"\\s*\\)?$"));
    const QRegularExpressionMatch literal = literalOnly.match(insert);
    if (literal.hasMatch()) {
        const QString words = literal.captured(1);
        const QString term = OperatorWording::internalTermIn(words);
        const QString problem = term.isEmpty() ? developerWordingIn(words) : term;
        return problem.isEmpty()
                   ? QString()
                   : QStringLiteral("inserts \"%1\" [%2]").arg(words, problem);
    }
    return plainInserts.contains(insert)
               ? QString()
               : QStringLiteral("inserts %1, which is not known to be plain words").arg(insert);
}

// Every wording problem of one reason: its own words, one word standing
// alone (not a sentence an operator can act on), and what .arg() inserts.
QStringList problemsOf(const ReasonText& reason, const QStringList& plainInserts,
                       const QStringList& forwards = {})
{
    QStringList problems;
    if (reason.forwarded) {
        if (!forwards.contains(reason.text)) {
            problems.append(QStringLiteral("passes on %1 as a reason; name it in forwards with "
                                           "where its words come from")
                                .arg(reason.text));
        }
        return problems;
    }
    QString problem = wordingProblemIn(reason.text);
    static const QRegularExpression letter(QStringLiteral("[A-Za-z]"));
    if (problem.isEmpty() && reason.positioned && letter.match(reason.text).hasMatch()
        && !reason.text.trimmed().contains(QLatin1Char(' '))) {
        problem = QStringLiteral("one word");
    }
    if (!problem.isEmpty()) {
        problems.append(QStringLiteral("\"%1\" [%2]").arg(reason.text, problem));
    }
    for (const QString& insert : reason.inserts) {
        const QString inserted = insertProblemIn(insert, plainInserts);
        if (!inserted.isEmpty()) {
            problems.append(QStringLiteral("\"%1\" %2").arg(reason.text, inserted));
        }
    }
    return problems;
}

// Every wording problem among the reasons written in `code`. `plainInserts`
// names the .arg() arguments, by their text, known to insert plain words.
QStringList wordingProblemsInCode(const QString& code, const QStringList& plainInserts)
{
    QStringList problems;
    for (const ReasonText& reason : reasonsIn(code)) {
        problems.append(problemsOf(reason, plainInserts));
    }
    return problems;
}

struct ReasonSource {
    const char* file;
    QStringList functions;   // Empty: the whole file.
    QStringList notReasons;  // Literals here that never reach an app, by their start.
    int atLeast;             // So the scan cannot pass on nothing.
    // .arg() arguments here, by their text inside .arg( ), that insert
    // plain words (numbers, a band, an address, the operator's own label).
    QStringList plainInserts = {};
    // Reason expressions here with no words of their own (a variable, a
    // call), by their text, each passing on words scanned where they are
    // written (named with where, beside the entry).
    QStringList forwards = {};
    // Only literals in a reason position count: the file also writes
    // device commands with spaces in them (the accessory connections).
    bool positionedOnly = false;
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
         30,
         // The other app's network address (WebSocketTransport::peerDescription).
         {QStringLiteral("description")},
         {// listen(): the Core's own setup error (m_lastError), for its
          // console and log; never sent to an app.
          QStringLiteral("CertificateStore::tlsBackendDiagnostic()"),
          QStringLiteral("m_certificates->lastError()"),
          QStringLiteral("m_wsServer->errorString()"),
          // dropPeer's and sendRejected's parameter, from this file's calls.
          QStringLiteral("reason"),
          // SessionEndReasons' version and takeover reasons, scanned below.
          QStringLiteral("SessionEndReasons::versionRefused(m_supportedMajors, "
                         "message.supportedMajors)"),
          QStringLiteral("SessionEndReasons::takenOver(description)"),
          // Each model's readOnlyReason, scanned below.
          QStringLiteral("IoBoardHl2Facade::readOnlyReason()"),
          QStringLiteral("AmplifierModel::readOnlyReason()"),
          QStringLiteral("RfKitModel::readOnlyReason()"),
          QStringLiteral("StationTciModel::readOnlyReason()"),
          QStringLiteral("AccessoryDataModel::readOnlyReason()"),
          QStringLiteral("AccessorySettingsModel::readOnlyReason()"),
          // Literals inserted into `refusals` in this file.
          QStringLiteral("refusals.value(update.name)"),
          // The attenuator and antenna facades' settle reasons, scanned below.
          QStringLiteral("m_radioModel->stepAttFacade()->settleReason(update.name)"),
          QStringLiteral("m_radioModel->alexAntennaFacade()->settleReason(update.name)"),
          // A constant of this file, its literal scanned here.
          QStringLiteral("QString::fromLatin1(kReceiveOnlyTransmitReason)"),
          // StateMirror's and SettingsProxyServer's results, scanned below.
          QStringLiteral("result.reason"),
          QStringLiteral("m_settingsServer->otherRadioRefusal(key)"),
          QStringLiteral("refusal"),
          // A code windows compare, not a reason (section 17).
          QStringLiteral("m_displayBudgetReason")}},
        // command.result for every verb.
        // The device's name ("Power Genius", "Tuner Genius") and what the
        // request asked, both this file's own literals
        // (handleAccessoryDeviceSettings).
        {"src/core/session/SessionCommandDispatcher.cpp", {}, {}, 30,
         {QStringLiteral("device"), QStringLiteral("what")},
         {// Out-parameters and results of the scanned model, facade and
          // allocator functions each verb calls (RadioModel, the PureSignal
          // facade, SliceModel, AlexAntennaFacade, DspAssetService,
          // SliceStreamAllocator), and emitResult's own parameter.
          QStringLiteral("reason"), QStringLiteral("result.reason"),
          QStringLiteral("refusal"), QStringLiteral("facade->lastActionError()"),
          QStringLiteral("slice->nnrLastError()"), QStringLiteral("accepted ? QString() : reason"),
          QStringLiteral("rejectionReason"), QStringLiteral("outcome.reason"),
          QStringLiteral("receiveOnly ? alex->setRxOnlyAntForBand(Band(band), antenna) : "
                         "alex->setRxAntForBand(Band(band), antenna)"),
          // A function of this file, its literal scanned here.
          QStringLiteral("notRepresentableReason()"),
          // AlexAntennaFacade's filter policy refusal, scanned below.
          QStringLiteral("alex->setBpfModeForChain(chain, mode)")}},
        // iPhone app Task 13 (R-IOS-08): the device administration verbs'
        // command.result, forwarded by the dispatcher as result.reason; the
        // log lines never reach an app.
        {"src/core/session/StationDevicesFacade.cpp", {},
         {"Could not save", "A paired device was removed", "The pairing token was retired"},
         8},
        // The label rule a refused station.rename gives.
        {"src/core/security/StationLabel.cpp", {QStringLiteral("ruleText")}, {}, 1},
        // property.result for a write the mirror refuses.
        {"src/core/session/StateMirror.cpp", {}, {}, 5, {},
         {// A function of this file, and a model's applyMirroredValue
          // (the hook), both scanned.
          QStringLiteral("writableButOutboundReason( MirrorSchema::shortClassName(className), "
                         "prop.name)"),
          QStringLiteral("hookReason")}},
        // PureSignal's command.result and its lastActionError.
        {"src/core/session/PureSignalSessionFacade.cpp", {}, {}, 15, {},
         {// finishOperation's parameter and the store's import error, both
          // from this file; lastActionError as a remote window receives it.
          QStringLiteral("reason"), QStringLiteral("imported.error"),
          QStringLiteral("value.toString()")}},
        // Model and correction files; the store's and validator's own
        // messages are detail for the log unless isOperatorMessage says
        // otherwise (DspAssetService::rejectDetail).
        // The operator's own label for a model, and "the bundled small (large)
        // model" (bundledNr3Name).
        {"src/core/dsp/DspAssetService.cpp", {}, {}, 30,
         {QStringLiteral("label"), QStringLiteral("bundledNr3Name(id)")},
         {// rejectDetail's operator message (isOperatorMessage, scanned) or
          // its plain fallback, and the statuses this file words.
          QStringLiteral("detail"), QStringLiteral("plain"), QStringLiteral("error"),
          QStringLiteral("problems.join(QLatin1Char(' '))"), QStringLiteral("problem"),
          QStringLiteral("none")}},
        {"src/core/dsp/DspAssetValidation.cpp", {QStringLiteral("isOperatorMessage")}, {}, 6},
        // The explanation it restores is one this file words.
        {"src/core/dsp/NnrAdapter.cpp", {}, {}, 6, {}, {QStringLiteral("before.explanation")}},
        // The model paths' refusal reaches nnr.applyModelSelection.
        {"src/core/WdspEngine.cpp", {QStringLiteral("setNnrModelPaths")}, {}, 2},
        // A refused PureSignal settings write (property.result).
        {"src/models/PureSignalSettings.cpp",
         {QStringLiteral("isValid"), QStringLiteral("apply")}, {}, 5, {},
         // The reject lambda's parameter: the literals isValid passes it.
         {QStringLiteral("message")}},
        // The link version refusal and the takeover, sent in session.end.
        // Version numbers, "Update the Core." or "Update this app.", and
        // the other app's network address (WebSocketTransport::peerDescription).
        {"src/core/session/SessionEndReasons.cpp",
         {QStringLiteral("takenOver"), QStringLiteral("versionRefused")}, {}, 4,
         {QStringLiteral("coreNewest"), QStringLiteral("appNewest"), QStringLiteral("update"),
          QStringLiteral("otherApp")}},
        // The window's reason it keeps: its own parameter, from this file.
        {"src/core/accessories/AlexAntennaFacade.cpp", {}, {}, 6, {}, {QStringLiteral("kept")}},
        // The attenuator's range in dB.
        {"src/core/StepAttenuatorFacade.cpp", {}, {}, 6,
         {QStringLiteral("lo"), QStringLiteral("hi")}, {QStringLiteral("kept")}},
        {"src/core/IoBoardHl2Facade.cpp", {}, {}, 1},
        // A receiver count, and a frequency in MHz.
        {"src/core/SliceStreamAllocator.cpp", {}, {}, 4,
         {QStringLiteral("count"),
          QStringLiteral("QString::number(frequencyHz / 1.0e6, 'f', 4)")}},
        // Power in watts.
        {"src/core/StationAccessoryData.cpp", {}, {}, 4,
         {QStringLiteral("forwardW"), QStringLiteral("limitW")}},
        // The port number and the Core's addresses (blockedReason).
        {"src/core/StationTciController.cpp", {}, {}, 1,
         {QStringLiteral("port"), QStringLiteral("stationAddresses.join(QStringLiteral(\", \"))")},
         {// TciServer's own error text, kept for the Core's log line only.
          QStringLiteral("error")}},
        // The SWR limit's range, one decimal.
        {"src/core/settings/SettingsProxyServer.cpp", {}, {}, 3,
         {QStringLiteral("kSwrProtectionLimitMin, 0, 'f', 1"),
          QStringLiteral("kSwrProtectionLimitMax, 0, 'f', 1")},
         {// Functions of this file and SettingsScope's refusal, scanned.
          QStringLiteral("otherRadioRefusal(key)"), QStringLiteral("refusal"),
          QStringLiteral("modelOwnedSettingsRefusal(key)")}},
        {"src/core/settings/SettingsScope.cpp", {QStringLiteral("modelOwnedSettingsRefusal")},
         {}, 5},
        // The display refusals and retirements (rejected, allocation-result).
        {"src/core/session/media/DaemonMediaController.cpp", {},
         {// statsSummary(): a log line's text.
          "largestKeyframe="},
         18, {},
         {// Parameters and fields that carry this file's own reasons.
          QStringLiteral("reason"), QStringLiteral("prior.reason"),
          QStringLiteral("admitted.refusal"), QStringLiteral("stream.profileRefusal"),
          QStringLiteral("m_headphones.profileRefusal"), QStringLiteral("m_audioProfileRefusal"),
          // Codes, not reasons (section 17): receiver audio off and
          // profile refusal codes, spectrum limit reasons, retire reasons.
          QStringLiteral("headphonesBlockedBy().value_or(RemoteAudioOffReason::RadioOffline)"),
          QStringLiteral("grantReason(grant)"), QStringLiteral("grantReason(reasonGrant)"),
          QStringLiteral("context.offReason"),
          QStringLiteral("RemoteAudioProfileRefusal::NotAllowed"),
          QStringLiteral("RemoteAudioProfileRefusal::Unavailable"),
          QStringLiteral("RemoteAudioOffReason::EncoderUnavailable"),
          QStringLiteral("QString::fromLatin1(kRetireReasonSourceRetune)"),
          QStringLiteral("QString::fromLatin1(kRetireReasonStreamBindingChanged)"),
          QStringLiteral("QString::fromLatin1(kRetireReasonSliceRemoved)")}},
        {"src/core/session/media/SpectrumEndpoint.h", {}, {}, 3},
        {"src/models/AccessoryDataModel.cpp", {QStringLiteral("readOnlyReason")}, {}, 1},
        {"src/models/AccessorySettingsModel.cpp", {QStringLiteral("readOnlyReason")}, {}, 1},
        // A Power Genius or Tuner Genius on another network: its
        // connectionError, through the controllers' rejectIdentity. The
        // device's name and its address.
        {"src/core/StationNetwork.cpp", {QStringLiteral("offNetworkReason")}, {}, 1,
         {QStringLiteral("deviceName, address")}},
        // Reset amp error's refusal (resetRfKitError).
        {"src/core/StationRfKitController.cpp", {QStringLiteral("resetError")}, {}, 1},
        // The amp's and tuner's own settings: the refusals of their
        // commands and the answers the `accessorySettings` object carries.
        // The device's name ("Power Genius", "Tuner Genius").
        {"src/core/StationDeviceSettings.cpp", {}, {}, 10,
         {QStringLiteral("deviceName()"), QStringLiteral("device")},
         {// validateNetwork's result, this file's own literals.
          QStringLiteral("problem")}},
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
          QStringLiteral("disconnectRfKitForStation"), QStringLiteral("resetRfKitErrorForStation"),
          QStringLiteral("refuseNoStationDevice"), QStringLiteral("setPgxlNameForStation"),
          QStringLiteral("setPgxlHardwareForStation"), QStringLiteral("setPgxlNetworkForStation"),
          QStringLiteral("savePgxlSettingsForStation"),
          QStringLiteral("readPgxlSettingsForStation"), QStringLiteral("setTgxlNameForStation"),
          QStringLiteral("setTgxlNetworkForStation"),
          QStringLiteral("saveTgxlSettingsForStation"),
          QStringLiteral("readTgxlSettingsForStation"), QStringLiteral("setNnrDiagnosticMode"),
          QStringLiteral("applyNnrModelSelection"), QStringLiteral("addNotchFromStation"),
          QStringLiteral("moveNotchFromStation"), QStringLiteral("setNotchActiveFromStation"),
          QStringLiteral("deleteNotchFromStation"), QStringLiteral("requestIoBoardProbe"),
          QStringLiteral("nr3CannotRunReason")},
         {// This app's own branch in a remote window (role Remote), shown
          // through OperatorReasonText; never sent by the Core.
          "There is no station session."},
         20, {},
         {// The refuse lambdas' parameter (literals of these functions),
          // the facades' and allocators' results (scanned), and the notch
          // refusals, constants of this file checked in
          // notchConstantsArePlain below.
          QStringLiteral("text"), QStringLiteral("result.reason"),
          QStringLiteral("outcome.reason"), QStringLiteral("kUnknownNotchReason"),
          QStringLiteral("kNotchListBusyReason")}},
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
        {"src/models/RadioModel.cpp", "reportStationAccessoryRefusal",
         "a remote window passes the Core's refusal on to its own pages"},
        {"src/core/session/IStationLink.h", "pgxlDeviceSettingsUnavailableReason",
         "a remote window's own reason when its Core cannot take the request"},
        {"src/core/session/IStationLink.h", "tgxlDeviceSettingsUnavailableReason",
         "a remote window's own reason when its Core cannot take the request"},
        {"src/core/session/IStationLink.h", "filterPolicyUnavailableReason",
         "a remote window's own reason when its Core cannot take the request"},
        {"src/models/RadioModel.h", "rxFilter0Reason",
         "the filter badge's status label (AlexController), not a refusal"},
        {"src/models/RadioModel.h", "rxFilter1Reason",
         "the filter badge's status label (AlexController), not a refusal"},
    };
    return sites;
}

// Text the station sends as a property value, which an app shows as sent
// (the link document's section 17): each accessory's connectionError (the
// Tuner Genius, the Power Genius and the RF-Kit amplifier), a receiver's
// nnrStatus and nnrLastError, the receive filters' rxFilter0Reason and
// rxFilter1Reason, and PureSignal's lastLoadError. NnrAdapter.cpp (the
// NNR explanation behind nnrStatus) is a reason source above.
const QList<ReasonSource>& propertyTextSources()
{
    static const QList<ReasonSource> sources{
        // failIdentityAdmission's parameter: this file's identity texts,
        // and the controller's (scanned below) through rejectIdentity.
        {"src/core/TgxlConnection.cpp", {}, {}, 4, {}, {QStringLiteral("reason")}, true},
        {"src/core/PgxlConnection.cpp", {}, {}, 4, {}, {QStringLiteral("reason")}, true},
        // The name an amplifier that is not an RF2K-S reports for itself.
        // `reason` is the identity refusal worded just above it.
        {"src/core/Rf2ksConnection.cpp", {}, {}, 2, {QStringLiteral("device")},
         {QStringLiteral("reason")}, true},
        // The name the device at the address reports for itself.
        {"src/core/StationTgxlController.cpp", {}, {}, 2, {QStringLiteral("product")}},
        // The name the device at the address reports for itself.
        {"src/core/StationPgxlController.cpp", {}, {}, 2, {QStringLiteral("product")}},
        // publish's error: Rf2ksConnection's connectionFailed reason, scanned
        // above.
        {"src/core/StationRfKitController.cpp", {}, {}, 1, {}, {QStringLiteral("reason")}},
        // Band names ("20m"), one or several joined with " + ".
        {"src/core/accessories/AlexController.cpp", {QStringLiteral("recomputeBpf")}, {}, 3,
         {QStringLiteral("bandLabel(s.currentBpfBand)"),
          QStringLiteral("bandList.join(QStringLiteral(\" + \"))")},
         // One band's name alone: the reason when one band is filtered.
         {QStringLiteral("bandLabel(s.currentBpfBand)")}},
        {"src/core/dsp/NnrSettings.h", {QStringLiteral("nnrLimitExplanation")}, {}, 4},
        {"src/models/SliceModel.cpp",
         {QStringLiteral("setActiveNr"), QStringLiteral("applyNnrSettings"),
          QStringLiteral("requestNnrDiagnostics"), QStringLiteral("restoreNnrSettings")},
         {}, 3},
        {"src/models/PureSignalSettings.cpp", {QStringLiteral("load")}, {}, 1},
        // The station TCI server's error (StationTciModel error), worded by
        // blockedReason. The port number and the Core's addresses.
        {"src/core/StationTciController.cpp", {}, {}, 1,
         {QStringLiteral("port"), QStringLiteral("stationAddresses.join(QStringLiteral(\", \"))")},
         {// TciServer's own error text, kept for the Core's log line only.
          QStringLiteral("error")},
         true},

        // The 4O3A listener's error (fourO3AListenerError).
        {"src/core/SmartSdrApiListener.cpp", {}, {}, 1, {}, {}, true},
        // The settings save error (settingsSaveError) and the receive
        // layout notice (receiveLayoutRestoreMessage).
        {"src/models/RadioModel.cpp",
         {QStringLiteral("flushPendingSettingsSave"), QStringLiteral("bindReceiveLayoutSlices"),
          QStringLiteral("connectToRadioImpl"), QStringLiteral("prepareReceiveLayout"),
          QStringLiteral("completeReceiveLayoutStartup"),
          QStringLiteral("plainReceiveLayoutProblem"), QStringLiteral("withKeptLayout"),
          QStringLiteral("captureReceiveLayout"),
          QStringLiteral("activateRestoredRadeReceiveOwner"),
          QStringLiteral("radeAudioAwaitsReceiver")},
         {}, 6,
         // activateRestoredRadeReceiveOwner's refusal, scanned here.
         {QStringLiteral("error")},
         // Sentences these functions word, joined by withKeptLayout; and
         // captureReceiveLayout's refusal, both scanned here.
         {QStringLiteral("withKeptLayout(refusals)"), QStringLiteral("withKeptLayout(notes)"),
          QStringLiteral("error")},
         true},
        // The Power Genius's efficiency: the device's own reading (a
        // number and a unit), passed on as it reports it.
        {"src/core/PgxlStatusGauges.cpp", {}, {}, 0, {}, {}, true},
    };
    return sources;
}

// Functions that write a reason only by passing their QString* out-parameter
// on to a function scanned above: the call must be in the body, with the
// out-parameter among its arguments.
struct ForwardingSite {
    const char* file;
    const char* function;
    const char* callee;
};

const QList<ForwardingSite>& forwardingSites()
{
    static const QList<ForwardingSite> sites{
        {"src/core/RxChannel.cpp", "setNnrTuning", "NnrAdapter::apply"},
        {"src/core/RxChannel.cpp", "setNnrDiagnostics", "NnrAdapter::setDiagnostics"},
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
    for (const ForwardingSite& site : forwardingSites()) {
        if (file == QLatin1String(site.file) && function == QLatin1String(site.function)) {
            return true;
        }
    }
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

// The reason sites in `file` (its code `code`) this test neither scans
// nor names on the app side: a function whose name says it words a reason
// (…Reason, …Refusal, …ForStation, …FromStation, applyMirroredValue), a
// function that writes a reason through a QString* out-parameter
// (…reason, …Reason, …refusal), and a file that sends a reason to an app.
// `functions` counts the functions found by name.
QStringList unplacedReasonSites(const QString& file, const QString& code, int* functions = nullptr)
{
    static const QRegularExpression anyName(QStringLiteral("."));
    static const QRegularExpression reasonFunction(
        QStringLiteral("(Reason|Refusal|ForStation|FromStation)$|^applyMirroredValue$"));
    static const QRegularExpression outParameter(
        QStringLiteral("\\bQString\\s*\\*\\s*\\w*(?:[Rr]eason|[Rr]efusal)\\w*\\b"));
    static const QRegularExpression sender(QStringLiteral(
        "\\b(commandResult|sessionEnd|authResult|settingsReject|propertyResult|emitResult"
        "|dropPeer|sendRejected|sendAllocationResult|rejectAllocation)\\s*\\("));
    QStringList unplaced;
    for (const FunctionBody& function : functionsIn(code, anyName)) {
        const bool named = reasonFunction.match(function.name).hasMatch();
        const bool writesOut = outParameter.match(function.params).hasMatch();
        if (!named && !writesOut) {
            continue;
        }
        if (named && functions != nullptr) {
            ++*functions;
        }
        if (!scanned(file, function.name) && !appSide(file, function.name)) {
            unplaced.append(file + QStringLiteral(": ") + function.name
                            + (named ? QString() : QStringLiteral(" (writes a reason it is given)")));
        }
    }
    if (sender.match(code).hasMatch() && !scanned(file, QString()) && !appSide(file, QString())) {
        unplaced.append(file + QStringLiteral(": sends a reason"));
    }
    return unplaced;
}

// Checks every source of `sources` as reasons: the problems found, and
// how many reasons were checked. `failures` also gets a line for a source
// with fewer reasons than it promises, or a named function not found.
int checkReasonSources(const QList<ReasonSource>& sources, QStringList* failures)
{
    static const QRegularExpression anyName(QStringLiteral("."));
    int checked = 0;
    for (const ReasonSource& source : sources) {
        const QString code = codeOf(sourcePath(QString::fromLatin1(source.file)));
        if (code.isEmpty()) {
            failures->append(QStringLiteral("%1: not read").arg(QLatin1String(source.file)));
            continue;
        }
        QList<ReasonText> found;
        if (source.functions.isEmpty()) {
            found = reasonsIn(code);
        } else {
            QStringList seen;
            for (const FunctionBody& function : functionsIn(code, anyName)) {
                if (!source.functions.contains(function.name)) {
                    continue;
                }
                seen.append(function.name);
                for (const ReasonText& reason : reasonsIn(function.body)) {
                    const bool known = std::any_of(
                        found.cbegin(), found.cend(),
                        [&reason](const ReasonText& r) { return r.text == reason.text; });
                    if (!known) {
                        found.append(reason);
                    }
                }
            }
            for (const QString& function : source.functions) {
                if (!seen.contains(function)) {
                    failures->append(QStringLiteral("%1: %2 not found")
                                         .arg(QLatin1String(source.file), function));
                }
            }
        }
        int reasons = 0;
        for (const ReasonText& reason : found) {
            if (source.positionedOnly && !reason.positioned) {
                continue;
            }
            const bool exempt = std::any_of(
                source.notReasons.cbegin(), source.notReasons.cend(),
                [&reason](const QString& start) { return reason.text.startsWith(start); });
            if (exempt) {
                continue;
            }
            ++reasons;
            for (const QString& problem :
                 problemsOf(reason, source.plainInserts, source.forwards)) {
                failures->append(QStringLiteral("%1: %2").arg(QLatin1String(source.file), problem));
            }
        }
        if (reasons < source.atLeast) {
            failures->append(QStringLiteral("%1: %2 reasons, fewer than %3")
                                 .arg(QLatin1String(source.file))
                                 .arg(reasons)
                                 .arg(source.atLeast));
        }
        checked += reasons;
    }
    return checked;
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
              "key is not Station-scoped",
              // R-R3-21: the Core is the Core, not the station.
              "The station sets this itself; it cannot be changed from here.",
              "This Core has no TCI server for the station.",
              "Transmit configuration is unavailable on this receive-only station."}) {
            QVERIFY2(!wordingProblemIn(QString::fromUtf8(old)).isEmpty(), old);
        }
        for (const char* plain :
             {"PureSignal cannot be run from a remote window yet.",
              "The Core could not read this request.",
              "Update this app to set up the Power Genius on this Core.",
              "Choose an SWR protection limit from %1 to %2.",
              // The ham sense of "station" stays.
              "Operating the station's amplifier or tuner waits for remote transmit. "
              "This Core is receive-only."}) {
            QVERIFY2(wordingProblemIn(QString::fromUtf8(plain)).isEmpty(), plain);
        }
    }

    void theScanReadsOneWordAndInsertedReasons()
    {
        // A one-word reason is not a sentence an operator can act on.
        const QStringList oneWord = wordingProblemsInCode(
            QStringLiteral("emitResult(verb, id, false, QStringLiteral(\"unimplemented\"), {});"),
            {});
        QVERIFY2(oneWord.join(QLatin1Char('|')).contains(QStringLiteral("unimplemented")),
                 qPrintable(oneWord.join(QLatin1Char('|'))));
        const QStringList assigned = wordingProblemsInCode(
            QStringLiteral("*reason = QStringLiteral(\"EINVAL\");"), {});
        QVERIFY2(assigned.join(QLatin1Char('|')).contains(QStringLiteral("EINVAL")),
                 qPrintable(assigned.join(QLatin1Char('|'))));
        // What .arg() inserts into a reason is part of the reason: a
        // developer word inserted as a literal, and an expression nobody
        // has said inserts plain words.
        const QStringList insertedLiteral = wordingProblemsInCode(
            QStringLiteral("dropPeer(t, QStringLiteral(\"The Core stopped %1.\")"
                           ".arg(QStringLiteral(\"sessionEpoch\")), true);"),
            {});
        QVERIFY2(insertedLiteral.join(QLatin1Char('|')).contains(QStringLiteral("sessionEpoch")),
                 qPrintable(insertedLiteral.join(QLatin1Char('|'))));
        const QStringList insertedName = wordingProblemsInCode(
            QStringLiteral("emitResult(verb, id, false, QStringLiteral(\"The Core refused %1.\")"
                           ".arg(QString::fromUtf8(verb)), {});"),
            {});
        QVERIFY2(insertedName.join(QLatin1Char('|')).contains(QStringLiteral("fromUtf8(verb)")),
                 qPrintable(insertedName.join(QLatin1Char('|'))));
        // Named as plain, the same insertion passes.
        QVERIFY(wordingProblemsInCode(
                    QStringLiteral("emitResult(verb, id, false, QStringLiteral(\"The Core refused %1.\")"
                                   ".arg(count), {});"),
                    {QStringLiteral("count")})
                    .isEmpty());
    }

    void everyStationReasonIsPlain()
    {
        QStringList failures;
        const int checked = checkReasonSources(reasonSources(), &failures);
        // QtTest cuts a long message short; each failure gets its own line.
        for (const QString& failure : std::as_const(failures)) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
        QVERIFY2(checked >= 250, qPrintable(QString::number(checked)));
    }

    void forwardingSitesPassTheirReasonOn()
    {
        static const QRegularExpression anyName(QStringLiteral("."));
        int checked = 0;
        for (const ForwardingSite& site : forwardingSites()) {
            const QString code = codeOf(sourcePath(QString::fromLatin1(site.file)));
            bool passed = false;
            for (const FunctionBody& function : functionsIn(code, anyName)) {
                if (function.name != QLatin1String(site.function)) {
                    continue;
                }
                const QString callee = QLatin1String(site.callee) + QLatin1Char('(');
                const qsizetype call = function.body.indexOf(callee);
                if (call < 0) {
                    continue;
                }
                const qsizetype open = call + callee.size() - 1;
                const qsizetype close =
                    closingOf(function.body, open, QLatin1Char('('), QLatin1Char(')'));
                for (const QString& argument :
                     splitTopLevel(function.body.mid(open + 1, close - open - 2))) {
                    passed = passed || argument.trimmed() == QStringLiteral("reason");
                }
            }
            QVERIFY2(passed, qPrintable(QStringLiteral("%1: %2 does not pass its reason to %3")
                                            .arg(QLatin1String(site.file),
                                                 QLatin1String(site.function),
                                                 QLatin1String(site.callee))));
            const QString calleeClass =
                QString::fromLatin1(site.callee).section(QStringLiteral("::"), 0, 0);
            QVERIFY2(scanned(QStringLiteral("src/core/dsp/%1.cpp").arg(calleeClass), QString()),
                     site.callee);
            ++checked;
        }
        QCOMPARE(checked, 2);
    }

    void notchConstantsArePlain()
    {
        // RadioModel.cpp's two notch refusals are file-scope constants the
        // function scan names as forwards; their words are checked here.
        const QString code = codeOf(sourcePath(QStringLiteral("src/models/RadioModel.cpp")));
        int checked = 0;
        for (const char* name : {"kUnknownNotchReason", "kNotchListBusyReason"}) {
            const QRegularExpression definition(
                QStringLiteral("\\bconst QString %1\\s*=\\s*QStringLiteral\\(\\s*\"([^\"]*)\"")
                    .arg(QLatin1String(name)));
            const QRegularExpressionMatch match = definition.match(code);
            QVERIFY2(match.hasMatch(), name);
            const QString problem = wordingProblemIn(match.captured(1));
            QVERIFY2(problem.isEmpty(), qPrintable(match.captured(1) + QStringLiteral(" [")
                                                   + problem + QLatin1Char(']')));
            ++checked;
        }
        QCOMPARE(checked, 2);
    }

    void everyStationPropertyTextIsPlain()
    {
        // Text sent as a property value is shown as sent too.
        QStringList failures;
        const int checked = checkReasonSources(propertyTextSources(), &failures);
        // QtTest cuts a long message short; each failure gets its own line.
        for (const QString& failure : std::as_const(failures)) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
        QVERIFY2(checked >= 25, qPrintable(QString::number(checked)));
    }

    void anUnlistedReasonSiteFails()
    {
        // A function in a file this test does not list, writing a reason
        // through a QString* out-parameter, fails whatever it is called.
        const QString planted = QStringLiteral(
            "bool PlantedModel::applyThing(int value, QString* reason)\n"
            "{\n    if (value < 0) { *reason = QStringLiteral(\"The Core refused it.\"); }\n"
            "    return value >= 0;\n}\n");
        const QStringList unplaced =
            unplacedReasonSites(QStringLiteral("src/core/PlantedModel.cpp"), planted);
        QVERIFY2(unplaced.join(QLatin1Char('|')).contains(QStringLiteral("applyThing")),
                 qPrintable(unplaced.join(QLatin1Char('|'))));
        // Named on the app side, it is placed.
        QVERIFY(unplacedReasonSites(QStringLiteral("src/core/session/StationClient.cpp"), planted)
                    .isEmpty());
    }

    void everyReasonSiteIsScannedOrOnTheAppSide()
    {
        QStringList unplaced;
        int functions = 0;
        for (const QString& file :
             sourceFiles({QStringLiteral("src/core"), QStringLiteral("src/models")})) {
            const QString code = codeOf(sourcePath(file));
            unplaced.append(unplacedReasonSites(file, code, &functions));
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
