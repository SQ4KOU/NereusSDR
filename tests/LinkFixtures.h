// no-port-check: NereusSDR-original.
#pragma once
// =================================================================
// tests/LinkFixtures.h  (NereusSDR)
// =================================================================
//
// iPhone app plan, Task 3 (R-IOS-01): the station's half of the link's
// conformance suite. tests/data/link/v1/ holds fixtures both ends run: the
// station's runners here, the app's in its own tests. The format is the
// contract of the link document's section 16 (Conformance):
//
//   manifest.json   {"linkMajors":[1],"fixtures":[{"id","file","kind",
//                   "requires"}]}
//   control/*.json  {"from":"station"|"client","wire":{...},"decodes":bool}
//   sessions/*.json {"stationSetup":{...},"steps":[...]}, a step being
//                   {"from","message"}, {"advanceMs":N} or
//                   {"expectClosed":{"retryable":bool}}
//   media/*.bin     one packet as it travels, with *.expect.json
//                   {"codec":...,"expect":{...}}; "after" in expect names
//                   the vectors a fresh decoder takes first
//
// This file holds what every station runner shares: the loader, the
// placeholder matcher ("$any", "$string", "$int", "$capture:<name>",
// "$ref:<name>"), the control fixture check and the session script player.
// Building the station a session fixture describes (its stationSetup) is
// the session test's own job, because it needs the fake radio.
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
// =================================================================

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <QJsonArray>

#include <QVector>

#include "core/session/media/DisplayCodec.h"
#include "core/session/media/OpusAudioCodec.h"

namespace NereusSDR {
class StationServer;
struct Ps3Snapshot;
struct StationLanAnnouncement;
}

namespace NereusSDR::Test {

class LoopbackTransport;

class LinkFixtures {
public:
    /// The values "$capture:<name>" recorded, by name.
    using Captures = QHash<QString, QJsonValue>;

    struct Entry {
        QString id;
        QString file;
        QString kind;
        QJsonObject requirements;
    };

    /// tests/data/link/v1 in the source tree (NEREUS_LINK_DATA_DIR).
    static QString dataDirectory();

    /// A JSON object read from `path`; an empty object and `error` set when
    /// the file is missing or is not one JSON object.
    static QJsonObject readObject(const QString& path, QString* error);

    /// manifest.json, checked: linkMajors [1], every entry's four fields,
    /// unique ids, a known kind, an existing file (a media entry names its
    /// .bin and needs the .expect.json beside it), and every file under
    /// control/, sessions/ and media/ listed exactly once. Empty on pass.
    static QString checkManifest(const QJsonObject& manifest, const QString& directory);

    /// The manifest's linkMajors. Every runner runs its fixtures once per
    /// major in it, against a station that offers that major.
    static QList<quint16> linkMajors(const QJsonObject& manifest);

    /// The manifest's entries of one kind, in manifest order.
    static QList<Entry> entries(const QJsonObject& manifest, const QString& kind);

    /// Matches an expected value, placeholders allowed, against an actual
    /// one. Objects need the same keys, arrays the same length, numbers the
    /// same value (1 and 1.0 are equal). "$capture:<name>" records the
    /// actual value into `captures`; "$ref:<name>" must equal a recorded
    /// one. Returns the first difference, naming its path from `path`;
    /// empty when they match.
    static QString match(const QJsonValue& expected, const QJsonValue& actual,
                         Captures* captures, const QString& path = QStringLiteral("$"));

    /// A message to inject, with every "$ref:<name>" replaced by its
    /// recorded value. Any other placeholder is an error: a runner cannot
    /// invent a value to send.
    static QJsonValue substitute(const QJsonValue& value, const Captures& captures,
                                 QString* error);

    /// Decodes `wire` with the station's codec (SessionMessages::decode);
    /// when `decodes` is true, encodes the result again and compares the
    /// two objects. Empty on pass, else what differed.
    static QString runControl(const QJsonObject& fixture);

    /// Plays a session fixture against `server`. `transport` is the
    /// client's end, linked to a station end the caller has just handed to
    /// server.acceptTransport(). The client's messages are sent through it
    /// and the station's are matched in the order they arrive. Time moves
    /// only through {"advanceMs":N}: the player keeps a virtual clock over
    /// the server's own timers (the handshake deadline, the heartbeat, the
    /// delta flush) and fires each when its virtual time comes. It reads
    /// two stationSetup keys itself: "clientAnswersPings" (default true)
    /// and "preemptingClient": {"afterStep": i}, a second client that
    /// authenticates once step i is done. "$ref:token" in a client message
    /// is the station's token, read at run time; no fixture holds one.
    /// Returns an empty string on pass, else the failing step and why.
    static QString runSession(const QJsonObject& fixture, NereusSDR::StationServer& server,
                              LoopbackTransport& transport);
};

/// The media vectors' fixed inputs and their JSON form. The regen target
/// encodes these with the station's own encoders; the media runner decodes
/// the committed bytes, compares with the committed expectation, and
/// encodes the expectation again to compare with the committed bytes.
class LinkMediaVectors {
public:
    static NereusSDR::StationLanAnnouncement lanAnnouncement();
    static QJsonObject toJson(const NereusSDR::StationLanAnnouncement& value);
    /// False, with `error` set, when `json` is not one announcement.
    static bool fromJson(const QJsonObject& json, NereusSDR::StationLanAnnouncement* value,
                         QString* error);

    static NereusSDR::Ps3Snapshot ps3Snapshot();
    /// Every decoded field of a PS3D frame; the eight value lists as arrays.
    static QJsonObject toJson(const NereusSDR::Ps3Snapshot& snapshot);
    static bool fromJson(const QJsonObject& json, NereusSDR::Ps3Snapshot* snapshot,
                         QString* error);

    /// Four display frames one endpoint sends in order, sequences 1 to 4:
    /// a keyframe, two deltas, and a keyframe the sender was asked for
    /// after the third was lost (encode it with requestKeyframe = true).
    static QList<NereusSDR::DisplayCodecFrame> nsdcFrames();
    /// A decode result as the vector's expectation holds it: disposition
    /// and reason by name, and the frame's fields when it was accepted.
    static QJsonObject toJson(const NereusSDR::DisplayCodecDecodeResult& result);
    static QString nsdcDispositionName(NereusSDR::DisplayCodecDisposition disposition);
    static QString nsdcReasonName(NereusSDR::DisplayCodecReason reason);

    /// The Opus vectors: RTP synchronisation source, first sequence and
    /// first timestamp, and packet `index`'s stereo input, interleaved
    /// (1920 frames of a 440 Hz tone left and 1000 Hz right, continuous
    /// across packets).
    static constexpr quint32 kOpusSsrc = 0x4E524553U;
    static constexpr quint16 kOpusFirstSequence = 100;
    static constexpr quint32 kOpusFirstTimestamp = 0;
    static constexpr int kOpusPackets = 4;
    static QVector<float> opusInput(int index);
    /// A decode result as the vector's expectation holds it: status by
    /// name, sequence, timestamp, the packet's channels, Opus bandwidth
    /// constant and samples per channel, and the PCM as 16-bit values
    /// (round(sample * 32767), clamped).
    static QJsonObject toJson(const NereusSDR::OpusRtpDecodeResult& result);
    static QString opusStatusName(NereusSDR::OpusAudioCodecStatus status);
};

} // namespace NereusSDR::Test
