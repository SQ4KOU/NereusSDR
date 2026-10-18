// no-port-check: NereusSDR-original. How a station accessory controller
// takes the device that answers at its address. J.J. Boyd (KG4VCF),
// October 2026; AI-assisted via Anthropic Claude Code.
#pragma once

namespace NereusSDR {

// The station controllers (StationTgxlController, StationPgxlController,
// StationRfKitController) own one accessory link each, for every window
// and client of the Core that runs them.
//
// Identified: nereusd. The device proves what it is before anything is
// sent to it (R-R3-47), because its address is typed from somewhere else.
//
// TypedAddress: a desktop window running its own radio, which shares its
// accessories with the windows and phones signed in to it. The link is
// dialed and used exactly as that window used it before it shared them:
// whatever answers at the address typed at the station is the device, and
// nothing new is sent on the wire.
enum class StationAccessoryAdmission { Identified, TypedAddress };

} // namespace NereusSDR
