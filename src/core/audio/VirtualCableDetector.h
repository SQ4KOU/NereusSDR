// src/core/audio/VirtualCableDetector.h (NereusSDR)
// Detects known Windows virtual-audio-cable products by regex-matching OS
// device name strings. NereusSDR-original; no Thetis/AetherSDR port.
#pragma once
#include <QString>
#include <QVector>

namespace NereusSDR {

enum class VirtualCableProduct {
    None = 0,
    VbCable,
    VbCableA,  VbCableB,  VbCableC,  VbCableD,
    VbHiFiCable,
    MuzychenkoVac,
    Voicemeeter,
    Dante,
    FlexRadioDax,
    NereusSdrVax,  // reserved for future NereusSDR-owned Windows driver
};

struct DetectedCable {
    VirtualCableProduct product;
    QString deviceName;
    bool isInput;  // false = render / output
    int channel;   // 1..N for multi-cable families; 0 if N/A
};

class VirtualCableDetector {
public:
    // Pure-function test-hook. Inspects the OS device name string.
    static VirtualCableProduct matchProduct(const QString& deviceName);

    // Enumerates OS audio devices via PortAudio and returns matches.
    static QVector<DetectedCable> scan();

    // Test-friendly pure filter — drops NereusSdrVax entries from the input.
    // Used by scanThirdPartyOnly() and exposed as a seam so unit tests can
    // exercise the filter without standing up a PortAudio enumeration.
    static QVector<DetectedCable> filterThirdParty(const QVector<DetectedCable>& all);

    // Same as scan() but drops NereusSdrVax entries. Windows first-run path
    // calls this to surface only user-installable 3rd-party cables; the
    // NereusSdrVax enum is reserved for a future NereusSDR-owned driver and
    // must not be offered to the user as a binding target.
    static QVector<DetectedCable> scanThirdPartyOnly();

    // Vendor install URL for a given product (for the first-run helper).
    static QString installUrl(VirtualCableProduct p);

    // Startup rescan-diff helpers.
    //
    // fingerprintCsv() hashes the device-name string of each detected cable
    // to an 8-hex-char tag (SHA-256 truncated) and comma-joins the tags.
    // Empty string for an empty input; no trailing comma. SHA-256 is chosen
    // over qHash so the tag is stable across processes — qHash is randomized
    // per run and would spuriously flag every cable as "new" on every launch.
    //
    // diffNewCables() returns the cables in `current` whose fingerprint is
    // NOT present in `lastCsv`. Used by MainWindow::checkVaxFirstRun() to
    // decide whether to pop the RescanNewCables scenario on startup.
    static QString fingerprintCsv(const QVector<DetectedCable>& cables);
    static QVector<DetectedCable> diffNewCables(const QVector<DetectedCable>& current,
                                                const QString& lastCsv);
};

} // namespace NereusSDR
