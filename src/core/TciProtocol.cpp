// no-port-check: NereusSDR-original — parser + dispatch shells ported from
// Thetis TCIServer.cs:4900-5197 [v2.10.3.13]. Handler bodies are stubs;
// Phase 5+ adds individual cases via the matrix runner.

// src/core/TciProtocol.cpp  (NereusSDR)
// NereusSDR-original — TCI command protocol handler implementation.
//
// Parser ported from Thetis TCIServer.cs:4900-4924 [v2.10.3.13].
// Two-switch dispatch shape from Thetis TCIServer.cs:4924-5197 [v2.10.3.13].
//
// This file REPLACES the Phase 1 stub (commit 77d27b3).
//
// Modification history (NereusSDR):
//   2026-05-10 — Phase 3J-1 Task 3.1 by J.J. Boyd (KG4VCF);
//                AI-assisted transformation via Anthropic Claude Code.

#include "TciProtocol.h"
#include "AppSettings.h"

namespace NereusSDR {

TciProtocol::TciProtocol(QObject* radio, QObject* parent)
    : QObject(parent)
    , m_radio(radio)
{
    (void)m_radio;  // dereferenced in Phase 6+ via QMetaObject::invokeMethod for setters.
}

// From Thetis TCIServer.cs:4900-4924 [v2.10.3.13] — text-frame parser.
// Strip trailing ';', split on first ':', case-insensitive command lookup.
// json_spot exception (parts[0]=="spot" AND msg contains "[json]{") routes
// to set path; the actual JSON brace-aware splitter at TCIServer.cs:4785-4900
// is a Phase 12 (spot stubs) concern.
QString TciProtocol::handleCommand(const QString& command)
{
    QString msg = command.trimmed();
    if (msg.endsWith(QLatin1Char(';'))) {
        msg.chop(1);
        msg = msg.trimmed();
    }
    if (msg.isEmpty()) {
        return {};
    }

    const int colonIdx = msg.indexOf(QLatin1Char(':'));
    QStringList parts;
    if (colonIdx >= 0) {
        parts << msg.left(colonIdx);
        parts << msg.mid(colonIdx + 1);
    } else {
        parts << msg;
    }

    const QString name = parts.at(0).toLower().trimmed();

    // From Thetis TCIServer.cs:4917 [v2.10.3.13] — json_spot detour.
    const bool jsonSpot = parts.size() >= 2 && name == QStringLiteral("spot")
                          && msg.toLower().indexOf(QStringLiteral("[json]{")) >= 0;

    if (parts.size() == 2 || jsonSpot) {
        const QStringList args = parts.at(1).split(QLatin1Char(','));
        return handleSetCommand(name, args);
    }
    return handleQueryCommand(name);
}

bool TciProtocol::hasPendingNotification() const
{
    return !m_pendingNotifications.isEmpty();
}

QString TciProtocol::takePendingNotification()
{
    if (m_pendingNotifications.isEmpty()) {
        return {};
    }
    return m_pendingNotifications.takeFirst();
}

// From Thetis TCIServer.cs:2512-2552 [v2.10.3.13] — sendInitialisationData
// emits 8 wrapper lines + buildInitialRadioState() + ready;.
QStringList TciProtocol::buildInitBurst() const
{
    QStringList lines;
    auto& s = AppSettings::instance();

    // From Thetis TCIServer.cs:2515-2520 [v2.10.3.13]
    //MW0LGE_22 emulate ee3 protocol
    const bool emulateEsdr3 = s.value(QStringLiteral("TciEmulateExpertSDR3Protocol"),
                                      QStringLiteral("False")).toString()
                              == QStringLiteral("True");
    const QString protocolName = emulateEsdr3
        ? QStringLiteral("ExpertSDR3")
        : QStringLiteral("Thetis");
    lines << QStringLiteral("protocol:%1,2.0;").arg(protocolName);

    // From Thetis TCIServer.cs:2523-2528 [v2.10.3.13]
    //MW0LGE_22 emulate sunsdr
    const bool emulateSunSdr = s.value(QStringLiteral("TciEmulateSunSDR2Pro"),
                                       QStringLiteral("False")).toString()
                               == QStringLiteral("True");
    // NereusSDR divergence: HardwareSpecific.Model has no NereusSDR equivalent;
    // hardcode "NereusSDR" until Phase 4 Task 4.2 wires RadioModel state.
    const QString deviceName = emulateSunSdr
        ? QStringLiteral("SunSDR2PRO")
        : QStringLiteral("NereusSDR");
    lines << QStringLiteral("device:%1;").arg(deviceName);

    // From Thetis TCIServer.cs:2529 [v2.10.3.13]
    lines << QStringLiteral("receive_only:false;");

    // From Thetis TCIServer.cs:2530 [v2.10.3.13] — locked at 2 per design doc §1.2;
    // Slice C/D are NereusSDR-internal and not exposed via TCI in Phase 3J-1.
    lines << QStringLiteral("trx_count:2;");

    // From Thetis TCIServer.cs:2531 [v2.10.3.13]
    lines << QStringLiteral("channels_count:2;");

    // From Thetis TCIServer.cs:2533 [v2.10.3.13] — sendVFOLimits(0, MaxFreq*1e6).
    // Phase 4 Task 4.1 hardcodes 64 MHz; Task 4.2 wires RadioModel state.
    lines << QStringLiteral("vfo_limits:0,64000000;");

    // From Thetis TCIServer.cs:2535-2536 [v2.10.3.13] — sendIFLimits(-halfSample, halfSample).
    // halfSample = SampleRateRX1 / 2. Phase 4 Task 4.1 hardcodes 96000 (192 kHz / 2);
    // Task 4.2 wires RadioModel state.
    lines << QStringLiteral("if_limits:-96000,96000;");

    // From Thetis TCIServer.cs:2538-2544 [v2.10.3.13]
    // MW0LGE_22b modulations are upper in sun, so replicate
    const bool cwluBecomesCw = s.value(QStringLiteral("TciCwluBecomesCw"),
                                       QStringLiteral("False")).toString()
                               == QStringLiteral("True");
    const QString cwSuffix = cwluBecomesCw
        ? QStringLiteral("cwl,cwu,cw")
        : QStringLiteral("cwl,cwu");
    const QString modList = QStringLiteral("am,sam,dsb,lsb,usb,nfm,fm,digl,digu,%1")
                                .arg(cwSuffix).toUpper();
    lines << QStringLiteral("modulations_list:%1;").arg(modList);

    // From Thetis TCIServer.cs:2546 [v2.10.3.13] — sendInitialRadioState body.
    // Phase 4 Task 4.2 fills the body (~75-87 wire lines per Sweep D).
    lines.append(buildInitialRadioStateLines());

    // From Thetis TCIServer.cs:2548 [v2.10.3.13]
    lines << QStringLiteral("ready;");

    return lines;
}

// From Thetis TCIServer.cs:2363-2510 [v2.10.3.13] — sendInitialRadioState body.
// Emits up to 97 wire frames (subset depending on bSend + bRX2Enabled flags).
// Phase 4 Task 4.2 ports the source order, 6 inline comments verbatim, and
// the typo-fix divergence (design doc §7 row 1). Value args are hardcoded
// placeholders; Phase 6+ wires RadioModel state via QMetaObject::invokeMethod.
//
// audioGainToDb(gain): if gain<=0 → -60.0; if gain>=1 → 0.0;
//   else 20*log10(gain). From Thetis TCIServer.cs:4553-4562 [v2.10.3.13].
// linearToDbVolume(volume): maps [0..100] linearly to [-60..0] dB, clamped.
//   From Thetis TCIServer.cs:4110-4120 [v2.10.3.13].
QStringList TciProtocol::buildInitialRadioStateLines() const
{
    QStringList lines;
    auto& s = AppSettings::instance();

    // From Thetis TCIServer.cs:2365 [v2.10.3.13]
    const bool bSend = s.value(QStringLiteral("TciSendInitialFrequencyStateOnConnect"),
                               QStringLiteral("True")).toString()
                       == QStringLiteral("True");

    // Phase 4 Task 4.2 placeholder — single-RX default (bRX2Enabled = false).
    // Phase 6+ reads RadioModel::rx2Enabled() via QMetaObject::invokeMethod.
    const bool bRX2Enabled = false;

    // Phase 4 Task 4.2 placeholder values.
    // Phase 6+ wires each to the corresponding RadioModel property.
    const qint64 rx1FreqHz  = 14250000;  // 14.250 MHz — 20m USB demo value
    const qint64 rx2FreqHz  = 7100000;   // 7.100 MHz  — 40m USB demo value
    const qint64 txFreqHz   = 14250000;  // 14.250 MHz — demo value
    const QString txBand    = QStringLiteral("20m");
    const QString modeUpper = QStringLiteral("USB");

    // Filter: 200/2900 Hz — common SSB filter placeholder.
    const int filterLow  = 200;
    const int filterHigh = 2900;

    // RX idle defaults — all off.
    const bool mox  = false;
    const bool tune = false;

    // NR off. Phase 6+ reads GetSelectedNR(1/2) via RadioModel.
    const int rx1nr = 0;
    const int rx2nr = 0;

    // RX volume: RX0Gain=100, RX1Gain=0, RX2Gain=100 (0-100 scale).
    // audioGainToDb(100/100) = 0.0; audioGainToDb(0/100) = -60.0.
    // From Thetis TCIServer.cs:4415-4417 variable naming conventions.
    const double rx1vol    = 0.0;   // audioGainToDb(100/100) = 0.0
    const double rx1Subvol = -60.0; // audioGainToDb(0/100) = -60.0
    const double rx2vol    = 0.0;   // audioGainToDb(100/100) = 0.0

    // Balance: GetBal(1/2, sub) = 0.0 → 40.0 - (0.0 * 0.8) = 40.0
    const double bal = 40.0;

    // AGC: MED → "normal". From Thetis TCIServer.cs:2206-2207 [v2.10.3.13].
    const QString agcMode = QStringLiteral("normal");
    const int agcGain = 40;  // Thetis default AGC threshold

    // CTUN off.
    const bool ctun = false;

    // TX profiles placeholder.
    const QStringList txProfiles = { QStringLiteral("Default") };
    const QString txProfile = QStringLiteral("Default");

    // Calibration: all 0.0 (F6) — defaults.
    const double calMeter   = 0.0;
    const double calDisplay = 0.0;
    const double calXvtr    = 0.0;
    const double cal6m      = 0.0;
    const double calTxDisp  = 0.0;

    // RIT / XIT defaults.
    const bool ritOn  = false;
    const bool xitOn  = false;
    const int ritVal  = 0;
    const int xitVal  = 0;

    // VFO locks.
    const bool vfoALock = false;
    const bool vfoBLock = false;

    // Squelch off.
    const bool sqlEn    = false;
    const int  sqlLevel = -150;

    // DIGL / DIGU click-tune offsets.
    const int diglOffset = 0;
    const int diguOffset = 0;

    // CW macro defaults.
    const int cwMacrosSpeed = 25;  // WPM
    const int cwMacrosDelay = 0;   // ms
    const int cwKeyerSpeed  = 25;  // WPM

    // Split off.
    const bool split = false;

    // IQ / audio stream defaults.
    const int iqSampleRate          = 192000;  // common HPSDR rate
    const int audioSampleRate       = 48000;   // default
    const QString audioSampleType   = QStringLiteral("float32");
    const int audioStreamChannels   = 2;       // stereo
    const int audioStreamSamples    = 2048;    // per design doc §10 default
    const int txStreamBufferingMs   = 50;      // per Thetis default

    // Mute / volume / MON defaults.
    // linearToDbVolume(AF=100): ((100-0)/(100-0))*(0-(-60))+(-60) = 0.0 dB.
    const bool globalMute = false;
    const bool rx0Mute    = false;
    const bool rx1Mute    = false;
    const double volumeDb = 0.0;   // linearToDbVolume(AF=100)
    const bool monEnable  = false;
    const double monVolDb = 0.0;   // linearToDbVolume(TXAF=100)
    const bool powerOn    = true;

    // From Thetis TCIServer.cs:2368-2383 [v2.10.3.13] — bSend gate.
    if (bSend) {
        // From Thetis TCIServer.cs:2370-2379 [v2.10.3.13]
        lines << buildDdsLine(0, rx1FreqHz);
        lines << buildDdsLine(1, rx2FreqHz);
        lines << buildIfLine(0, 0, 0);
        lines << buildIfLine(0, 1, 0);
        // NereusSDR divergence (design doc §7 row 1): Thetis TCIServer.cs:2374-2375
        // [v2.10.3.13] calls sendIF(1,1) TWICE (copy-paste bug). We emit the intended
        // (1,0)+(1,1) cross-product instead, matching the sendVFO enumeration below.
        lines << buildIfLine(1, 0, 0);
        lines << buildIfLine(1, 1, 0);
        lines << buildVfoLine(0, 0, rx1FreqHz);
        lines << buildVfoLine(0, 1, rx1FreqHz);
        lines << buildVfoLine(1, 0, rx2FreqHz);
        lines << buildVfoLine(1, 1, rx2FreqHz);

        //bespoke
        lines << buildTxFrequencyLine(txFreqHz);
        lines << buildTxFrequencyThetisLine(txFreqHz, txBand, bRX2Enabled, false);
    }

    // From Thetis TCIServer.cs:2385-2389 [v2.10.3.13]
    lines << buildModulationLine(0, modeUpper);
    lines << buildModulationLine(1, modeUpper);
    lines << buildRxFilterBandLine(0, filterLow, filterHigh);
    lines << buildRxFilterBandLine(1, filterLow, filterHigh);

    // From Thetis TCIServer.cs:2391-2392 [v2.10.3.13]
    lines << buildRxEnableLine(0, !mox);
    lines << buildRxEnableLine(1, bRX2Enabled && !mox);

    // From Thetis TCIServer.cs:2394-2403 [v2.10.3.13]
    lines << buildRxNrEnableLine(0, rx1nr > 0);
    lines << buildRxNrEnableLine(1, rx2nr > 0);
    lines << buildRxNrEnableExLine(0, rx1nr > 0, rx1nr);
    lines << buildRxNrEnableExLine(1, rx2nr > 0, rx2nr);
    lines << buildRxNbEnableLine(0, false);
    lines << buildRxNbEnableLine(1, false);
    lines << buildRxBinEnableLine(0, false);
    lines << buildRxBinEnableLine(1, false);

    // From Thetis TCIServer.cs:2405-2413 [v2.10.3.13]
    lines << buildRxAnfEnableLine(0, false);
    lines << buildRxAnfEnableLine(1, false);
    // Gate on !IsSetupFormNull; Phase 4 Task 4.2 always emits (no Setup form yet).
    lines << buildRxApfEnableLine(0, false);
    lines << buildRxApfEnableLine(1, false);
    lines << buildRxNfEnableLine(0, false);
    lines << buildRxNfEnableLine(1, false);

    // From Thetis TCIServer.cs:2415-2430 [v2.10.3.13]
    lines << buildRxVolumeLine(0, 0, rx1vol);
    lines << buildRxVolumeLine(0, 1, rx1Subvol);
    lines << buildRxVolumeLine(1, 0, rx2vol);
    lines << buildRxVolumeLine(1, 1, rx2vol);
    lines << buildRxBalanceLine(0, 0, bal);
    lines << buildRxBalanceLine(0, 1, bal);
    lines << buildRxBalanceLine(1, 0, bal);
    lines << buildRxBalanceLine(1, 1, bal);

    // From Thetis TCIServer.cs:2427-2433 [v2.10.3.13]
    lines << buildAgcModeLine(0, agcMode);
    lines << buildAgcModeLine(1, agcMode);
    lines << buildAgcGainLine(0, agcGain);
    lines << buildAgcGainLine(1, agcGain);
    lines << buildRxCtunExLine(0, ctun);
    lines << buildRxCtunExLine(1, ctun);

    // From Thetis TCIServer.cs:2435-2439 [v2.10.3.13]
    lines << buildTxProfilesExLine(txProfiles);
    lines << buildTxProfileExLine(txProfile);
    lines << buildCalibrationExLine(0, calMeter, calDisplay, calXvtr, cal6m, calTxDisp);
    lines << buildCalibrationExLine(1, calMeter, calDisplay, calXvtr, cal6m, calTxDisp);

    //lock
    //TODO rx channel enable
    //rit/xit

    // From Thetis TCIServer.cs:2445-2452 [v2.10.3.13]
    lines << buildRitEnableLine(0, ritOn);
    lines << buildRitEnableLine(1, ritOn);
    lines << buildXitEnableLine(0, xitOn);
    lines << buildXitEnableLine(1, xitOn);
    lines << buildRitOffsetLine(0, ritVal);
    lines << buildRitOffsetLine(1, ritVal);
    lines << buildXitOffsetLine(0, xitVal);
    lines << buildXitOffsetLine(1, xitVal);

    // From Thetis TCIServer.cs:2453-2458 [v2.10.3.13]
    lines << buildLockLine(0, vfoALock);
    if (bRX2Enabled) {
        lines << buildLockLine(1, vfoBLock);
    }
    lines.append(buildAllVfoLocksLines(bRX2Enabled, vfoALock, vfoBLock));

    // From Thetis TCIServer.cs:2459-2464 [v2.10.3.13]
    lines << buildSqlEnableLine(0, sqlEn);
    lines << buildSqlEnableLine(1, sqlEn);
    lines << buildSqlLevelLine(0, sqlLevel);
    lines << buildSqlLevelLine(1, sqlLevel);
    lines << buildDiglOffsetLine(diglOffset);
    lines << buildDiguOffsetLine(diguOffset);

    // From Thetis TCIServer.cs:2465-2470 [v2.10.3.13] — gated on m_server != null
    // (always true in our port; Phase 6+ reads from server config object).
    lines << buildCwMacrosSpeedLine(cwMacrosSpeed);
    lines << buildCwMacrosDelayLine(cwMacrosDelay);
    lines << buildCwKeyerSpeedLine(cwKeyerSpeed);

    // From Thetis TCIServer.cs:2472-2476 [v2.10.3.13]
    lines << buildSplitEnableLine(0, split);
    lines << buildSplitEnableLine(1, bRX2Enabled && split);
    lines << buildTxEnableLine(0, !mox);
    lines << buildTxEnableLine(1, bRX2Enabled && !mox);

    // From Thetis TCIServer.cs:2478-2481 [v2.10.3.13]
    lines << buildRxChannelEnableLine(0, 0, true);
    lines << buildRxChannelEnableLine(0, 1, false);  // GetSubRX(1) placeholder
    lines << buildRxChannelEnableLine(1, 0, bRX2Enabled);
    // no sub rx on rx2
    lines << buildRxChannelEnableLine(1, 1, false);

    // From Thetis TCIServer.cs:2483-2487 [v2.10.3.13]
    lines << buildTrxLine(0, mox && !(false && bRX2Enabled));
    lines << buildTrxLine(1, mox && (false && bRX2Enabled));
    lines << buildTuneLine(0, tune && !(false && bRX2Enabled));
    lines << buildTuneLine(1, tune && (false && bRX2Enabled));

    // From Thetis TCIServer.cs:2489-2497 [v2.10.3.13]
    lines << buildIqStopLine(0);
    lines << buildIqStopLine(1);
    lines << buildIqSampleRateLine(iqSampleRate);
    lines << buildAudioSampleRateLine(audioSampleRate);
    lines << buildAudioStreamSampleTypeLine(audioSampleType);
    lines << buildAudioStreamChannelsLine(audioStreamChannels);
    lines << buildAudioStreamSamplesLine(audioStreamSamples);
    lines << buildTxStreamAudioBufferingLine(txStreamBufferingMs);

    // From Thetis TCIServer.cs:2499-2505 [v2.10.3.13]
    lines << buildMuteLine(globalMute);
    lines << buildRxMuteLine(0, rx0Mute);
    lines << buildRxMuteLine(1, rx1Mute);
    lines << buildVolumeLine(volumeDb);
    lines << buildMonEnableLine(monEnable);
    lines << buildMonVolumeLine(monVolDb);

    // From Thetis TCIServer.cs:2507 [v2.10.3.13]
    // MW0LGE_22b moved here to replicate sun
    lines << buildStartStopLine(powerOn);

    return lines;
}

// ── Freq / IF / VFO helpers ─────────────────────────────────────────────────

// From Thetis TCIServer.cs:2334-2348 [v2.10.3.13] — sendDDS format string.
QString TciProtocol::buildDdsLine(int rx, qint64 hz)
{
    return QStringLiteral("dds:%1,%2;").arg(rx).arg(hz);
}

// From Thetis TCIServer.cs:2096-2120 [v2.10.3.13] — sendIF format string.
// MW0LGE [2.9.0.7] note we invert with - (the cw pitch shift subtraction is
// part of sendIF; Phase 4 Task 4.2 hardcodes offsetHz = 0 for all IF lines).
QString TciProtocol::buildIfLine(int rx, int chan, qint64 offsetHz)
{
    return QStringLiteral("if:%1,%2,%3;").arg(rx).arg(chan).arg(offsetHz);
}

// From Thetis TCIServer.cs:2061-2095 [v2.10.3.13] — sendVFO format string.
QString TciProtocol::buildVfoLine(int rx, int chan, qint64 hz)
{
    return QStringLiteral("vfo:%1,%2,%3;").arg(rx).arg(chan).arg(hz);
}

// From Thetis TCIServer.cs:2246-2259 [v2.10.3.13] — sendTXFrequencyChanged.
// Emits two frames: tx_frequency (lower-cased) and the bespoke tx_frequency_thetis.
// bespoke TCI command for anan to make life easier determining active TX frequency
// format is : tx_frequency_thetis:3700000,b80m,false,false;
// arg1 freq (long) / arg2 band b80m, b40m etc / arg3 rx2 enabled / arg4 tx on vfoB
QString TciProtocol::buildTxFrequencyLine(qint64 hz)
{
    return QStringLiteral("tx_frequency:%1;").arg(hz);
}

QString TciProtocol::buildTxFrequencyThetisLine(qint64 hz, const QString& band,
                                                 bool rx2en, bool txvfob)
{
    return QStringLiteral("tx_frequency_thetis:%1,%2,%3,%4;")
        .arg(hz)
        .arg(band)
        .arg(rx2en ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(txvfob ? QStringLiteral("true") : QStringLiteral("false"));
}

// ── Mode / filter helpers ───────────────────────────────────────────────────

// From Thetis TCIServer.cs:2136-2157 [v2.10.3.13] — sendMode format string.
// MW0LGE_22b mods are uppercase on the sun, replicate
QString TciProtocol::buildModulationLine(int rx, const QString& modeUpper)
{
    return QStringLiteral("modulation:%1,%2;").arg(rx).arg(modeUpper);
}

// From Thetis TCIServer.cs:2349-2353 [v2.10.3.13] — sendFilterBand.
// Adjacent sendDDS at TCIServer.cs:2344 carries: //MW0LGE [2.9.0.7]
QString TciProtocol::buildRxFilterBandLine(int rx, int lowHz, int highHz)
{
    return QStringLiteral("rx_filter_band:%1,%2,%3;").arg(rx).arg(lowHz).arg(highHz);
}

// ── RX-enable / NR / NB / BIN / ANF / APF / NF ────────────────────────────

// From Thetis TCIServer.cs:2279-2283 [v2.10.3.13] — sendRXEnable.
QString TciProtocol::buildRxEnableLine(int rx, bool en)
{
    return QStringLiteral("rx_enable:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:4472-4480 [v2.10.3.13] — sendNrEnable (non-extended form).
QString TciProtocol::buildRxNrEnableLine(int rx, bool en)
{
    return QStringLiteral("rx_nr_enable:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:4472-4480 [v2.10.3.13] — sendNrEnable (extended form).
QString TciProtocol::buildRxNrEnableExLine(int rx, bool en, int nrIndex)
{
    return QStringLiteral("rx_nr_enable_ex:%1,%2,%3;")
        .arg(rx)
        .arg(en ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(nrIndex);
}

// From Thetis TCIServer.cs:1901-1905 [v2.10.3.13] — sendRxNbEnable.
QString TciProtocol::buildRxNbEnableLine(int rx, bool en)
{
    return QStringLiteral("rx_nb_enable:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:1906-1910 [v2.10.3.13] — sendRxBinEnable.
QString TciProtocol::buildRxBinEnableLine(int rx, bool en)
{
    return QStringLiteral("rx_bin_enable:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:4482-4487 [v2.10.3.13] — sendAnfEnable.
QString TciProtocol::buildRxAnfEnableLine(int rx, bool en)
{
    return QStringLiteral("rx_anf_enable:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:1911-1915 [v2.10.3.13] — sendRxApfEnable.
QString TciProtocol::buildRxApfEnableLine(int rx, bool en)
{
    return QStringLiteral("rx_apf_enable:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:1916-1920 [v2.10.3.13] — sendRxNfEnable.
QString TciProtocol::buildRxNfEnableLine(int rx, bool en)
{
    return QStringLiteral("rx_nf_enable:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// ── Volume / balance helpers ────────────────────────────────────────────────

// From Thetis TCIServer.cs:4563-4568 [v2.10.3.13] — sendRxVolume (F2 C-locale).
QString TciProtocol::buildRxVolumeLine(int rx, int chan, double db)
{
    return QStringLiteral("rx_volume:%1,%2,%3;")
        .arg(rx)
        .arg(chan)
        .arg(QString::number(db, 'f', 2));
}

// From Thetis TCIServer.cs:2187-2191 [v2.10.3.13] — sendRxBalance (F2 C-locale).
QString TciProtocol::buildRxBalanceLine(int rx, int chan, double bal)
{
    return QStringLiteral("rx_balance:%1,%2,%3;")
        .arg(rx)
        .arg(chan)
        .arg(QString::number(bal, 'f', 2));
}

// ── AGC helpers ─────────────────────────────────────────────────────────────

// From Thetis TCIServer.cs:2236-2240 [v2.10.3.13] — sendAgcMode.
QString TciProtocol::buildAgcModeLine(int rx, const QString& mode)
{
    return QStringLiteral("agc_mode:%1,%2;").arg(rx).arg(mode);
}

// From Thetis TCIServer.cs:2241-2245 [v2.10.3.13] — sendAgcGain.
QString TciProtocol::buildAgcGainLine(int rx, int gain)
{
    return QStringLiteral("agc_gain:%1,%2;").arg(rx).arg(gain);
}

// ── CTUN / TX profile / calibration ────────────────────────────────────────

// From Thetis TCIServer.cs:4690-4694 [v2.10.3.13] — sendCTUN (rx_ctun_ex suffix).
QString TciProtocol::buildRxCtunExLine(int rx, bool en)
{
    return QStringLiteral("rx_ctun_ex:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:4721-4731 [v2.10.3.13] — sendTXProfiles.
// Profiles are comma-separated; sendTXProfiles returns early if IsSetupFormNull.
// Phase 4 Task 4.2 always emits with the placeholder list.
QString TciProtocol::buildTxProfilesExLine(const QStringList& names)
{
    return QStringLiteral("tx_profiles_ex:%1;").arg(names.join(QLatin1Char(',')));
}

// From Thetis TCIServer.cs:4715-4720 [v2.10.3.13] — sendTXProfile.
QString TciProtocol::buildTxProfileExLine(const QString& active)
{
    return QStringLiteral("tx_profile_ex:%1;").arg(active);
}

// From Thetis TCIServer.cs:4766-4775 [v2.10.3.13] — sendCalibration (F6 C-locale).
QString TciProtocol::buildCalibrationExLine(int rx, double meter, double display,
                                             double xvtr, double sixMeter,
                                             double txDisplay)
{
    return QStringLiteral("calibration_ex:%1,%2,%3,%4,%5,%6;")
        .arg(rx)
        .arg(QString::number(meter, 'f', 6))
        .arg(QString::number(display, 'f', 6))
        .arg(QString::number(xvtr, 'f', 6))
        .arg(QString::number(sixMeter, 'f', 6))
        .arg(QString::number(txDisplay, 'f', 6));
}

// ── RIT / XIT helpers ───────────────────────────────────────────────────────

// From Thetis TCIServer.cs:1881-1885 [v2.10.3.13] — sendRITEnable.
QString TciProtocol::buildRitEnableLine(int rx, bool en)
{
    return QStringLiteral("rit_enable:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:1886-1890 [v2.10.3.13] — sendXITEnable.
QString TciProtocol::buildXitEnableLine(int rx, bool en)
{
    return QStringLiteral("xit_enable:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:1891-1895 [v2.10.3.13] — sendRITOffset.
QString TciProtocol::buildRitOffsetLine(int rx, int hz)
{
    return QStringLiteral("rit_offset:%1,%2;").arg(rx).arg(hz);
}

// From Thetis TCIServer.cs:1896-1900 [v2.10.3.13] — sendXITOffset.
QString TciProtocol::buildXitOffsetLine(int rx, int hz)
{
    return QStringLiteral("xit_offset:%1,%2;").arg(rx).arg(hz);
}

// ── Lock helpers ────────────────────────────────────────────────────────────

// From Thetis TCIServer.cs:1921-1925 [v2.10.3.13] — sendLock.
QString TciProtocol::buildLockLine(int rx, bool locked)
{
    return QStringLiteral("lock:%1,%2;").arg(rx).arg(locked ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:2032-2049 [v2.10.3.13] — sendAllVFOLocks logic.
// Uses sendVFOLock (TCIServer.cs:1926-1930) for the individual vfo_lock lines.
// When rx2Enabled: emits vfo_lock:0,0; vfo_lock:1,0; vfo_lock:1,1 (3 lines).
// When !rx2Enabled: emits vfo_lock:0,0; vfo_lock:0,1 (2 lines).
QStringList TciProtocol::buildAllVfoLocksLines(bool rx2Enabled,
                                                bool vfoALock, bool vfoBLock)
{
    QStringList out;
    const QString trueStr  = QStringLiteral("true");
    const QString falseStr = QStringLiteral("false");
    if (rx2Enabled) {
        // tryGetVFOLockState(0,0) → VFOALock
        out << QStringLiteral("vfo_lock:0,0,%1;").arg(vfoALock ? trueStr : falseStr);
        // tryGetVFOLockState(1,0) → VFOBLock
        out << QStringLiteral("vfo_lock:1,0,%1;").arg(vfoBLock ? trueStr : falseStr);
        // tryGetVFOLockState(1,1) → VFOBLock
        out << QStringLiteral("vfo_lock:1,1,%1;").arg(vfoBLock ? trueStr : falseStr);
    } else {
        // tryGetVFOLockState(0,0) → VFOALock
        out << QStringLiteral("vfo_lock:0,0,%1;").arg(vfoALock ? trueStr : falseStr);
        // tryGetVFOLockState(0,1) → VFOBLock
        out << QStringLiteral("vfo_lock:0,1,%1;").arg(vfoBLock ? trueStr : falseStr);
    }
    return out;
}

// ── SQL / DIGL / DIGU helpers ────────────────────────────────────────────────

// From Thetis TCIServer.cs:1931-1935 [v2.10.3.13] — sendSqlEnable.
QString TciProtocol::buildSqlEnableLine(int rx, bool en)
{
    return QStringLiteral("sql_enable:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:1936-1940 [v2.10.3.13] — sendSqlLevel.
QString TciProtocol::buildSqlLevelLine(int rx, int level)
{
    return QStringLiteral("sql_level:%1,%2;").arg(rx).arg(level);
}

// From Thetis TCIServer.cs:2051-2055 [v2.10.3.13] — sendDiglOffset.
QString TciProtocol::buildDiglOffsetLine(int hz)
{
    return QStringLiteral("digl_offset:%1;").arg(hz);
}

// From Thetis TCIServer.cs:2056-2060 [v2.10.3.13] — sendDiguOffset.
QString TciProtocol::buildDiguOffsetLine(int hz)
{
    return QStringLiteral("digu_offset:%1;").arg(hz);
}

// ── CW macro helpers ─────────────────────────────────────────────────────────

// From Thetis TCIServer.cs:1941-1944 [v2.10.3.13] — sendCwMacrosSpeed.
QString TciProtocol::buildCwMacrosSpeedLine(int wpm)
{
    return QStringLiteral("cw_macros_speed:%1;").arg(wpm);
}

// From Thetis TCIServer.cs:1945-1948 [v2.10.3.13] — sendCwMacrosDelay.
QString TciProtocol::buildCwMacrosDelayLine(int ms)
{
    return QStringLiteral("cw_macros_delay:%1;").arg(ms);
}

// From Thetis TCIServer.cs:1949-1952 [v2.10.3.13] — sendCwKeyerSpeed.
QString TciProtocol::buildCwKeyerSpeedLine(int wpm)
{
    return QStringLiteral("cw_keyer_speed:%1;").arg(wpm);
}

// ── Split / TX-enable helpers ─────────────────────────────────────────────────

// From Thetis TCIServer.cs:1876-1880 [v2.10.3.13] — sendSplit.
QString TciProtocol::buildSplitEnableLine(int rx, bool en)
{
    return QStringLiteral("split_enable:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:2284-2288 [v2.10.3.13] — sendTXEnable.
QString TciProtocol::buildTxEnableLine(int rx, bool en)
{
    return QStringLiteral("tx_enable:%1,%2;").arg(rx).arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// ── RX channel enable ─────────────────────────────────────────────────────────

// From Thetis TCIServer.cs:5887-5890 [v2.10.3.13] — sendRxChannelEnable.
QString TciProtocol::buildRxChannelEnableLine(int rx, int chan, bool en)
{
    return QStringLiteral("rx_channel_enable:%1,%2,%3;")
        .arg(rx)
        .arg(chan)
        .arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// ── MOX / TUNE helpers ────────────────────────────────────────────────────────

// From Thetis TCIServer.cs:2121-2131 [v2.10.3.13] — sendMOX (no ",tci" suffix in
// init burst; signalTCI=false always for initial state lines).
// Adjacent sendIF at TCIServer.cs:2116 carries: //MW0LGE [2.9.0.7] note we invert with -
QString TciProtocol::buildTrxLine(int rx, bool mox)
{
    return QStringLiteral("trx:%1,%2;").arg(rx).arg(mox ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:2274-2278 [v2.10.3.13] — sendTune.
QString TciProtocol::buildTuneLine(int rx, bool tuning)
{
    return QStringLiteral("tune:%1,%2;").arg(rx).arg(tuning ? QStringLiteral("true") : QStringLiteral("false"));
}

// ── IQ helpers ────────────────────────────────────────────────────────────────

// From Thetis TCIServer.cs:5814-5817 [v2.10.3.13] — sendIQStartStop (iq_stop form).
// Init burst always sends iq_stop (enable=false per Thetis sendIQStartStop(0,false)).
QString TciProtocol::buildIqStopLine(int rx)
{
    return QStringLiteral("iq_stop:%1;").arg(rx);
}

// From Thetis TCIServer.cs:5364-5370 [v2.10.3.13] — sendIQSampleRate.
QString TciProtocol::buildIqSampleRateLine(int sr)
{
    return QStringLiteral("iq_samplerate:%1;").arg(sr);
}

// ── Audio-stream helpers ──────────────────────────────────────────────────────

// From Thetis TCIServer.cs:5372-5375 [v2.10.3.13] — sendAudioSampleRate.
QString TciProtocol::buildAudioSampleRateLine(int sr)
{
    return QStringLiteral("audio_samplerate:%1;").arg(sr);
}

// From Thetis TCIServer.cs:5377-5380 [v2.10.3.13] — sendAudioStreamSampleType.
QString TciProtocol::buildAudioStreamSampleTypeLine(const QString& typeLower)
{
    return QStringLiteral("audio_stream_sample_type:%1;").arg(typeLower);
}

// From Thetis TCIServer.cs:5382-5385 [v2.10.3.13] — sendAudioStreamChannels.
QString TciProtocol::buildAudioStreamChannelsLine(int n)
{
    return QStringLiteral("audio_stream_channels:%1;").arg(n);
}

// From Thetis TCIServer.cs:5387-5390 [v2.10.3.13] — sendAudioStreamSamples.
QString TciProtocol::buildAudioStreamSamplesLine(int n)
{
    return QStringLiteral("audio_stream_samples:%1;").arg(n);
}

// From Thetis TCIServer.cs:5392-5395 [v2.10.3.13] — sendTxStreamAudioBuffering.
QString TciProtocol::buildTxStreamAudioBufferingLine(int ms)
{
    return QStringLiteral("tx_stream_audio_buffering:%1;").arg(ms);
}

// ── Mute / volume / MON helpers ───────────────────────────────────────────────

// From Thetis TCIServer.cs:2158-2162 [v2.10.3.13] — sendMute.
QString TciProtocol::buildMuteLine(bool muted)
{
    return QStringLiteral("mute:%1;").arg(muted ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:2163-2167 [v2.10.3.13] — sendMuteRX.
QString TciProtocol::buildRxMuteLine(int rx, bool muted)
{
    return QStringLiteral("rx_mute:%1,%2;").arg(rx).arg(muted ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:2173-2179 [v2.10.3.13] — sendVolume.
// Clamped -60..0 dB; F1 C-locale. Thetis returns early if out of range;
// Phase 4 Task 4.2 hardcodes 0.0 dB which is within range.
QString TciProtocol::buildVolumeLine(double db)
{
    if (db < -60.0) { db = -60.0; }
    if (db >   0.0) { db =   0.0; }
    return QStringLiteral("volume:%1;").arg(QString::number(db, 'f', 1));
}

// From Thetis TCIServer.cs:2168-2172 [v2.10.3.13] — sendMONEnable.
QString TciProtocol::buildMonEnableLine(bool en)
{
    return QStringLiteral("mon_enable:%1;").arg(en ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:2180-2186 [v2.10.3.13] — sendMONVolume.
// Clamped -60..0 dB; F1 C-locale.
QString TciProtocol::buildMonVolumeLine(double db)
{
    if (db < -60.0) { db = -60.0; }
    if (db >   0.0) { db =   0.0; }
    return QStringLiteral("mon_volume:%1;").arg(QString::number(db, 'f', 1));
}

// ── Start / stop ──────────────────────────────────────────────────────────────

// From Thetis TCIServer.cs:1868-1875 / 2354-2360 [v2.10.3.13] — sendStartStop.
QString TciProtocol::buildStartStopLine(bool powerOn)
{
    return powerOn ? QStringLiteral("start;") : QStringLiteral("stop;");
}

// NereusSDR architectural divergence — Slice A/B/C/D maps to trx:N wire format.
// Identity through Phase 3; same mapping in subsequent phases (design doc §1.2).
int TciProtocol::sliceToTrx(int slice) { return slice; }
int TciProtocol::trxToSlice(int trx) { return trx; }

void TciProtocol::resetDispatchCounters()
{
    m_setDispatchCount = 0;
    m_queryDispatchCount = 0;
}

// From Thetis TCIServer.cs:4924-5128 [v2.10.3.13] — 60-case set-command switch.
// Phase 6 adds vfo, vfo_lock, lock cases; remaining cases land Phase 7+.
QString TciProtocol::handleSetCommand(const QString& name, const QStringList& args)
{
    ++m_setDispatchCount;

    // From Thetis TCIServer.cs:4929-4931 [v2.10.3.13] — vfo case (set switch).
    // handleVFOMessage at TCIServer.cs:3724 [v2.10.3.13] dispatches by args.Length:
    //   args.Length == 3 → set; args.Length == 2 → query (unicast response).
    if (name == QStringLiteral("vfo")) {
        return handleVfoCommand(args);
    }

    // From Thetis TCIServer.cs:4962-4964 [v2.10.3.13] — lock case (set switch).
    // 2-arg form drops channel per sendLock at TCIServer.cs:1921-1925 [v2.10.3.13].
    if (name == QStringLiteral("lock")) {
        return handleLockCommand(args);
    }

    // From Thetis TCIServer.cs:4965-4967 [v2.10.3.13] — vfo_lock case (set switch).
    if (name == QStringLiteral("vfo_lock")) {
        return handleVfoLockCommand(args);
    }

    // Phase 7: mode/filter family.
    if (name == QStringLiteral("modulation"))     { return handleModulationCommand(args); }
    if (name == QStringLiteral("rx_filter_band")) { return handleRxFilterBandCommand(args); }

    // Phase 8+ adds more cases.
    return {};
}

// From Thetis TCIServer.cs:5134-5197 [v2.10.3.13] — 21-case query-command switch.
// Phase 6: vfo, lock, vfo_lock have no 1-arg query cases in Thetis query switch;
// they self-dispatch (set vs. query) by args.size() inside handleSetCommand.
// Phase 7+ adds remaining query cases.
QString TciProtocol::handleQueryCommand(const QString& name)
{
    (void)name;
    ++m_queryDispatchCount;
    return {};
}

// ── VFO family handlers (Phase 6) ───────────────────────────────────────────

// From Thetis TCIServer.cs:3724-3833 [v2.10.3.13] — handleVFOMessage.
// 3-arg path: set VFO frequency; enqueue broadcast notification.
// 2-arg path: query VFO frequency; return as direct response (Phase 6 stub —
//   Thetis routes through VFOChange → sendTextFrame broadcast; Phase 14 adds
//   priority-queue coalescing; for Phase 6 we return the value directly).
// UseRX1VFOaForRX2VFOa quirk (TCIServer.cs:3732 [v2.10.3.13]) deferred to
// Phase 6+ refinement (compat placeholder row notes this).
QString TciProtocol::handleVfoCommand(const QStringList& args)
{
    if (args.size() < 2) {
        return {};
    }
    bool ok1 = false;
    bool ok2 = false;
    const int rx = args.at(0).trimmed().toInt(&ok1);
    const int chan = args.at(1).trimmed().toInt(&ok2);
    if (!ok1 || !ok2) {
        return {};
    }

    if (args.size() >= 3) {
        // 3-arg set path.
        // From Thetis TCIServer.cs:3746-3793 [v2.10.3.13] — set VFOAFreq/VFOBFreq.
        bool ok3 = false;
        const qint64 hz = args.at(2).trimmed().toLongLong(&ok3);
        if (!ok3) {
            return {};
        }
        // Write to mock via QMetaObject::invokeMethod (DirectConnection — test thread).
        // Production RadioModel exposes setVfoHz as Q_INVOKABLE too (Phase 17+).
        QMetaObject::invokeMethod(m_radio, "setVfoHz",
                                  Qt::DirectConnection,
                                  Q_ARG(int, rx),
                                  Q_ARG(int, chan),
                                  Q_ARG(qint64, hz));
        // Broadcast notification — Phase 14 adds priority-queue coalescing.
        // From Thetis sendVFO at TCIServer.cs:2061-2093 [v2.10.3.13] — format string.
        m_pendingNotifications << QStringLiteral("vfo:%1,%2,%3;").arg(rx).arg(chan).arg(hz);
        return {};
    }

    // 2-arg query path.
    // From Thetis TCIServer.cs:3793-3833 [v2.10.3.13] — reads VFOAFreq/VFOBFreq.
    qint64 hz = 0;
    QMetaObject::invokeMethod(m_radio, "vfoHz",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(qint64, hz),
                              Q_ARG(int, rx),
                              Q_ARG(int, chan));
    // From Thetis sendVFO at TCIServer.cs:2093 [v2.10.3.13] — format string.
    return QStringLiteral("vfo:%1,%2,%3;").arg(rx).arg(chan).arg(hz);
}

// From Thetis TCIServer.cs:3284-3302 [v2.10.3.13] — handleVFOLock.
// 3-arg path: set vfo_lock state; enqueue broadcast notification.
// 2-arg path: query vfo_lock state; return as direct response.
QString TciProtocol::handleVfoLockCommand(const QStringList& args)
{
    if (args.size() < 2) {
        return {};
    }
    bool ok1 = false;
    bool ok2 = false;
    const int rx = args.at(0).trimmed().toInt(&ok1);
    const int chan = args.at(1).trimmed().toInt(&ok2);
    if (!ok1 || !ok2) {
        return {};
    }

    if (args.size() >= 3) {
        // 3-arg set path.
        // From Thetis TCIServer.cs:3298 [v2.10.3.13] — trySetVFOLockState.
        const QString boolStr = args.at(2).trimmed().toLower();
        if (boolStr != QStringLiteral("true") && boolStr != QStringLiteral("false")) {
            return {};
        }
        const bool locked = (boolStr == QStringLiteral("true"));
        QMetaObject::invokeMethod(m_radio, "setVfoLock",
                                  Qt::DirectConnection,
                                  Q_ARG(int, rx),
                                  Q_ARG(int, chan),
                                  Q_ARG(bool, locked));
        // From Thetis sendVFOLock at TCIServer.cs:1926-1930 [v2.10.3.13] — format.
        m_pendingNotifications << QStringLiteral("vfo_lock:%1,%2,%3;")
                                      .arg(rx).arg(chan)
                                      .arg(locked ? QStringLiteral("true") : QStringLiteral("false"));
        return {};
    }

    // 2-arg query path.
    // From Thetis TCIServer.cs:3292-3293 [v2.10.3.13] — tryGetVFOLockState + sendVFOLock.
    bool locked = false;
    QMetaObject::invokeMethod(m_radio, "vfoLock",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(bool, locked),
                              Q_ARG(int, rx),
                              Q_ARG(int, chan));
    // From Thetis sendVFOLock at TCIServer.cs:1926-1930 [v2.10.3.13] — format.
    return QStringLiteral("vfo_lock:%1,%2,%3;")
        .arg(rx).arg(chan)
        .arg(locked ? QStringLiteral("true") : QStringLiteral("false"));
}

// From Thetis TCIServer.cs:3265-3283 [v2.10.3.13] — handleLock.
// 2-arg path: set lock state; enqueue broadcast notification.
// 1-arg path: query lock state; return as direct response.
// Note: drops channel arg — sendLock at TCIServer.cs:1921-1925 [v2.10.3.13]
// emits lock:rx,bool; (no channel).
QString TciProtocol::handleLockCommand(const QStringList& args)
{
    if (args.size() < 1) {
        return {};
    }
    bool ok1 = false;
    const int rx = args.at(0).trimmed().toInt(&ok1);
    if (!ok1) {
        return {};
    }

    if (args.size() >= 2) {
        // 2-arg set path.
        // From Thetis TCIServer.cs:3277-3282 [v2.10.3.13] — VFOALock/VFOBLock write.
        const QString boolStr = args.at(1).trimmed().toLower();
        if (boolStr != QStringLiteral("true") && boolStr != QStringLiteral("false")) {
            return {};
        }
        const bool locked = (boolStr == QStringLiteral("true"));
        QMetaObject::invokeMethod(m_radio, "setLock",
                                  Qt::DirectConnection,
                                  Q_ARG(int, rx),
                                  Q_ARG(bool, locked));
        // From Thetis sendLock at TCIServer.cs:1921-1925 [v2.10.3.13] — format.
        m_pendingNotifications << QStringLiteral("lock:%1,%2;")
                                      .arg(rx)
                                      .arg(locked ? QStringLiteral("true") : QStringLiteral("false"));
        return {};
    }

    // 1-arg query path.
    // From Thetis TCIServer.cs:3271-3273 [v2.10.3.13] — sendLock(rx, VFOALock/VFOBLock).
    bool locked = false;
    QMetaObject::invokeMethod(m_radio, "lock",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(bool, locked),
                              Q_ARG(int, rx));
    // From Thetis sendLock at TCIServer.cs:1921-1925 [v2.10.3.13] — format.
    return QStringLiteral("lock:%1,%2;")
        .arg(rx)
        .arg(locked ? QStringLiteral("true") : QStringLiteral("false"));
}

// ── Mode / filter family handlers (Phase 7) ─────────────────────────────────

// From Thetis TCIServer.cs:4926 [v2.10.3.13] — modulation case in set switch.
// handleModulationMessage at TCIServer.cs:3837-3940 [v2.10.3.13] dispatches by args.size():
//   args.size() == 2 → set (parse mode string, set DSPMode + enqueue notification)
//   args.size() == 1 → query (return current mode uppercase)
// CWLUbecomesCW transform at TCIServer.cs:2148-2153 [v2.10.3.13] is DEFERRED
//   (send-side, Phase 11/12 follow-up — wiring requires touching buildModulationLine).
// CWbecomesCWUabove10mhz transform at TCIServer.cs:3868-3895 [v2.10.3.13] is DEFERRED
//   (needs VFOATX/VFOBTX accessors on TestMockRadioModel — future phase).
QString TciProtocol::handleModulationCommand(const QStringList& args)
{
    if (args.size() < 1) {
        return {};
    }
    bool ok = false;
    const int rx = args.at(0).trimmed().toInt(&ok);
    if (!ok) {
        return {};
    }

    if (args.size() == 1) {
        // Query path — return current mode uppercase.
        // From Thetis TCIServer.cs:3933-3940 [v2.10.3.13] — query dispatch.
        QString currentMode;
        QMetaObject::invokeMethod(m_radio, "mode", Qt::DirectConnection,
                                  Q_RETURN_ARG(QString, currentMode),
                                  Q_ARG(int, rx));
        return QStringLiteral("modulation:%1,%2;").arg(rx).arg(currentMode.toUpper());
    }

    if (args.size() >= 2) {
        // Set path — canonicalize mode string.
        // From Thetis TCIServer.cs:3847-3911 [v2.10.3.13] — switch on args[1].ToLower().
        //MW0LGE change if needed [2.10.3.6]MW0LGE fixes #365  [original inline comment from TCIServer.cs:3869]
        // Note: nfm and fm are aliases for DSPMode.FM (TCIServer.cs:3865-3866).
        // Unknown modes map to DSPMode.FIRST, which is treated as a no-op
        // (TCIServer.cs:3909-3913 [v2.10.3.13] — silent drop).
        const QString modeIn = args.at(1).trimmed().toLower();
        QString modeOut;
        if      (modeIn == QStringLiteral("lsb"))  { modeOut = QStringLiteral("LSB"); }
        else if (modeIn == QStringLiteral("usb"))  { modeOut = QStringLiteral("USB"); }
        else if (modeIn == QStringLiteral("dsb"))  { modeOut = QStringLiteral("DSB"); }
        else if (modeIn == QStringLiteral("am"))   { modeOut = QStringLiteral("AM"); }
        else if (modeIn == QStringLiteral("sam"))  { modeOut = QStringLiteral("SAM"); }
        else if (modeIn == QStringLiteral("nfm") || modeIn == QStringLiteral("fm")) {
            // nfm and fm both alias to DSPMode.FM per TCIServer.cs:3865-3866 [v2.10.3.13].
            modeOut = QStringLiteral("FM");
        }
        else if (modeIn == QStringLiteral("cw")) {
            // CWbecomesCWUabove10mhz transform at TCIServer.cs:3868-3895 [v2.10.3.13] is
            // DEFERRED — `cw` → CWL until VFOATX/VFOBTX state arrives.
            modeOut = QStringLiteral("CWL");
        }
        else if (modeIn == QStringLiteral("cwl"))  { modeOut = QStringLiteral("CWL"); }
        else if (modeIn == QStringLiteral("cwu"))  { modeOut = QStringLiteral("CWU"); }
        else if (modeIn == QStringLiteral("digl")) { modeOut = QStringLiteral("DIGL"); }
        else if (modeIn == QStringLiteral("digu")) { modeOut = QStringLiteral("DIGU"); }
        else {
            // Unknown mode — silent drop per Thetis DSPMode.FIRST default
            // at TCIServer.cs:3909-3913 [v2.10.3.13].
            return {};
        }

        QMetaObject::invokeMethod(m_radio, "setMode", Qt::DirectConnection,
                                  Q_ARG(int, rx), Q_ARG(QString, modeOut));
        // MW0LGE_22b mods are uppcase on the sun, replicate
        // From Thetis TCIServer.cs:2155 [v2.10.3.13] — sendMode format string.
        m_pendingNotifications << QStringLiteral("modulation:%1,%2;").arg(rx).arg(modeOut);
        return {};
    }

    return {};
}

// From Thetis TCIServer.cs:5085 [v2.10.3.13] — rx_filter_band case in set switch.
// handleRxFilterBand at TCIServer.cs:4366-4413 [v2.10.3.13] dispatches by args.size():
//   args.size() == 3 → set (rx, low, high — all int)
//     calls UpdateRX1Filters/UpdateRX2Filters at TCIServer.cs:4393-4399 [v2.10.3.13].
//   args.size() == 1 → query (rx)
//     reads RX1FilterLow/High or RX2FilterLow/High at TCIServer.cs:4380-4384 [v2.10.3.13].
QString TciProtocol::handleRxFilterBandCommand(const QStringList& args)
{
    if (args.size() < 1) {
        return {};
    }
    bool ok = false;
    const int rx = args.at(0).trimmed().toInt(&ok);
    if (!ok) {
        return {};
    }

    if (args.size() == 1) {
        // Query path.
        // From Thetis TCIServer.cs:4380-4384 [v2.10.3.13] — RX1FilterLow/High read.
        int low = 0, high = 0;
        QMetaObject::invokeMethod(m_radio, "filterLow", Qt::DirectConnection,
                                  Q_RETURN_ARG(int, low), Q_ARG(int, rx));
        QMetaObject::invokeMethod(m_radio, "filterHigh", Qt::DirectConnection,
                                  Q_RETURN_ARG(int, high), Q_ARG(int, rx));
        return QStringLiteral("rx_filter_band:%1,%2,%3;").arg(rx).arg(low).arg(high);
    }

    if (args.size() >= 3) {
        // Set path.
        // From Thetis TCIServer.cs:4393-4399 [v2.10.3.13] — UpdateRX1Filters/UpdateRX2Filters.
        bool ok2 = false, ok3 = false;
        const int low  = args.at(1).trimmed().toInt(&ok2);
        const int high = args.at(2).trimmed().toInt(&ok3);
        if (!ok2 || !ok3) {
            return {};
        }
        QMetaObject::invokeMethod(m_radio, "setFilterBand", Qt::DirectConnection,
                                  Q_ARG(int, rx), Q_ARG(int, low), Q_ARG(int, high));
        // From Thetis sendFilterBand at TCIServer.cs:2349-2353 [v2.10.3.13] — format string.
        m_pendingNotifications << QStringLiteral("rx_filter_band:%1,%2,%3;")
                                      .arg(rx).arg(low).arg(high);
        return {};
    }

    return {};
}

} // namespace NereusSDR
