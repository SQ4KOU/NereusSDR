// no-port-check: NereusSDR-original.
// =================================================================
// tests/LinkFixtures.cpp  (NereusSDR)
// =================================================================
//
// See LinkFixtures.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 3 (R-IOS-01): fixture
//                                    loader, matcher and script player.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 4 (R-IOS-01): linkMajors().
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Part A fix wave (R-IOS-01):
//                                    linkMajors read against the
//                                    station's supported majors.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Part A fix wave (R-IOS-01):
//                                    fixtures say which ends run them and
//                                    each client step's role; placeholders
//                                    in client messages.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 12 (R-IOS-08):
//                                    "$device:<case>" and the runner's own
//                                    device key, made at run time.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 13 (R-IOS-08):
//                                    stationSetup "otherPairedDevices" and
//                                    the device ids it records, with the
//                                    runner's own, for "$ref:device:<n>"
//                                    and "$ref:device:self".
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#include "LinkFixtures.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "core/dsp/Ps3Snapshot.h"
#include "core/security/DeviceAuthenticator.h"
#include "core/security/DeviceStore.h"
#include "core/security/StationIdentity.h"
#include "core/session/LinkVersion.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationLanAnnouncement.h"
#include "core/session/StationServer.h"
#include "fakes/LoopbackTransport.h"

namespace NereusSDR::Test {

namespace {

constexpr int kMaxShownChars = 400;
// How long the player waits, in real time, for a station message the event
// queue has not produced yet (work on another thread of the fake radio). A
// passing fixture never waits this long; it bounds a failing one.
constexpr int kStationReplyWaitMs = 5000;
// A timer whose real remaining time grew by more than this since it was
// last seen was started again.
constexpr qint64 kRestartSlackMs = 20;

QString shown(const QJsonValue& value)
{
    QByteArray text;
    if (value.isObject()) {
        text = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    } else if (value.isArray()) {
        text = QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    } else if (value.isString()) {
        text = '"' + value.toString().toUtf8() + '"';
    } else if (value.isDouble()) {
        text = QByteArray::number(value.toDouble(), 'g', 17);
    } else if (value.isBool()) {
        text = value.toBool() ? "true" : "false";
    } else if (value.isNull()) {
        text = "null";
    } else {
        text = "(absent)";
    }
    QString out = QString::fromUtf8(text);
    if (out.size() > kMaxShownChars) {
        out = out.left(kMaxShownChars) + QStringLiteral("...");
    }
    return out;
}

QString placeholderArgument(const QString& text, const QString& prefix)
{
    return text.startsWith(prefix) ? text.mid(prefix.size()) : QString();
}

// The placeholders of the link document's section 16.3, by their text.
//   $any                 any value (the key must be present)
//   $string[:<name>]     any string; with a name, also recorded
//   $int[:<name>]        any whole number; with a name, also recorded
//   $object              any JSON object
//   $capture:<name>      any value, recorded
//   $ref:<name>          equal to the value recorded under <name>
//   $within:<t>:<v>      a number no further than <t> from <v>
//   $majors              only as a hello's "majors": whole numbers from 0 to
//                        65535, ascending, no repeats, naming its "major"
bool isPlaceholder(const QJsonValue& value)
{
    if (!value.isString()) {
        return false;
    }
    const QString text = value.toString();
    return text == QStringLiteral("$any") || text == QStringLiteral("$string")
        || text == QStringLiteral("$int") || text == QStringLiteral("$object")
        || text == QStringLiteral("$majors")
        || (text.startsWith(QStringLiteral("$string:")) && text.size() > 8)
        || (text.startsWith(QStringLiteral("$int:")) && text.size() > 5)
        || (text.startsWith(QStringLiteral("$capture:")) && text.size() > 9)
        || (text.startsWith(QStringLiteral("$ref:")) && text.size() > 5)
        || text.startsWith(QStringLiteral("$within:"));
}

// Why `actual` is not a list of majors naming `major`, or an empty
// string ("$majors", section 16.1).
QString majorsProblem(const QJsonValue& actual, const QJsonValue& major, const QString& path)
{
    if (!actual.isArray() || actual.toArray().isEmpty()) {
        return QStringLiteral("%1: expected a list of majors, got %2").arg(path, shown(actual));
    }
    double previous = -1.0;
    bool named = false;
    for (const QJsonValue& value : actual.toArray()) {
        const double m = value.toDouble(-1.0);
        if (!value.isDouble() || m < 0.0 || m > 65535.0 || m <= previous
            || m != static_cast<double>(static_cast<qint64>(m))) {
            return QStringLiteral("%1: majors must be whole numbers from 0 to 65535, ascending, "
                                  "without repeats; got %2")
                .arg(path, shown(actual));
        }
        named = named || (major.isDouble() && m == major.toDouble());
        previous = m;
    }
    return named ? QString()
                 : QStringLiteral("%1: majors %2 do not name the hello's major %3")
                       .arg(path, shown(actual), shown(major));
}

struct NamedInt {
    QString name;
    bool ranged = false;
    double min = 0.0;
    double max = 0.0;
};

// "$int:<name>" or "$int:<name>:<min>:<max>" (min and max whole numbers in
// JSON syntax, min not above max); false when the text is neither.
bool parseNamedInt(const QString& text, NamedInt* out)
{
    const QStringList parts = text.mid(5).split(QLatin1Char(':'));
    if (parts.isEmpty() || parts.first().isEmpty() || (parts.size() != 1 && parts.size() != 3)) {
        return false;
    }
    out->name = parts.first();
    out->ranged = parts.size() == 3;
    if (!out->ranged) {
        return true;
    }
    static const QRegularExpression whole(QStringLiteral("^-?(?:0|[1-9][0-9]*)$"));
    if (!whole.match(parts.at(1)).hasMatch() || !whole.match(parts.at(2)).hasMatch()) {
        return false;
    }
    out->min = parts.at(1).toDouble();
    out->max = parts.at(2).toDouble();
    return out->min <= out->max;
}

bool isWholeNumber(const QJsonValue& value)
{
    const double d = value.toDouble();
    return value.isDouble() && std::isfinite(d)
        && d == static_cast<double>(static_cast<qint64>(d));
}

// "$within:<tolerance>:<value>", both in JSON number syntax; false when
// the text is not that.
bool parseWithin(const QString& text, double* tolerance, double* centre)
{
    const QStringList parts = text.mid(8).split(QLatin1Char(':'));
    if (parts.size() != 2) {
        return false;
    }
    // Each part in JSON number syntax exactly (RFC 8259 section 6), not
    // whatever a number parser would take ("+1", "inf", ".5", "0x10").
    static const QRegularExpression jsonNumber(QStringLiteral(
        "^-?(?:0|[1-9][0-9]*)(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?$"));
    if (!jsonNumber.match(parts.at(0)).hasMatch() || !jsonNumber.match(parts.at(1)).hasMatch()) {
        return false;
    }
    bool okTolerance = false;
    bool okCentre = false;
    *tolerance = parts.at(0).toDouble(&okTolerance);
    *centre = parts.at(1).toDouble(&okCentre);
    return okTolerance && okCentre && std::isfinite(*tolerance) && *tolerance >= 0.0
        && std::isfinite(*centre);
}

QString expectKeys(const QJsonObject& object, const QStringList& required,
                   const QStringList& optional, const QString& where)
{
    for (const QString& key : required) {
        if (!object.contains(key)) {
            return QStringLiteral("%1: missing \"%2\"").arg(where, key);
        }
    }
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!required.contains(it.key()) && !optional.contains(it.key())) {
            return QStringLiteral("%1: unknown field \"%2\"").arg(where, it.key());
        }
    }
    return QString();
}

// Runs the queued work of every object on this thread until the queue is
// idle, including deleteLater(). No real time passes on purpose.
void drain()
{
    QAbstractEventDispatcher* dispatcher = QAbstractEventDispatcher::instance();
    int idle = 0;
    for (int i = 0; i < 1000 && idle < 2; ++i) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const bool worked = dispatcher != nullptr
            && dispatcher->processEvents(QEventLoop::AllEvents);
        idle = worked ? 0 : idle + 1;
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

// A virtual clock over every QTimer the station server owns (its heartbeat
// and delta flush timers, and each peer's handshake deadline, which is
// parented to that peer's transport). advance() fires them in virtual time
// order by emitting their timeout directly, exactly as a real expiry would:
// a single-shot timer stops first, a repeating one runs on. The same
// timers still run in real time too, but a fixture run lasts milliseconds,
// so only the 50 ms delta flush can fire on its own, and that only moves a
// delta earlier, never out of order.
class VirtualClock {
public:
    explicit VirtualClock(QObject* root)
        : m_root(root)
    {
    }

    qint64 now() const { return m_now; }

    void scan()
    {
        m_tracked.erase(std::remove_if(m_tracked.begin(), m_tracked.end(),
                                       [](const Tracked& t) {
                                           return t.timer.isNull() || !t.timer->isActive();
                                       }),
                        m_tracked.end());
        const QList<QTimer*> timers = m_root->findChildren<QTimer*>();
        for (QTimer* timer : timers) {
            if (!timer->isActive()) {
                continue;
            }
            // A coarse timer (Qt's default) may report up to 5% more than
            // its interval; the virtual clock keeps to the interval.
            const int remaining =
                std::clamp(timer->remainingTime(), 0, std::max(0, timer->interval()));
            auto it = std::find_if(m_tracked.begin(), m_tracked.end(),
                                   [timer](const Tracked& t) { return t.timer == timer; });
            if (it == m_tracked.end()) {
                Tracked t;
                t.timer = timer;
                t.due = m_now + remaining;
                t.realRemaining = remaining;
                t.since.start();
                m_tracked.push_back(t);
            } else if (remaining > it->realRemaining - it->since.elapsed() + kRestartSlackMs) {
                it->due = m_now + remaining;
                it->realRemaining = remaining;
                it->since.restart();
            }
        }
    }

    /// Empty on success; a description when a timeout could not be fired.
    QString advance(qint64 ms)
    {
        const qint64 target = m_now + ms;
        for (int guard = 0; guard < 100000; ++guard) {
            drain();
            scan();
            auto next = std::min_element(m_tracked.begin(), m_tracked.end(),
                                         [](const Tracked& a, const Tracked& b) {
                                             return a.due < b.due;
                                         });
            if (next == m_tracked.end() || next->due > target) {
                break;
            }
            m_now = std::max(m_now, next->due);
            QPointer<QTimer> timer = next->timer;
            if (timer->isSingleShot()) {
                timer->stop();
                m_tracked.erase(next);
            } else {
                timer->start();
                next->due = m_now + std::max(1, timer->interval());
                next->realRemaining = timer->interval();
                next->since.restart();
            }
            if (!QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection)) {
                return QStringLiteral("could not fire a timer's timeout");
            }
        }
        m_now = target;
        drain();
        scan();
        return QString();
    }

private:
    struct Tracked {
        QPointer<QTimer> timer;
        qint64 due = 0;
        qint64 realRemaining = 0;
        QElapsedTimer since;
    };

    QObject* m_root = nullptr;
    qint64 m_now = 0;
    std::vector<Tracked> m_tracked;
};

} // namespace

QString LinkFixtures::dataDirectory()
{
    return QStringLiteral(NEREUS_LINK_DATA_DIR);
}

QJsonObject LinkFixtures::readObject(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read %1").arg(path);
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("%1 is not one JSON object: %2")
                     .arg(path, parseError.errorString());
        return {};
    }
    error->clear();
    return doc.object();
}

QString LinkFixtures::checkManifest(const QJsonObject& manifest, const QString& directory)
{
    QString problem = expectKeys(manifest, {QStringLiteral("linkMajors"), QStringLiteral("fixtures")},
                                 {}, QStringLiteral("manifest"));
    if (!problem.isEmpty()) {
        return problem;
    }
    // The link majors the suite covers: whole numbers from 1 to 65535,
    // oldest first without repeats, each one this station supports
    // (LinkVersion::supportedMajors(), not a list written here).
    const QJsonArray majors = manifest.value(QStringLiteral("linkMajors")).toArray();
    if (majors.isEmpty()) {
        return QStringLiteral("manifest: linkMajors must name at least one link major");
    }
    const QList<quint16> supported = LinkVersion::supportedMajors();
    double previous = 0.0;
    for (const QJsonValue& value : majors) {
        const double major = value.toDouble(-1.0);
        if (!value.isDouble() || major < 1.0 || major > 65535.0
            || major != static_cast<double>(static_cast<qint64>(major)) || major <= previous) {
            return QStringLiteral("manifest: linkMajors must be whole numbers from 1 to 65535, "
                                  "oldest first, without repeats");
        }
        if (!supported.contains(static_cast<quint16>(major))) {
            return QStringLiteral("manifest: linkMajors names %1, which this station does not "
                                  "support")
                .arg(static_cast<qint64>(major));
        }
        previous = major;
    }
    const QDir root(directory);
    QSet<QString> ids;
    QSet<QString> listed;
    const QJsonArray fixtures = manifest.value(QStringLiteral("fixtures")).toArray();
    if (fixtures.isEmpty()) {
        return QStringLiteral("manifest: no fixtures");
    }
    for (int i = 0; i < fixtures.size(); ++i) {
        const QString where = QStringLiteral("manifest fixtures[%1]").arg(i);
        if (!fixtures.at(i).isObject()) {
            return where + QStringLiteral(": not an object");
        }
        const QJsonObject entry = fixtures.at(i).toObject();
        problem = expectKeys(entry,
                             {QStringLiteral("id"), QStringLiteral("file"), QStringLiteral("kind"),
                              QStringLiteral("requires")},
                             {}, where);
        if (!problem.isEmpty()) {
            return problem;
        }
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString file = entry.value(QStringLiteral("file")).toString();
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        if (id.isEmpty() || ids.contains(id)) {
            return QStringLiteral("%1: id \"%2\" is empty or used twice").arg(where, id);
        }
        ids.insert(id);
        if (!entry.value(QStringLiteral("requires")).isObject()) {
            return where + QStringLiteral(": requires must be an object");
        }
        const QJsonObject requirements = entry.value(QStringLiteral("requires")).toObject();
        for (auto it = requirements.constBegin(); it != requirements.constEnd(); ++it) {
            const double version = it.value().toDouble(-1.0);
            if (!it.value().isDouble() || version < 1.0 || version != static_cast<qint64>(version)) {
                return QStringLiteral("%1: requires.%2 must be a whole version of at least 1")
                    .arg(where, it.key());
            }
        }
        const QString folder = kind == QStringLiteral("control")   ? QStringLiteral("control/")
                               : kind == QStringLiteral("session") ? QStringLiteral("sessions/")
                               : kind == QStringLiteral("media")   ? QStringLiteral("media/")
                                                                   : QString();
        if (folder.isEmpty()) {
            return QStringLiteral("%1: unknown kind \"%2\"").arg(where, kind);
        }
        const bool media = kind == QStringLiteral("media");
        if (!file.startsWith(folder) || !file.endsWith(media ? QStringLiteral(".bin")
                                                             : QStringLiteral(".json"))) {
            return QStringLiteral("%1: %2 fixture \"%3\" must be %4*%5")
                .arg(where, kind, file, folder,
                     media ? QStringLiteral(".bin") : QStringLiteral(".json"));
        }
        if (!QFileInfo::exists(root.filePath(file))) {
            return QStringLiteral("%1: %2 does not exist").arg(where, file);
        }
        listed.insert(file);
        if (media) {
            const QString expect = file.chopped(4) + QStringLiteral(".expect.json");
            if (!QFileInfo::exists(root.filePath(expect))) {
                return QStringLiteral("%1: %2 does not exist").arg(where, expect);
            }
            listed.insert(expect);
        }
    }
    for (const QString& folder : {QStringLiteral("control"), QStringLiteral("sessions"),
                                  QStringLiteral("media")}) {
        QDirIterator it(root.filePath(folder), QDir::Files);
        while (it.hasNext()) {
            const QString relative = root.relativeFilePath(it.next());
            if (!listed.contains(relative)) {
                return QStringLiteral("manifest: %1 is not listed").arg(relative);
            }
        }
    }
    return QString();
}

QList<quint16> LinkFixtures::linkMajors(const QJsonObject& manifest)
{
    QList<quint16> majors;
    for (const QJsonValue& value : manifest.value(QStringLiteral("linkMajors")).toArray()) {
        majors.append(static_cast<quint16>(value.toInt()));
    }
    return majors;
}

QList<LinkFixtures::Entry> LinkFixtures::entries(const QJsonObject& manifest, const QString& kind)
{
    QList<Entry> out;
    for (const QJsonValue& value : manifest.value(QStringLiteral("fixtures")).toArray()) {
        const QJsonObject o = value.toObject();
        if (o.value(QStringLiteral("kind")).toString() != kind) {
            continue;
        }
        out.append(Entry{o.value(QStringLiteral("id")).toString(),
                         o.value(QStringLiteral("file")).toString(), kind,
                         o.value(QStringLiteral("requires")).toObject()});
    }
    return out;
}

QString LinkFixtures::match(const QJsonValue& expected, const QJsonValue& actual,
                            Captures* captures, const QString& path)
{
    if (isPlaceholder(expected)) {
        const QString text = expected.toString();
        if (actual.isUndefined()) {
            return QStringLiteral("%1: expected %2, the key is absent").arg(path, text);
        }
        if (text == QStringLiteral("$any")) {
            return QString();
        }
        if (text == QStringLiteral("$majors")) {
            return QStringLiteral("%1: $majors stands only as a hello's majors").arg(path);
        }
        if (text == QStringLiteral("$object")) {
            return actual.isObject()
                       ? QString()
                       : QStringLiteral("%1: expected an object, got %2").arg(path, shown(actual));
        }
        if (text == QStringLiteral("$string") || text.startsWith(QStringLiteral("$string:"))) {
            if (!actual.isString()) {
                return QStringLiteral("%1: expected a string, got %2").arg(path, shown(actual));
            }
            const QString name = placeholderArgument(text, QStringLiteral("$string:"));
            if (!name.isEmpty()) {
                captures->insert(name, actual);
            }
            return QString();
        }
        if (text == QStringLiteral("$int") || text.startsWith(QStringLiteral("$int:"))) {
            NamedInt named;
            if (text != QStringLiteral("$int") && !parseNamedInt(text, &named)) {
                return QStringLiteral("%1: %2 is not $int:<name>[:<min>:<max>]").arg(path, text);
            }
            if (!isWholeNumber(actual)) {
                return QStringLiteral("%1: expected a whole number, got %2")
                    .arg(path, shown(actual));
            }
            if (named.ranged && (actual.toDouble() < named.min || actual.toDouble() > named.max)) {
                return QStringLiteral("%1: expected a whole number from %2 to %3, got %4")
                    .arg(path, text.section(QLatin1Char(':'), 2, 2),
                         text.section(QLatin1Char(':'), 3, 3), shown(actual));
            }
            if (!named.name.isEmpty()) {
                captures->insert(named.name, actual);
            }
            return QString();
        }
        if (text.startsWith(QStringLiteral("$within:"))) {
            double tolerance = 0.0;
            double centre = 0.0;
            if (!parseWithin(text, &tolerance, &centre)) {
                return QStringLiteral("%1: %2 is not $within:<tolerance>:<value>").arg(path, text);
            }
            if (!actual.isDouble() || std::abs(actual.toDouble() - centre) > tolerance) {
                const QStringList written = text.mid(8).split(QLatin1Char(':'));
                return QStringLiteral("%1: expected a number within %2 of %3, got %4")
                    .arg(path, written.at(0), written.at(1), shown(actual));
            }
            return QString();
        }
        const QString capture = placeholderArgument(text, QStringLiteral("$capture:"));
        if (!capture.isEmpty()) {
            captures->insert(capture, actual);
            return QString();
        }
        const QString ref = placeholderArgument(text, QStringLiteral("$ref:"));
        if (!captures->contains(ref)) {
            return QStringLiteral("%1: $ref:%2 names nothing captured").arg(path, ref);
        }
        return match(captures->value(ref), actual, captures, path);
    }

    if (expected.isObject()) {
        if (!actual.isObject()) {
            return QStringLiteral("%1: expected an object, got %2").arg(path, shown(actual));
        }
        const QJsonObject e = expected.toObject();
        const QJsonObject a = actual.toObject();
        for (auto it = e.constBegin(); it != e.constEnd(); ++it) {
            const QString child = path + QLatin1Char('.') + it.key();
            if (!a.contains(it.key())) {
                return QStringLiteral("%1: expected %2, the key is absent")
                    .arg(child, shown(it.value()));
            }
            if (it.value() == QJsonValue(QStringLiteral("$majors"))) {
                if (it.key() != QStringLiteral("majors")) {
                    return QStringLiteral("%1: $majors stands only as a hello's majors").arg(child);
                }
                const QString problem =
                    majorsProblem(a.value(it.key()), a.value(QStringLiteral("major")), child);
                if (!problem.isEmpty()) {
                    return problem;
                }
                continue;
            }
            const QString difference = match(it.value(), a.value(it.key()), captures, child);
            if (!difference.isEmpty()) {
                return difference;
            }
        }
        for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
            if (!e.contains(it.key())) {
                return QStringLiteral("%1.%2: not expected, got %3")
                    .arg(path, it.key(), shown(it.value()));
            }
        }
        return QString();
    }

    if (expected.isArray()) {
        if (!actual.isArray()) {
            return QStringLiteral("%1: expected an array, got %2").arg(path, shown(actual));
        }
        const QJsonArray e = expected.toArray();
        const QJsonArray a = actual.toArray();
        const qsizetype common = std::min(e.size(), a.size());
        for (qsizetype i = 0; i < common; ++i) {
            const QString difference =
                match(e.at(i), a.at(i), captures, QStringLiteral("%1[%2]").arg(path).arg(i));
            if (!difference.isEmpty()) {
                return difference;
            }
        }
        if (e.size() != a.size()) {
            return QStringLiteral("%1: expected %2 elements, got %3; first extra: %4")
                .arg(path)
                .arg(e.size())
                .arg(a.size())
                .arg(e.size() > a.size() ? shown(e.at(common)) : shown(a.at(common)));
        }
        return QString();
    }

    if (expected.isDouble()) {
        return actual.isDouble() && actual.toDouble() == expected.toDouble()
                   ? QString()
                   : QStringLiteral("%1: expected %2, got %3")
                         .arg(path, shown(expected), shown(actual));
    }

    return expected == actual ? QString()
                              : QStringLiteral("%1: expected %2, got %3")
                                    .arg(path, shown(expected), shown(actual));
}

QJsonValue LinkFixtures::substitute(const QJsonValue& value, Captures* captures,
                                    int* counter, QString* error)
{
    if (isPlaceholder(value)) {
        const QString text = value.toString();
        if (text == QStringLiteral("$string") || text.startsWith(QStringLiteral("$string:"))) {
            const QJsonValue filled(QStringLiteral("conformance"));
            const QString name = placeholderArgument(text, QStringLiteral("$string:"));
            if (!name.isEmpty()) {
                captures->insert(name, filled);
            }
            return filled;
        }
        if (text == QStringLiteral("$int")) {
            return QJsonValue(0);
        }
        if (text.startsWith(QStringLiteral("$int:"))) {
            NamedInt named;
            if (!parseNamedInt(text, &named)) {
                *error = QStringLiteral("%1 is not $int:<name>[:<min>:<max>]").arg(text);
                return {};
            }
            const QJsonValue filled(++*counter);
            if (named.ranged && (filled.toDouble() < named.min || filled.toDouble() > named.max)) {
                *error = QStringLiteral("%1: the counter's %2 is outside its range")
                             .arg(text).arg(*counter);
                return {};
            }
            captures->insert(named.name, filled);
            return filled;
        }
        if (text == QStringLiteral("$object")) {
            return QJsonObject{};
        }
        const QString ref = placeholderArgument(text, QStringLiteral("$ref:"));
        if (ref.isEmpty()) {
            *error = QStringLiteral("%1 cannot stand in a client message").arg(text);
            return {};
        }
        if (!captures->contains(ref)) {
            *error = QStringLiteral("$ref:%1 names nothing captured").arg(ref);
            return {};
        }
        return captures->value(ref);
    }
    if (value.isObject()) {
        QJsonObject out;
        const QJsonObject in = value.toObject();
        for (auto it = in.constBegin(); it != in.constEnd(); ++it) {
            if (it.value() == QJsonValue(QStringLiteral("$majors"))) {
                continue;  // Filled below, from the filled major.
            }
            out.insert(it.key(), substitute(it.value(), captures, counter, error));
            if (!error->isEmpty()) {
                return {};
            }
        }
        for (auto it = in.constBegin(); it != in.constEnd(); ++it) {
            if (it.value() != QJsonValue(QStringLiteral("$majors"))) {
                continue;
            }
            if (it.key() != QStringLiteral("majors") || !isWholeNumber(out.value(QStringLiteral("major")))) {
                *error = QStringLiteral("$majors stands only as a hello's majors");
                return {};
            }
            // The sender's own list: the major it chose, alone.
            out.insert(it.key(), QJsonArray{out.value(QStringLiteral("major"))});
        }
        return out;
    }
    if (value.isArray()) {
        QJsonArray out;
        for (const QJsonValue& element : value.toArray()) {
            out.append(substitute(element, captures, counter, error));
            if (!error->isEmpty()) {
                return {};
            }
        }
        return out;
    }
    return value;
}

QString LinkFixtures::runControl(const QJsonObject& fixture)
{
    const QString problem = expectKeys(fixture,
                                       {QStringLiteral("from"), QStringLiteral("wire"),
                                        QStringLiteral("decodes")},
                                       {}, QStringLiteral("control fixture"));
    if (!problem.isEmpty()) {
        return problem;
    }
    const QString from = fixture.value(QStringLiteral("from")).toString();
    if (from != QStringLiteral("station") && from != QStringLiteral("client")) {
        return QStringLiteral("control fixture: from must be \"station\" or \"client\"");
    }
    if (!fixture.value(QStringLiteral("wire")).isObject()
        || !fixture.value(QStringLiteral("decodes")).isBool()) {
        return QStringLiteral("control fixture: wire must be an object and decodes a bool");
    }
    const QJsonObject wire = fixture.value(QStringLiteral("wire")).toObject();
    const bool decodes = fixture.value(QStringLiteral("decodes")).toBool();

    SessionMessage message;
    const bool decoded = SessionMessages::decode(
        QJsonDocument(wire).toJson(QJsonDocument::Compact), &message);
    if (decoded != decodes) {
        return decodes ? QStringLiteral("the station's decoder refused a message the fixture "
                                        "says decodes: %1")
                             .arg(shown(wire))
                       : QStringLiteral("the station's decoder accepted a message the fixture "
                                        "says it refuses: %1")
                             .arg(shown(wire));
    }
    if (!decoded) {
        return QString();
    }
    const QJsonObject again =
        QJsonDocument::fromJson(SessionMessages::encode(message)).object();
    Captures none;
    const QString difference = match(wire, again, &none);
    if (!difference.isEmpty()) {
        return QStringLiteral("encoding the decoded message again differs at %1")
            .arg(difference);
    }
    return QString();
}

bool LinkFixtures::runsOn(const QJsonObject& fixture, const QString& end)
{
    for (const QJsonValue& value : fixture.value(QStringLiteral("runs")).toArray()) {
        if (value.toString() == end) {
            return true;
        }
    }
    return false;
}

QString LinkFixtures::checkSessionFormat(const QJsonObject& fixture)
{
    QString problem = expectKeys(fixture,
                                 {QStringLiteral("runs"), QStringLiteral("stationSetup"),
                                  QStringLiteral("steps")},
                                 {}, QStringLiteral("session fixture"));
    if (!problem.isEmpty()) {
        return problem;
    }
    const QJsonValue runs = fixture.value(QStringLiteral("runs"));
    QSet<QString> ends;
    for (const QJsonValue& value : runs.toArray()) {
        const QString end = value.toString();
        if ((end != QStringLiteral("station") && end != QStringLiteral("app"))
            || ends.contains(end)) {
            ends.clear();
            break;
        }
        ends.insert(end);
    }
    if (!runs.isArray() || ends.isEmpty() || ends.size() != runs.toArray().size()) {
        return QStringLiteral("session fixture: runs must list \"station\" and/or \"app\", "
                              "each once");
    }
    if (!fixture.value(QStringLiteral("stationSetup")).isObject()) {
        return QStringLiteral("session fixture: stationSetup must be an object");
    }
    const QJsonArray steps = fixture.value(QStringLiteral("steps")).toArray();
    if (steps.isEmpty()) {
        return QStringLiteral("session fixture: no steps");
    }
    for (int index = 0; index < steps.size(); ++index) {
        const QString where = QStringLiteral("step %1").arg(index);
        if (!steps.at(index).isObject()) {
            return where + QStringLiteral(": not an object");
        }
        const QJsonObject step = steps.at(index).toObject();
        if (step.contains(QStringLiteral("from"))) {
            const QString from = step.value(QStringLiteral("from")).toString();
            if (from == QStringLiteral("client")) {
                problem = expectKeys(step,
                                     {QStringLiteral("from"), QStringLiteral("role"),
                                      QStringLiteral("message")},
                                     {}, where);
                const QString role = step.value(QStringLiteral("role")).toString();
                if (problem.isEmpty() && role != QStringLiteral("behaviour")
                    && role != QStringLiteral("scripted")) {
                    problem = where + QStringLiteral(": role must be \"behaviour\" or "
                                                     "\"scripted\"");
                }
            } else if (from == QStringLiteral("station")) {
                problem = expectKeys(step, {QStringLiteral("from"), QStringLiteral("message")},
                                     {}, where);
            } else {
                problem = where + QStringLiteral(": from must be \"station\" or \"client\"");
            }
            if (problem.isEmpty() && !step.value(QStringLiteral("message")).isObject()) {
                problem = where + QStringLiteral(": message must be an object");
            }
        } else if (step.contains(QStringLiteral("advanceMs"))) {
            problem = expectKeys(step, {QStringLiteral("advanceMs")}, {}, where);
            const double ms = step.value(QStringLiteral("advanceMs")).toDouble(-1.0);
            if (problem.isEmpty()
                && (ms < 0.0 || ms != static_cast<double>(static_cast<qint64>(ms)))) {
                problem = where + QStringLiteral(": advanceMs must be a whole number of at "
                                                 "least 0");
            }
        } else if (step.contains(QStringLiteral("expectClosed"))) {
            problem = expectKeys(step, {QStringLiteral("expectClosed")}, {}, where);
            if (problem.isEmpty()) {
                problem = expectKeys(step.value(QStringLiteral("expectClosed")).toObject(),
                                     {QStringLiteral("retryable")}, {},
                                     where + QStringLiteral(" expectClosed"));
            }
        } else {
            problem = where + QStringLiteral(": not a message, advanceMs or expectClosed step");
        }
        if (!problem.isEmpty()) {
            return problem;
        }
    }
    return QString();
}

namespace {

// iPhone app Task 12: the runner's own device for "$device:<case>" (the
// link document, section 16.1). Its key is made at run time in a scratch
// directory; stationSetup's "pairedDevice" puts it in the Core's paired
// devices before the client connects.
struct ConformanceDevice {
    QTemporaryDir dir;
    StationIdentity key = StationIdentity::loadOrCreate(dir.path());
};

bool mentionsDevice(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString().startsWith(QStringLiteral("$device:"));
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        return std::any_of(object.begin(), object.end(),
                           [](const QJsonValue& v) { return mentionsDevice(v); });
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        return std::any_of(array.begin(), array.end(),
                           [](const QJsonValue& v) { return mentionsDevice(v); });
    }
    return false;
}

// Fills every "$device:<case>" with the device block the runner's device
// sends: "signed" signs this connection's transcript (the challenge the
// station's hello gave, recorded as "challenge"); "otherChallenge" signs one
// with a challenge of the runner's own; "otherCertificate" signs one that
// binds a certificate other than the station's.
QJsonValue fillDevice(const QJsonValue& value, const ConformanceDevice& device,
                      const StationServer& server, const LinkFixtures::Captures& captures,
                      QString* error)
{
    if (value.isString() && value.toString().startsWith(QStringLiteral("$device:"))) {
        const QString which = value.toString().mid(8);
        if (!captures.contains(QStringLiteral("challenge"))) {
            *error = QStringLiteral("$device needs the station's challenge recorded as "
                                    "\"challenge\"");
            return {};
        }
        bool ok = false;
        QByteArray challenge = StationIdentity::fromBase64Url(
            captures.value(QStringLiteral("challenge")).toString(), &ok);
        QString pin = server.certificateFingerprint();
        pin.remove(QLatin1Char(':'));
        QByteArray certSha256 = QByteArray::fromHex(pin.toLatin1());
        if (which == QStringLiteral("otherChallenge")) {
            quint32 words[DeviceAuthenticator::kChallengeBytes / sizeof(quint32)]{};
            QRandomGenerator::system()->fillRange(words);
            challenge = QByteArray(reinterpret_cast<const char*>(words), sizeof(words));
        } else if (which == QStringLiteral("otherCertificate")) {
            certSha256 = StationIdentity::fingerprintOf(QByteArrayLiteral("another certificate"));
        } else if (which != QStringLiteral("signed")) {
            *error = QStringLiteral("%1 is not $device:signed, $device:otherChallenge or "
                                    "$device:otherCertificate").arg(value.toString());
            return {};
        }
        if (!ok || !device.key.isValid()) {
            *error = QStringLiteral("$device: no usable challenge or device key");
            return {};
        }
        const QByteArray transcript = DeviceAuthenticator::transcript(
            challenge, certSha256, server.stationIdentity().publicKeySpki(),
            device.key.publicKeySpki());
        return QJsonObject{
            {QStringLiteral("id"), StationIdentity::toBase64Url(device.key.fingerprint())},
            {QStringLiteral("publicKey"), StationIdentity::toBase64Url(device.key.publicKeySpki())},
            {QStringLiteral("name"), QStringLiteral("Conformance device")},
            {QStringLiteral("kind"), QStringLiteral("phone")},
            {QStringLiteral("signature"),
             StationIdentity::toBase64Url(device.key.sign(transcript))},
        };
    }
    if (value.isObject()) {
        QJsonObject out;
        const QJsonObject in = value.toObject();
        for (auto it = in.constBegin(); it != in.constEnd(); ++it) {
            out.insert(it.key(), fillDevice(it.value(), device, server, captures, error));
        }
        return out;
    }
    return value;
}

} // namespace

QString LinkFixtures::runSession(const QJsonObject& fixture, StationServer& server,
                                 LoopbackTransport& transport)
{
    QString problem = checkSessionFormat(fixture);
    if (!problem.isEmpty()) {
        return problem;
    }
    if (!runsOn(fixture, QStringLiteral("station"))) {
        return QStringLiteral("session fixture: \"runs\" does not name the station");
    }
    const QJsonObject setup = fixture.value(QStringLiteral("stationSetup")).toObject();
    const QJsonArray steps = fixture.value(QStringLiteral("steps")).toArray();
    if (steps.isEmpty()) {
        return QStringLiteral("session fixture: no steps");
    }

    transport.setAnswersPings(setup.value(QStringLiteral("clientAnswersPings")).toBool(true));
    int preemptAfter = -1;
    if (setup.contains(QStringLiteral("preemptingClient"))) {
        preemptAfter = setup.value(QStringLiteral("preemptingClient"))
                           .toObject()
                           .value(QStringLiteral("afterStep"))
                           .toInt(-1);
        if (preemptAfter < 0 || preemptAfter >= steps.size()) {
            return QStringLiteral("stationSetup.preemptingClient.afterStep must name a step");
        }
    }

    Captures captures;
    captures.insert(QStringLiteral("token"), server.token());
    int counter = 0;

    // iPhone app Task 12: the runner's device, when the fixture uses one.
    std::unique_ptr<ConformanceDevice> device;
    const bool paired = setup.value(QStringLiteral("pairedDevice")).toBool(false);
    if (paired || mentionsDevice(QJsonValue(steps))) {
        device = std::make_unique<ConformanceDevice>();
        if (!device->key.isValid()) {
            return QStringLiteral("the runner's device key could not be made");
        }
    }
    if (paired) {
        PairedDevice record;
        record.id = device->key.fingerprint();
        record.publicKeySpki = device->key.publicKeySpki();
        record.name = QStringLiteral("Conformance device");
        record.kind = QStringLiteral("phone");
        if (server.deviceStore() == nullptr || !server.deviceStore()->add(record)) {
            return QStringLiteral("stationSetup.pairedDevice: the device could not be paired");
        }
        captures.insert(QStringLiteral("device:self"),
                        StationIdentity::toBase64Url(device->key.fingerprint()));
    }
    // iPhone app Task 13: devices besides the runner's own, paired before
    // the client connects, their keys made here and their ids recorded as
    // "device:1" to "device:<n>" in the order they were paired.
    std::vector<std::unique_ptr<ConformanceDevice>> otherDevices;
    const QJsonValue othersValue = setup.value(QStringLiteral("otherPairedDevices"));
    const double othersCount = othersValue.toDouble(0.0);
    if (!othersValue.isUndefined()
        && (!othersValue.isDouble() || othersCount < 0.0 || othersCount > 16.0
            || othersCount != static_cast<double>(static_cast<int>(othersCount)))) {
        return QStringLiteral("stationSetup.otherPairedDevices must be a whole number from 0 "
                              "to 16");
    }
    for (int n = 1; n <= static_cast<int>(othersCount); ++n) {
        auto other = std::make_unique<ConformanceDevice>();
        PairedDevice record;
        record.id = other->key.fingerprint();
        record.publicKeySpki = other->key.publicKeySpki();
        record.name = QStringLiteral("Other device %1").arg(n);
        record.kind = QStringLiteral("tablet");
        if (!other->key.isValid() || server.deviceStore() == nullptr
            || !server.deviceStore()->add(record)) {
            return QStringLiteral("stationSetup.otherPairedDevices: device %1 could not be "
                                  "paired").arg(n);
        }
        captures.insert(QStringLiteral("device:%1").arg(n),
                        StationIdentity::toBase64Url(record.id));
        otherDevices.push_back(std::move(other));
    }
    VirtualClock clock(&server);
    int consumed = 0;
    bool lastRetryable = false;
    bool sawEnding = false;
    std::unique_ptr<LoopbackTransport> preempting;

    const auto describe = [&steps](int index) {
        const QJsonObject step = steps.at(index).toObject();
        QString what;
        if (step.contains(QStringLiteral("from"))) {
            what = QStringLiteral("%1 %2")
                       .arg(step.value(QStringLiteral("from")).toString(),
                            step.value(QStringLiteral("message"))
                                .toObject()
                                .value(QStringLiteral("type"))
                                .toString());
        } else if (step.contains(QStringLiteral("advanceMs"))) {
            what = QStringLiteral("advanceMs");
        } else {
            what = QStringLiteral("expectClosed");
        }
        return QStringLiteral("step %1 (%2)").arg(index).arg(what);
    };

    const auto unconsumed = [&transport, &consumed]() {
        QStringList kinds;
        const QList<QByteArray> received = transport.received();
        for (int i = consumed; i < received.size(); ++i) {
            kinds.append(QString::fromUtf8(received.at(i).left(kMaxShownChars)));
        }
        return kinds;
    };

    drain();
    clock.scan();

    for (int index = 0; index < steps.size(); ++index) {
        if (!steps.at(index).isObject()) {
            return QStringLiteral("step %1: not an object").arg(index);
        }
        const QJsonObject step = steps.at(index).toObject();

        if (step.contains(QStringLiteral("from"))) {
            const QString from = step.value(QStringLiteral("from")).toString();
            const QJsonValue message = step.value(QStringLiteral("message"));
            if (!message.isObject()) {
                return QStringLiteral("%1: message must be an object").arg(describe(index));
            }
            if (from == QStringLiteral("client")) {
                QString error;
                // Behaviour or scripted, the station's runner sends the
                // message itself, placeholders filled (section 16.3).
                QJsonValue outgoing = message;
                if (device) {
                    outgoing = fillDevice(message, *device, server, captures, &error);
                    if (!error.isEmpty()) {
                        return QStringLiteral("%1: %2").arg(describe(index), error);
                    }
                }
                const QJsonValue sent = substitute(outgoing, &captures, &counter, &error);
                if (!error.isEmpty()) {
                    return QStringLiteral("%1: %2").arg(describe(index), error);
                }
                if (!transport.isOpen()) {
                    return QStringLiteral("%1: the link is already closed").arg(describe(index));
                }
                transport.sendText(QJsonDocument(sent.toObject()).toJson(QJsonDocument::Compact));
                drain();
                clock.scan();
            } else if (from == QStringLiteral("station")) {
                drain();
                if (transport.received().size() <= consumed) {
                    const QDeadlineTimer deadline(kStationReplyWaitMs);
                    while (transport.received().size() <= consumed && !deadline.hasExpired()) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                    }
                }
                clock.scan();
                const QList<QByteArray> received = transport.received();
                if (received.size() <= consumed) {
                    return QStringLiteral("%1: the station sent nothing more (the link is %2); "
                                          "expected %3")
                        .arg(describe(index),
                             transport.isOpen() ? QStringLiteral("open")
                                                : QStringLiteral("closed"),
                             shown(message));
                }
                const QByteArray wire = received.at(consumed++);
                const QJsonDocument doc = QJsonDocument::fromJson(wire);
                if (!doc.isObject()) {
                    return QStringLiteral("%1: the station sent something that is not a JSON "
                                          "object: %2")
                        .arg(describe(index), QString::fromUtf8(wire.left(kMaxShownChars)));
                }
                const QJsonObject actual = doc.object();
                const QString difference = match(message, actual, &captures);
                if (!difference.isEmpty()) {
                    return QStringLiteral("%1: %2\n  the station sent: %3")
                        .arg(describe(index), difference, shown(actual));
                }
                const QString type = actual.value(QStringLiteral("type")).toString();
                if (type == QStringLiteral("session.end") || type == QStringLiteral("auth.result")) {
                    lastRetryable = actual.value(QStringLiteral("retryable")).toBool(false);
                    sawEnding = true;
                }
            } else {
                return QStringLiteral("step %1: from must be \"station\" or \"client\"").arg(index);
            }
        } else if (step.contains(QStringLiteral("advanceMs"))) {
            problem = expectKeys(step, {QStringLiteral("advanceMs")}, {},
                                 QStringLiteral("step %1").arg(index));
            const double ms = step.value(QStringLiteral("advanceMs")).toDouble(-1.0);
            if (!problem.isEmpty() || ms < 0.0 || ms != static_cast<double>(static_cast<qint64>(ms))) {
                return QStringLiteral("step %1: advanceMs must be a whole number of at least 0")
                    .arg(index);
            }
            const QString error = clock.advance(static_cast<qint64>(ms));
            if (!error.isEmpty()) {
                return QStringLiteral("%1: %2").arg(describe(index), error);
            }
        } else if (step.contains(QStringLiteral("expectClosed"))) {
            problem = expectKeys(step, {QStringLiteral("expectClosed")}, {},
                                 QStringLiteral("step %1").arg(index));
            const QJsonObject closed = step.value(QStringLiteral("expectClosed")).toObject();
            if (problem.isEmpty()) {
                problem = expectKeys(closed, {QStringLiteral("retryable")}, {},
                                     QStringLiteral("step %1 expectClosed").arg(index));
            }
            if (!problem.isEmpty()) {
                return problem;
            }
            drain();
            if (transport.isOpen()) {
                const QDeadlineTimer deadline(kStationReplyWaitMs);
                while (transport.isOpen() && !deadline.hasExpired()) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                }
            }
            if (transport.isOpen()) {
                return QStringLiteral("%1: the station did not close the link").arg(describe(index));
            }
            const QStringList left = unconsumed();
            if (!left.isEmpty()) {
                return QStringLiteral("%1: the station sent %2 message(s) the fixture does not "
                                      "list before closing, first: %3")
                    .arg(describe(index))
                    .arg(left.size())
                    .arg(left.first());
            }
            if (!sawEnding) {
                return QStringLiteral("%1: the station closed without a session.end or "
                                      "auth.result")
                    .arg(describe(index));
            }
            const bool retryable = closed.value(QStringLiteral("retryable")).toBool();
            if (retryable != lastRetryable) {
                return QStringLiteral("%1: expected retryable %2, the station said %3")
                    .arg(describe(index))
                    .arg(retryable ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(lastRetryable ? QStringLiteral("true") : QStringLiteral("false"));
            }
        } else {
            return QStringLiteral("step %1: not a message, advanceMs or expectClosed step")
                .arg(index);
        }

        if (index == preemptAfter) {
            preempting = std::make_unique<LoopbackTransport>(
                QStringLiteral("conformance-second-client"));
            auto* stationEnd = new LoopbackTransport(QStringLiteral("conformance-second"), &server);
            stationEnd->linkTo(preempting.get());
            server.acceptTransport(stationEnd);
            preempting->sendText(SessionMessages::encode(SessionMessages::hello(
                kSessionProtocolMajor, kSessionProtocolMinor, 0,
                QStringLiteral("conformance-second"))));
            preempting->sendText(
                SessionMessages::encode(SessionMessages::authRequest(server.token())));
            drain();
            clock.scan();
        }
    }

    const QJsonObject last = steps.last().toObject();
    if (!last.contains(QStringLiteral("expectClosed"))) {
        drain();
        if (!transport.isOpen()) {
            return QStringLiteral("after the last step: the station closed the link; the "
                                  "fixture does not expect it");
        }
    }
    return QString();
}

// ── Media vectors ────────────────────────────────────────────────────────

namespace {

QJsonArray numbers(const std::vector<double>& values)
{
    QJsonArray out;
    for (const double value : values) {
        out.append(value);
    }
    return out;
}

bool readNumbers(const QJsonObject& json, const QString& key, std::vector<double>* out,
                 QString* error)
{
    if (!json.value(key).isArray()) {
        *error = QStringLiteral("%1 must be an array of numbers").arg(key);
        return false;
    }
    out->clear();
    for (const QJsonValue& value : json.value(key).toArray()) {
        if (!value.isDouble()) {
            *error = QStringLiteral("%1 must be an array of numbers").arg(key);
            return false;
        }
        out->push_back(value.toDouble());
    }
    return true;
}

// A whole number that fits a double exactly (the JSON rule of the link
// document's section 4.1).
bool readWhole(const QJsonObject& json, const QString& key, qint64* out, QString* error)
{
    const QJsonValue value = json.value(key);
    const double d = value.toDouble();
    if (!value.isDouble() || d != static_cast<double>(static_cast<qint64>(d))) {
        *error = QStringLiteral("%1 must be a whole number").arg(key);
        return false;
    }
    *out = static_cast<qint64>(d);
    return true;
}

} // namespace

StationLanAnnouncement LinkMediaVectors::lanAnnouncement()
{
    StationLanAnnouncement value;
    value.controlPort = 50055;
    // A made-up certificate pin in the station's format: 32 bytes as
    // uppercase hex pairs joined by colons. Not any station's.
    QStringList pairs;
    for (int i = 0; i < 32; ++i) {
        pairs.append(QStringLiteral("%1").arg(0xA0 + i, 2, 16, QLatin1Char('0')).toUpper());
    }
    value.fingerprint = pairs.join(QLatin1Char(':'));
    value.coreName = QStringLiteral("Shack Core");
    value.radioName = QStringLiteral("Bench HL2");
    value.radioMac = QStringLiteral("AA:BB:CC:DD:EE:01");
    value.radioConnected = true;
    return value;
}

QJsonObject LinkMediaVectors::toJson(const StationLanAnnouncement& value)
{
    return QJsonObject{
        {QStringLiteral("controlPort"), value.controlPort},
        {QStringLiteral("fingerprint"), value.fingerprint},
        {QStringLiteral("coreName"), value.coreName},
        {QStringLiteral("radioName"), value.radioName},
        {QStringLiteral("radioMac"), value.radioMac},
        {QStringLiteral("radioConnected"), value.radioConnected},
    };
}

bool LinkMediaVectors::fromJson(const QJsonObject& json, StationLanAnnouncement* value,
                                QString* error)
{
    const QString problem = expectKeys(
        json,
        {QStringLiteral("controlPort"), QStringLiteral("fingerprint"), QStringLiteral("coreName"),
         QStringLiteral("radioName"), QStringLiteral("radioMac"), QStringLiteral("radioConnected")},
        {}, QStringLiteral("announcement expect"));
    if (!problem.isEmpty()) {
        *error = problem;
        return false;
    }
    qint64 port = 0;
    if (!readWhole(json, QStringLiteral("controlPort"), &port, error) || port < 0 || port > 65535
        || !json.value(QStringLiteral("radioConnected")).isBool()) {
        if (error->isEmpty()) {
            *error = QStringLiteral("controlPort or radioConnected is out of range");
        }
        return false;
    }
    value->controlPort = static_cast<quint16>(port);
    value->fingerprint = json.value(QStringLiteral("fingerprint")).toString();
    value->coreName = json.value(QStringLiteral("coreName")).toString();
    value->radioName = json.value(QStringLiteral("radioName")).toString();
    value->radioMac = json.value(QStringLiteral("radioMac")).toString();
    value->radioConnected = json.value(QStringLiteral("radioConnected")).toBool();
    return true;
}

Ps3Snapshot LinkMediaVectors::ps3Snapshot()
{
    // Eight measured points and four correction points. Every value is a
    // multiple of 1/8, so it reads the same in any JSON library.
    Ps3Snapshot snapshot;
    snapshot.channelId = 5;
    snapshot.sessionGeneration = 7;
    snapshot.sequence = 3;
    snapshot.capturedAtUnixMilliseconds = 1790000000000;
    snapshot.sampleCount = 8;
    snapshot.correctionCount = 4;
    for (int i = 0; i < snapshot.sampleCount; ++i) {
        snapshot.x.push_back(0.125 * i);
        snapshot.ym.push_back(0.5 + 0.0625 * i);
        // Written so the first value is +0.0: JSON cannot carry -0.0.
        snapshot.yc.push_back(0.25 * static_cast<double>(-i));
        snapshot.ys.push_back(1.0 - 0.125 * i);
    }
    for (int i = 0; i < snapshot.correctionCount; ++i) {
        snapshot.xmCorrection.push_back(0.25 * i);
        snapshot.ymCorrection.push_back(1.0 + 0.125 * i);
        snapshot.xaCorrection.push_back(0.25 * i);
        snapshot.yaCorrection.push_back(-2.5 + 0.5 * i);
    }
    snapshot.phaseReferenceDegrees = 12.5;
    return snapshot;
}

QJsonObject LinkMediaVectors::toJson(const Ps3Snapshot& snapshot)
{
    return QJsonObject{
        {QStringLiteral("channelId"), snapshot.channelId},
        {QStringLiteral("sessionGeneration"), static_cast<qint64>(snapshot.sessionGeneration)},
        {QStringLiteral("sequence"), static_cast<qint64>(snapshot.sequence)},
        {QStringLiteral("capturedAtUnixMilliseconds"),
         static_cast<qint64>(snapshot.capturedAtUnixMilliseconds)},
        {QStringLiteral("sampleCount"), snapshot.sampleCount},
        {QStringLiteral("correctionCount"), snapshot.correctionCount},
        {QStringLiteral("x"), numbers(snapshot.x)},
        {QStringLiteral("ym"), numbers(snapshot.ym)},
        {QStringLiteral("yc"), numbers(snapshot.yc)},
        {QStringLiteral("ys"), numbers(snapshot.ys)},
        {QStringLiteral("xmCorrection"), numbers(snapshot.xmCorrection)},
        {QStringLiteral("ymCorrection"), numbers(snapshot.ymCorrection)},
        {QStringLiteral("xaCorrection"), numbers(snapshot.xaCorrection)},
        {QStringLiteral("yaCorrection"), numbers(snapshot.yaCorrection)},
        {QStringLiteral("phaseReferenceDegrees"), snapshot.phaseReferenceDegrees},
    };
}

bool LinkMediaVectors::fromJson(const QJsonObject& json, Ps3Snapshot* snapshot, QString* error)
{
    const QStringList keys{
        QStringLiteral("channelId"),     QStringLiteral("sessionGeneration"),
        QStringLiteral("sequence"),      QStringLiteral("capturedAtUnixMilliseconds"),
        QStringLiteral("sampleCount"),   QStringLiteral("correctionCount"),
        QStringLiteral("x"),             QStringLiteral("ym"),
        QStringLiteral("yc"),            QStringLiteral("ys"),
        QStringLiteral("xmCorrection"),  QStringLiteral("ymCorrection"),
        QStringLiteral("xaCorrection"),  QStringLiteral("yaCorrection"),
        QStringLiteral("phaseReferenceDegrees"),
    };
    const QString problem = expectKeys(json, keys, {}, QStringLiteral("PS3D expect"));
    if (!problem.isEmpty()) {
        *error = problem;
        return false;
    }
    qint64 channel = 0;
    qint64 generation = 0;
    qint64 sequence = 0;
    qint64 captured = 0;
    qint64 samples = 0;
    qint64 corrections = 0;
    if (!readWhole(json, QStringLiteral("channelId"), &channel, error)
        || !readWhole(json, QStringLiteral("sessionGeneration"), &generation, error)
        || !readWhole(json, QStringLiteral("sequence"), &sequence, error)
        || !readWhole(json, QStringLiteral("capturedAtUnixMilliseconds"), &captured, error)
        || !readWhole(json, QStringLiteral("sampleCount"), &samples, error)
        || !readWhole(json, QStringLiteral("correctionCount"), &corrections, error)
        || !json.value(QStringLiteral("phaseReferenceDegrees")).isDouble()) {
        if (error->isEmpty()) {
            *error = QStringLiteral("phaseReferenceDegrees must be a number");
        }
        return false;
    }
    Ps3Snapshot out;
    out.channelId = static_cast<int>(channel);
    out.sessionGeneration = static_cast<std::uint64_t>(generation);
    out.sequence = static_cast<std::uint64_t>(sequence);
    out.capturedAtUnixMilliseconds = captured;
    out.sampleCount = static_cast<int>(samples);
    out.correctionCount = static_cast<int>(corrections);
    out.phaseReferenceDegrees = json.value(QStringLiteral("phaseReferenceDegrees")).toDouble();
    if (!readNumbers(json, QStringLiteral("x"), &out.x, error)
        || !readNumbers(json, QStringLiteral("ym"), &out.ym, error)
        || !readNumbers(json, QStringLiteral("yc"), &out.yc, error)
        || !readNumbers(json, QStringLiteral("ys"), &out.ys, error)
        || !readNumbers(json, QStringLiteral("xmCorrection"), &out.xmCorrection, error)
        || !readNumbers(json, QStringLiteral("ymCorrection"), &out.ymCorrection, error)
        || !readNumbers(json, QStringLiteral("xaCorrection"), &out.xaCorrection, error)
        || !readNumbers(json, QStringLiteral("yaCorrection"), &out.yaCorrection, error)) {
        return false;
    }
    *snapshot = out;
    return true;
}

QList<DisplayCodecFrame> LinkMediaVectors::nsdcFrames()
{
    // One endpoint, one context: 32 trace and 32 waterfall samples between
    // -140 and -40 dBm, no wide row. Each frame drifts the rows by 0.5 dB
    // so the deltas carry small residuals.
    DisplayCodecContext context;
    context.endpointId = 1;
    context.contextGeneration = 1;
    context.minDbm = -140.0f;
    context.maxDbm = -40.0f;
    context.traceSamples = 32;
    context.waterfallSamples = 32;
    context.wideSamples = 0;
    QList<DisplayCodecFrame> frames;
    for (quint32 sequence = 1; sequence <= 4; ++sequence) {
        DisplayCodecFrame frame;
        frame.context = context;
        frame.encoderSequence = sequence;
        frame.producerTimestamp = 20'000'000ULL * sequence;
        frame.waterfallAdvance = true;
        const float drift = 0.5f * static_cast<float>(sequence - 1);
        for (int i = 0; i < 32; ++i) {
            frame.traceDbm.append(-120.0f + 20.0f * static_cast<float>(std::sin(0.3 * i)) + drift);
            frame.waterfallDbm.append(-110.0f + 10.0f * static_cast<float>(std::cos(0.2 * i))
                                      + drift);
        }
        frames.append(frame);
    }
    return frames;
}

// The first plane's prefix starts right after the 42-byte header:
// blockSizeCode at 42, blockCount at 43..44; the sequence is at 16..19.
QByteArray LinkMediaVectors::nsdcBadPlaneDelta(const QByteArray& delta)
{
    QByteArray bytes = delta;
    bytes[42] = 4;
    return bytes;
}

QByteArray LinkMediaVectors::nsdcStaleTruncatedDelta(const QByteArray& delta)
{
    QByteArray bytes = delta;
    for (int i = 16; i < 20; ++i) {
        bytes[i] = 0;
    }
    bytes.chop(1);
    return bytes;
}

QByteArray LinkMediaVectors::nsdcBadBlockCountKeyframe(const QByteArray& keyframe)
{
    QByteArray bytes = keyframe;
    const quint16 count = static_cast<quint16>(
        (static_cast<quint8>(bytes.at(43)) << 8) | static_cast<quint8>(bytes.at(44)));
    const quint16 wrong = static_cast<quint16>(count + 1U);
    bytes[43] = static_cast<char>(wrong >> 8);
    bytes[44] = static_cast<char>(wrong & 0xFF);
    return bytes;
}

QString LinkMediaVectors::nsdcDispositionName(DisplayCodecDisposition disposition)
{
    switch (disposition) {
    case DisplayCodecDisposition::Accepted: return QStringLiteral("accepted");
    case DisplayCodecDisposition::NeedKeyframe: return QStringLiteral("needKeyframe");
    case DisplayCodecDisposition::Rejected: return QStringLiteral("rejected");
    }
    return QString();
}

QString LinkMediaVectors::nsdcReasonName(DisplayCodecReason reason)
{
    switch (reason) {
    case DisplayCodecReason::None: return QStringLiteral("none");
    case DisplayCodecReason::InvalidInput: return QStringLiteral("invalidInput");
    case DisplayCodecReason::NoHistory: return QStringLiteral("noHistory");
    case DisplayCodecReason::SequenceGap: return QStringLiteral("sequenceGap");
    case DisplayCodecReason::StaleSequence: return QStringLiteral("staleSequence");
    case DisplayCodecReason::OldContext: return QStringLiteral("oldContext");
    case DisplayCodecReason::ContextMismatch: return QStringLiteral("contextMismatch");
    case DisplayCodecReason::BadMagic: return QStringLiteral("badMagic");
    case DisplayCodecReason::UnsupportedVersion: return QStringLiteral("unsupportedVersion");
    case DisplayCodecReason::UnknownFlags: return QStringLiteral("unknownFlags");
    case DisplayCodecReason::Truncated: return QStringLiteral("truncated");
    case DisplayCodecReason::Oversized: return QStringLiteral("oversized");
    case DisplayCodecReason::Malformed: return QStringLiteral("malformed");
    }
    return QString();
}

QJsonObject LinkMediaVectors::toJson(const DisplayCodecDecodeResult& result)
{
    QJsonObject out{
        {QStringLiteral("disposition"), nsdcDispositionName(result.disposition)},
        {QStringLiteral("reason"), nsdcReasonName(result.reason)},
    };
    if (result.disposition != DisplayCodecDisposition::Accepted) {
        return out;
    }
    const auto rows = [](const QVector<float>& values) {
        QJsonArray array;
        for (const float value : values) {
            array.append(static_cast<double>(value));
        }
        return array;
    };
    const DisplayCodecFrame& frame = result.frame;
    out.insert(QStringLiteral("endpointId"), static_cast<qint64>(frame.context.endpointId));
    out.insert(QStringLiteral("contextGeneration"),
               static_cast<qint64>(frame.context.contextGeneration));
    out.insert(QStringLiteral("minDbm"), static_cast<double>(frame.context.minDbm));
    out.insert(QStringLiteral("maxDbm"), static_cast<double>(frame.context.maxDbm));
    out.insert(QStringLiteral("encoderSequence"), static_cast<qint64>(frame.encoderSequence));
    out.insert(QStringLiteral("producerTimestamp"), static_cast<qint64>(frame.producerTimestamp));
    out.insert(QStringLiteral("waterfallAdvance"), frame.waterfallAdvance);
    out.insert(QStringLiteral("traceDbm"), rows(frame.traceDbm));
    out.insert(QStringLiteral("waterfallDbm"), rows(frame.waterfallDbm));
    out.insert(QStringLiteral("wideDbm"), rows(frame.wideDbm));
    return out;
}

QVector<float> LinkMediaVectors::opusInput(int index)
{
    constexpr double kPi = 3.14159265358979323846;
    const int frames = OpusAudioCodecConfig::kFrameSamples;
    QVector<float> pcm;
    pcm.reserve(frames * OpusAudioCodecConfig::kChannels);
    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(index * frames + i)
                         / static_cast<double>(OpusAudioCodecConfig::kSampleRate);
        pcm.append(static_cast<float>(0.3 * std::sin(2.0 * kPi * 440.0 * t)));
        pcm.append(static_cast<float>(0.2 * std::sin(2.0 * kPi * 1000.0 * t)));
    }
    return pcm;
}

QString LinkMediaVectors::opusStatusName(OpusAudioCodecStatus status)
{
    switch (status) {
    case OpusAudioCodecStatus::Accepted: return QStringLiteral("accepted");
    case OpusAudioCodecStatus::Concealed: return QStringLiteral("concealed");
    case OpusAudioCodecStatus::InvalidInput: return QStringLiteral("invalidInput");
    case OpusAudioCodecStatus::EncodeFailed: return QStringLiteral("encodeFailed");
    case OpusAudioCodecStatus::DecodeFailed: return QStringLiteral("decodeFailed");
    case OpusAudioCodecStatus::MalformedRtp: return QStringLiteral("malformedRtp");
    case OpusAudioCodecStatus::UnexpectedSsrc: return QStringLiteral("unexpectedSsrc");
    case OpusAudioCodecStatus::Oversized: return QStringLiteral("oversized");
    }
    return QString();
}

QJsonObject LinkMediaVectors::toJson(const OpusRtpDecodeResult& result)
{
    QJsonArray pcm;
    for (const float sample : result.pcmInterleaved) {
        const double scaled = std::round(static_cast<double>(sample) * 32767.0);
        pcm.append(static_cast<qint64>(std::clamp(scaled, -32768.0, 32767.0)));
    }
    return QJsonObject{
        {QStringLiteral("status"), opusStatusName(result.status)},
        {QStringLiteral("sequence"), static_cast<qint64>(result.sequence)},
        {QStringLiteral("timestamp"), static_cast<qint64>(result.timestamp)},
        {QStringLiteral("channels"), result.packetInfo.channels},
        {QStringLiteral("bandwidth"), result.packetInfo.bandwidth},
        {QStringLiteral("samplesPerChannel"), result.packetInfo.samplesPerChannel},
        {QStringLiteral("pcm16"), pcm},
    };
}

} // namespace NereusSDR::Test
