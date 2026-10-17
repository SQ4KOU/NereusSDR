// =================================================================
// tests/fakes/FakeStationLink.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test fake; no upstream logic is ported.
//
// An IStationLink standing in for a remote window's link to a Core that
// runs a station TCI server (stationTciVersion 2, stationTciSettingsVersion
// 1). The public flags set what the link reports. Each request is recorded
// and answered as sent, with command ids from 101, 201 and 301. Nothing is
// sent anywhere.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  Written in tst_station_tci_server.cpp.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-15  J.J. Boyd / KG4VCF  Moved here unchanged so the visual
//                                    preview's TCI Server scene can use it.
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include "core/session/IStationLink.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariant>

#include <utility>

namespace NereusSDR::Test {

class FakeStationLink final : public IStationLink {
public:
    bool tciAvailable{true};
    bool coreHere{false};
    bool ready{true};
    bool serverVersion2{true};
    int stored{1};   // the Core has a stored station switch (-1: not known yet)
    int requests{0};
    bool requestedOn{false};
    quint16 requestedPort{0};
    int optionRequests{0};
    bool requestedExpert{true};
    CommandOutcome requestAddSlice(const QString&) override { return {}; }
    CommandOutcome requestAddSliceOnPan(const QString&) override { return {}; }
    CommandOutcome requestRemoveSlice(int) override { return {}; }
    CommandOutcome requestActiveSlice(int) override { return {}; }
    CommandOutcome requestSliceSampleRate(int, int) override { return {}; }
    bool stationLinkReady() const override { return ready; }
    bool stationTciAvailable() const override { return ready && tciAvailable; }
    bool stationTciServerAvailable() const override
    { return stationTciAvailable() && serverVersion2; }
    bool coreServesTciOnThisComputer() const override { return coreHere; }
    int coreStationTciStored() const override { return stored; }
    CommandOutcome requestStationTci(bool on, quint16 port) override
    {
        ++requests;
        requestedOn = on;
        requestedPort = port;
        return {true, {}, quint32(100 + requests)};   // command ids 101, 102, ...
    }
    CommandOutcome requestStationTciOptions(bool expert, bool, bool, bool) override
    {
        ++optionRequests;
        requestedExpert = expert;
        return {true, {}, quint32(200 + optionRequests)};
    }
    // JJ's ruling of 2026-09-28 (stationTciSettingsVersion 1).
    bool settingsAvailable{true};
    QList<std::pair<QByteArray, QVariant>> settingRequests;
    bool stationTciSettingsAvailable() const override
    { return stationTciServerAvailable() && settingsAvailable; }
    CommandOutcome requestStationTciSetting(const QByteArray& name, const QVariant& value) override
    {
        settingRequests.append({name, value});
        return {true, {}, quint32(300 + settingRequests.size())};
    }
};

} // namespace NereusSDR::Test
