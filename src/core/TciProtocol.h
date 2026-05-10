// no-port-check: NereusSDR-original TciProtocol skeleton — parser ported from
// Thetis TCIServer.cs:4900-4924 [v2.10.3.13] (split on first ':', case-insensitive
// command lookup), dispatch shape from TCIServer.cs:4924-5197 [v2.10.3.13] (two
// switches: set if parts.Length==2, query if parts.Length==1). Empty handler
// stubs land Phase 5+. AetherSDR seam pattern reference: TciProtocol.{h,cpp} [@0cd4559].

// src/core/TciProtocol.h  (NereusSDR)
// NereusSDR-original — TCI command protocol handler.
//
// Parser ported from Thetis TCIServer.cs:4900-4924 [v2.10.3.13].
// Two-switch dispatch shape from Thetis TCIServer.cs:4924-5197 [v2.10.3.13].
// AetherSDR seam pattern reference: TciProtocol.{h,cpp} [@0cd4559].
//
// This file REPLACES the Phase 1 stub (commit 77d27b3).
//
// Modification history (NereusSDR):
//   2026-05-10 — Phase 3J-1 Task 3.1 by J.J. Boyd (KG4VCF);
//                AI-assisted transformation via Anthropic Claude Code.

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace NereusSDR {

// ─────────────────────────────────────────────────────────────────────────
// AppSettings keys introduced by Phase 3J-1 TCI Server.
// All persisted to ~/.config/NereusSDR/NereusSDR.settings via AppSettings
// (NOT QSettings; per project convention). PascalCase key names.
//
// | Key                                  | Type   | Default | Notes
// | TciServerEnabled                     | bool   | False   | Server on/off
// | TciServerPort                        | int    | 50001   | Bind port (addr always 127.0.0.1)
// | TciSendInitialFrequencyStateOnConnect| bool   | True    | VFO/IF/DDS in init burst
// | TciRateLimitMsgsPerSec               | int    | 60      | Per-client message rate cap
// | TciAudioStreamSamples                | int    | 2048    | Audio block size [100..2048]
// | TciTxChannel                         | string | "Both"  | TX audio channel: Left/Right/Both
// | TciRxSensorIntervalMs                | int    | 200     | RX sensor push [30..1000]
// | TciTxSensorIntervalMs                | int    | 200     | TX sensor push [30..1000]
// | TciEmulateExpertSDR3Protocol         | bool   | False   | Compat flag
// | TciEmulateSunSDR2Pro                 | bool   | False   | Compat flag
// | TciCwluBecomesCw                     | bool   | False   | Compat flag
// | TciCwBecomesCwuAbove10mhz            | bool   | False   | Compat flag (W2PA #559)
// | TciIqSwap                            | bool   | True    | Compat flag
// | TciAlwaysStreamIq                    | bool   | False   | Compat flag
// | TciForgetRx2VfoBOnDisconnect         | bool   | False   | VFO quirk
// | TciUseRx1VfoaForRx2Vfoa              | bool   | False   | VFO quirk
// | TciCopyRx2VfobToVfoa                 | bool   | False   | VFO quirk
//
// Phases 5+ wire these into compat-flag handling. Phase 20 surfaces them in
// Setup → Network → TCI Server. See design doc Section 10.
// ─────────────────────────────────────────────────────────────────────────

class TciProtocol : public QObject {
    Q_OBJECT
public:
    explicit TciProtocol(QObject* radio = nullptr, QObject* parent = nullptr);
    ~TciProtocol() override = default;

    // Dispatch a single TCI command line. Returns the synchronous response
    // (empty for commands with no reply, or for the silent-error invariant
    // — unknown commands produce zero outbound traffic per design doc §4.1).
    QString handleCommand(const QString& command);

    // Notification queue — drained by TciServer after each handleCommand.
    bool hasPendingNotification() const;
    QString takePendingNotification();

    // Build the post-connect init burst. Stub returns empty list in Phase 3;
    // Phase 4 Task 4.1 replaces with the 8-line wrapper from
    // Thetis TCIServer.cs:2512-2552 [v2.10.3.13].
    QStringList buildInitBurst() const;

    // Slice ↔ trx mapping (NereusSDR architectural divergence per design doc §1.2):
    //   Slice A | trx:0,    Slice B | trx:1,    Slice C | trx:2,    Slice D | trx:3
    // Identity mapping in Phase 3; same mapping persists through subsequent phases.
    static int sliceToTrx(int slice);
    static int trxToSlice(int trx);

    // Phase 3 test-only instrumentation — incremented in handleSetCommand /
    // handleQueryCommand stubs so the dispatch-seam test can verify routing.
    // Phase 5+ may keep or remove these counters.
    int setDispatchCount() const { return m_setDispatchCount; }
    int queryDispatchCount() const { return m_queryDispatchCount; }
    void resetDispatchCounters();

private:
    // From Thetis TCIServer.cs:4924-5128 [v2.10.3.13] — 60-case set-command switch.
    // Phase 5+ adds individual cases via the matrix runner.
    QString handleSetCommand(const QString& name, const QStringList& args);

    // From Thetis TCIServer.cs:5134-5197 [v2.10.3.13] — 21-case query-command switch.
    // Phase 5+ adds individual cases via the matrix runner.
    QString handleQueryCommand(const QString& name);

    // From Thetis TCIServer.cs:2363-2510 [v2.10.3.13] — sendInitialRadioState body.
    // Phase 4 Task 4.2 fills the body (up to 97 wire lines per Sweep D).
    // Each build<Foo>Line helper is a pure static function: takes value args,
    // returns a formatted line. Phase 6+ feeds real RadioModel state.
    QStringList buildInitialRadioStateLines() const;

    // ── Freq / IF / VFO helpers ───────────────────────────────────────────────
    // From Thetis TCIServer.cs:2334-2348 [v2.10.3.13] — sendDDS format string.
    // ddsFreq += GetDSPcwPitchShiftToZero(rx+1); //MW0LGE [2.9.0.7]
    static QString buildDdsLine(int rx, qint64 hz);
    // From Thetis TCIServer.cs:2096-2120 [v2.10.3.13] — sendIF format string.
    // offset += -GetDSPcwPitchShiftToZero(rx+1); //MW0LGE [2.9.0.7] note we invert with -
    static QString buildIfLine(int rx, int chan, qint64 offsetHz);
    // From Thetis TCIServer.cs:2061-2095 [v2.10.3.13] — sendVFO format string.
    static QString buildVfoLine(int rx, int chan, qint64 hz);
    // From Thetis TCIServer.cs:2246-2259 [v2.10.3.13] — sendTXFrequencyChanged.
    static QString buildTxFrequencyLine(qint64 hz);
    static QString buildTxFrequencyThetisLine(qint64 hz, const QString& band,
                                              bool rx2en, bool txvfob);

    // ── Mode / filter helpers ─────────────────────────────────────────────────
    // From Thetis TCIServer.cs:2136-2157 [v2.10.3.13] — sendMode format string.
    static QString buildModulationLine(int rx, const QString& modeUpper);
    // From Thetis TCIServer.cs:2349-2353 [v2.10.3.13] — sendFilterBand.
    // Adjacent sendDDS at TCIServer.cs:2344 carries: //MW0LGE [2.9.0.7]
    static QString buildRxFilterBandLine(int rx, int lowHz, int highHz);

    // ── RX-enable / NR / NB / BIN / ANF / APF / NF ───────────────────────────
    // From Thetis TCIServer.cs:2279-2283 [v2.10.3.13] — sendRXEnable.
    static QString buildRxEnableLine(int rx, bool en);
    // From Thetis TCIServer.cs:4472-4480 [v2.10.3.13] — sendNrEnable (non-extended).
    static QString buildRxNrEnableLine(int rx, bool en);
    // From Thetis TCIServer.cs:4472-4480 [v2.10.3.13] — sendNrEnable (extended).
    static QString buildRxNrEnableExLine(int rx, bool en, int nrIndex);
    // From Thetis TCIServer.cs:1901-1905 [v2.10.3.13] — sendRxNbEnable.
    static QString buildRxNbEnableLine(int rx, bool en);
    // From Thetis TCIServer.cs:1906-1910 [v2.10.3.13] — sendRxBinEnable.
    static QString buildRxBinEnableLine(int rx, bool en);
    // From Thetis TCIServer.cs:4482-4487 [v2.10.3.13] — sendAnfEnable.
    static QString buildRxAnfEnableLine(int rx, bool en);
    // From Thetis TCIServer.cs:1911-1915 [v2.10.3.13] — sendRxApfEnable.
    static QString buildRxApfEnableLine(int rx, bool en);
    // From Thetis TCIServer.cs:1916-1920 [v2.10.3.13] — sendRxNfEnable.
    static QString buildRxNfEnableLine(int rx, bool en);

    // ── Volume / balance helpers ──────────────────────────────────────────────
    // From Thetis TCIServer.cs:4563-4568 [v2.10.3.13] — sendRxVolume (F2 C-locale).
    static QString buildRxVolumeLine(int rx, int chan, double db);
    // From Thetis TCIServer.cs:2187-2191 [v2.10.3.13] — sendRxBalance (F2 C-locale).
    static QString buildRxBalanceLine(int rx, int chan, double bal);

    // ── AGC helpers ───────────────────────────────────────────────────────────
    // From Thetis TCIServer.cs:2236-2240 [v2.10.3.13] — sendAgcMode.
    static QString buildAgcModeLine(int rx, const QString& mode);
    // From Thetis TCIServer.cs:2241-2245 [v2.10.3.13] — sendAgcGain.
    static QString buildAgcGainLine(int rx, int gain);

    // ── CTUN / TX profile / calibration ──────────────────────────────────────
    // From Thetis TCIServer.cs:4690-4694 [v2.10.3.13] — sendCTUN (rx_ctun_ex suffix).
    static QString buildRxCtunExLine(int rx, bool en);
    // From Thetis TCIServer.cs:4721-4731 [v2.10.3.13] — sendTXProfiles.
    static QString buildTxProfilesExLine(const QStringList& names);
    // From Thetis TCIServer.cs:4715-4720 [v2.10.3.13] — sendTXProfile.
    static QString buildTxProfileExLine(const QString& active);
    // From Thetis TCIServer.cs:4766-4775 [v2.10.3.13] — sendCalibration (F6 C-locale).
    static QString buildCalibrationExLine(int rx, double meter, double display,
                                          double xvtr, double sixMeter,
                                          double txDisplay);

    // ── RIT / XIT helpers ─────────────────────────────────────────────────────
    // From Thetis TCIServer.cs:1881-1885 [v2.10.3.13] — sendRITEnable.
    static QString buildRitEnableLine(int rx, bool en);
    // From Thetis TCIServer.cs:1886-1890 [v2.10.3.13] — sendXITEnable.
    static QString buildXitEnableLine(int rx, bool en);
    // From Thetis TCIServer.cs:1891-1895 [v2.10.3.13] — sendRITOffset.
    static QString buildRitOffsetLine(int rx, int hz);
    // From Thetis TCIServer.cs:1896-1900 [v2.10.3.13] — sendXITOffset.
    static QString buildXitOffsetLine(int rx, int hz);

    // ── Lock helpers ──────────────────────────────────────────────────────────
    // From Thetis TCIServer.cs:1921-1925 [v2.10.3.13] — sendLock.
    static QString buildLockLine(int rx, bool locked);
    // From Thetis TCIServer.cs:1926-1930 [v2.10.3.13] — sendVFOLock.
    // From Thetis TCIServer.cs:2032-2049 [v2.10.3.13] — sendAllVFOLocks logic.
    // When rx2Enabled: emits vfo_lock:0,0; vfo_lock:1,0; vfo_lock:1,1 (3 lines).
    // When !rx2Enabled: emits vfo_lock:0,0; vfo_lock:0,1 (2 lines).
    static QStringList buildAllVfoLocksLines(bool rx2Enabled,
                                             bool vfoALock, bool vfoBLock);

    // ── SQL / DIGL / DIGU helpers ─────────────────────────────────────────────
    // From Thetis TCIServer.cs:1931-1935 [v2.10.3.13] — sendSqlEnable.
    static QString buildSqlEnableLine(int rx, bool en);
    // From Thetis TCIServer.cs:1936-1940 [v2.10.3.13] — sendSqlLevel.
    static QString buildSqlLevelLine(int rx, int level);
    // From Thetis TCIServer.cs:2051-2055 [v2.10.3.13] — sendDiglOffset.
    static QString buildDiglOffsetLine(int hz);
    // From Thetis TCIServer.cs:2056-2060 [v2.10.3.13] — sendDiguOffset.
    static QString buildDiguOffsetLine(int hz);

    // ── CW macro helpers ──────────────────────────────────────────────────────
    // From Thetis TCIServer.cs:1941-1944 [v2.10.3.13] — sendCwMacrosSpeed.
    static QString buildCwMacrosSpeedLine(int wpm);
    // From Thetis TCIServer.cs:1945-1948 [v2.10.3.13] — sendCwMacrosDelay.
    static QString buildCwMacrosDelayLine(int ms);
    // From Thetis TCIServer.cs:1949-1952 [v2.10.3.13] — sendCwKeyerSpeed.
    static QString buildCwKeyerSpeedLine(int wpm);

    // ── Split / TX-enable helpers ─────────────────────────────────────────────
    // From Thetis TCIServer.cs:1876-1880 [v2.10.3.13] — sendSplit.
    static QString buildSplitEnableLine(int rx, bool en);
    // From Thetis TCIServer.cs:2284-2288 [v2.10.3.13] — sendTXEnable.
    static QString buildTxEnableLine(int rx, bool en);

    // ── RX channel enable ─────────────────────────────────────────────────────
    // From Thetis TCIServer.cs:5887-5890 [v2.10.3.13] — sendRxChannelEnable.
    static QString buildRxChannelEnableLine(int rx, int chan, bool en);

    // ── MOX / TUNE helpers ────────────────────────────────────────────────────
    // From Thetis TCIServer.cs:2121-2131 [v2.10.3.13] — sendMOX (no ",tci" suffix).
    // Adjacent sendIF at TCIServer.cs:2116 carries: //MW0LGE [2.9.0.7] note we invert with -
    static QString buildTrxLine(int rx, bool mox);
    // From Thetis TCIServer.cs:2274-2278 [v2.10.3.13] — sendTune.
    static QString buildTuneLine(int rx, bool tuning);

    // ── IQ helpers ────────────────────────────────────────────────────────────
    // From Thetis TCIServer.cs:5814-5817 [v2.10.3.13] — sendIQStartStop (iq_stop form).
    static QString buildIqStopLine(int rx);
    // From Thetis TCIServer.cs:5364-5370 [v2.10.3.13] — sendIQSampleRate.
    static QString buildIqSampleRateLine(int sr);

    // ── Audio-stream helpers ──────────────────────────────────────────────────
    // From Thetis TCIServer.cs:5372-5375 [v2.10.3.13] — sendAudioSampleRate.
    static QString buildAudioSampleRateLine(int sr);
    // From Thetis TCIServer.cs:5377-5380 [v2.10.3.13] — sendAudioStreamSampleType.
    static QString buildAudioStreamSampleTypeLine(const QString& typeLower);
    // From Thetis TCIServer.cs:5382-5385 [v2.10.3.13] — sendAudioStreamChannels.
    static QString buildAudioStreamChannelsLine(int n);
    // From Thetis TCIServer.cs:5387-5390 [v2.10.3.13] — sendAudioStreamSamples.
    static QString buildAudioStreamSamplesLine(int n);
    // From Thetis TCIServer.cs:5392-5395 [v2.10.3.13] — sendTxStreamAudioBuffering.
    static QString buildTxStreamAudioBufferingLine(int ms);

    // ── Mute / volume / MON helpers ───────────────────────────────────────────
    // From Thetis TCIServer.cs:2158-2162 [v2.10.3.13] — sendMute.
    static QString buildMuteLine(bool muted);
    // From Thetis TCIServer.cs:2163-2167 [v2.10.3.13] — sendMuteRX.
    static QString buildRxMuteLine(int rx, bool muted);
    // From Thetis TCIServer.cs:2173-2179 [v2.10.3.13] — sendVolume (F1, clamp -60..0).
    static QString buildVolumeLine(double db);
    // From Thetis TCIServer.cs:2168-2172 [v2.10.3.13] — sendMONEnable.
    static QString buildMonEnableLine(bool en);
    // From Thetis TCIServer.cs:2180-2186 [v2.10.3.13] — sendMONVolume (F1, clamp -60..0).
    static QString buildMonVolumeLine(double db);

    // ── Start / stop ──────────────────────────────────────────────────────────
    // From Thetis TCIServer.cs:1868-1875 [v2.10.3.13] — sendStartStop.
    static QString buildStartStopLine(bool powerOn);

    QObject* m_radio{nullptr};
    QStringList m_pendingNotifications;
    int m_setDispatchCount{0};
    int m_queryDispatchCount{0};
};

} // namespace NereusSDR
