// src/gui/applets/AccessoryReadouts.h (NereusSDR)
//
// The readouts on the tuner and amplifier panels (Tuner Genius XL, Power
// Genius XL, RF2K-S): forward power as one large bar with its scale, limit
// mark, held peak and a large number; reflected power, SWR and temperature
// as slim rows with the number on the face. One place for the four scales,
// the text rules and the two bar forms, so the three panels read alike.
//
// no-port-check: NereusSDR-original file. The bars are BarInstrument (a
// Longpath port, attributed in its own header); nothing here is ported.
// What the values rest on:
//   - The 0 to 2000 W forward scale with its limit at 1500 W, the 1 to 3
//     SWR scale with its limit at 2.5 and the 0 to 100 degree scale with
//     its limit at 80 are the ranges and red starts the panels' gauges had
//     (AetherSDR src/gui/AmpApplet.cpp:18-34 [@0cd4559], cited where the
//     panels build their bars). The yellow band those gauges also had is
//     not carried over: one limit per reading.
//   - The 0 to 500 W reflected scale, whole watts and whole degrees, and
//     the three SWR text forms are JJ's (KG4VCF) rulings of 2026-10-12.
//   - reflectedWatts() is the definition of SWR, no source beyond it.
//
// Modification history (NereusSDR):
//   2026-10-12 - Created for the tuner and amplifier panels.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#pragma once

#include "gui/instruments/ReadingSource.h"

#include <QString>

class QWidget;

namespace NereusSDR {

class BarInstrument;

namespace AccessoryReadouts {

// The row names. The look's caps font draws them in capitals.
QString forwardName();     // "Forward"
QString reflectedName();   // "Reflected"
QString swrName();         // "SWR"
QString temperatureName(); // "Temp"

// A slim row: 30 px tall, its number at 13 px.
constexpr int kSlimRowHeight = 30;
constexpr int kSlimValuePx = 13;

// SWR at or above this is the devices' open or short cap: all of the
// forward power comes back.
constexpr double kSwrCap = 99.0;

// Forward power, 0 to `maxWatts` with the limit at `limitWatts`: labeled
// ticks at every quarter of the scale and plain ones between them, whole
// watts ("1236 W"). The amplifier scale is the default: ticks 0, 500,
// 1000, 1.5k, 2k. A tick above 1000 W is written in kilowatts.
ReadingDescriptor forwardPower(double maxWatts = 2000.0, double limitWatts = 1500.0);

// Reflected power, 0 to 500 W, no limit, whole watts.
ReadingDescriptor reflectedPower();

// SWR, 1 to 3 with the limit at 2.5, text by swrText().
ReadingDescriptor swr();

// Temperature, 0 to 100 with the limit at 80, whole degrees ("41 °C").
ReadingDescriptor temperature();

// SWR as the rows write it, with no unit: two decimals below 9.995
// ("1.12"), one below 99.95 ("12.4"), ">99" from there.
QString swrText(double swr);

// Reflected power from forward power and SWR, for a device that reports
// only those two: g = (swr - 1) / (swr + 1), reflected = forward * g * g.
// SWR at or above kSwrCap gives all of the forward power; SWR at or below
// 1 gives none.
double reflectedWatts(double forwardWatts, double swr);

// The large bar: the full instrument form, its footer without the peak
// and limit text (it does not fit at panel width), at the fixed height
// that form asks for. No value yet: the instrument shows its dashes.
BarInstrument* makeLargeBar(const ReadingDescriptor& d, const QString& name,
                            QWidget* parent);

// A slim row: the compact form, kSlimRowHeight tall with its number at
// kSlimValuePx. No value yet.
BarInstrument* makeSlimBar(const ReadingDescriptor& d, const QString& name,
                           QWidget* parent);

} // namespace AccessoryReadouts
} // namespace NereusSDR
