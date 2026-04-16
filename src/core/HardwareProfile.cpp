// src/core/HardwareProfile.cpp
//
// Source: mi0bot/Thetis clsHardwareSpecific.cs:85-184, NetworkIO.cs:164-171

#include "HardwareProfile.h"

namespace NereusSDR {

// From Thetis clsHardwareSpecific.cs:85-184
HardwareProfile profileForModel(HPSDRModel model)
{
    HardwareProfile p;
    p.model = model;

    switch (model) {
        case HPSDRModel::HERMES:
            p.effectiveBoard    = HPSDRHW::Hermes;
            p.adcCount          = 1;
            p.mkiiBpf           = false;
            p.adcSupplyVoltage  = 33;
            p.lrAudioSwap       = true;
            break;
        case HPSDRModel::ANAN10:
            p.effectiveBoard    = HPSDRHW::Hermes;
            p.adcCount          = 1;
            p.mkiiBpf           = false;
            p.adcSupplyVoltage  = 33;
            p.lrAudioSwap       = true;
            break;
        case HPSDRModel::ANAN10E:
            p.effectiveBoard    = HPSDRHW::HermesII;
            p.adcCount          = 1;
            p.mkiiBpf           = false;
            p.adcSupplyVoltage  = 33;
            p.lrAudioSwap       = true;
            break;
        case HPSDRModel::ANAN100:
            p.effectiveBoard    = HPSDRHW::Hermes;
            p.adcCount          = 1;
            p.mkiiBpf           = false;
            p.adcSupplyVoltage  = 33;
            p.lrAudioSwap       = true;
            break;
        case HPSDRModel::ANAN100B:
            p.effectiveBoard    = HPSDRHW::HermesII;
            p.adcCount          = 1;
            p.mkiiBpf           = false;
            p.adcSupplyVoltage  = 33;
            p.lrAudioSwap       = true;
            break;
        case HPSDRModel::ANAN100D:
            p.effectiveBoard    = HPSDRHW::Angelia;
            p.adcCount          = 2;
            p.mkiiBpf           = false;
            p.adcSupplyVoltage  = 33;
            p.lrAudioSwap       = false;
            break;
        case HPSDRModel::ANAN200D:
            p.effectiveBoard    = HPSDRHW::Orion;
            p.adcCount          = 2;
            p.mkiiBpf           = false;
            p.adcSupplyVoltage  = 50;
            p.lrAudioSwap       = false;
            break;
        case HPSDRModel::ORIONMKII:
            p.effectiveBoard    = HPSDRHW::OrionMKII;
            p.adcCount          = 2;
            p.mkiiBpf           = true;
            p.adcSupplyVoltage  = 50;
            p.lrAudioSwap       = false;
            break;
        case HPSDRModel::ANAN7000D:
            p.effectiveBoard    = HPSDRHW::OrionMKII;
            p.adcCount          = 2;
            p.mkiiBpf           = true;
            p.adcSupplyVoltage  = 50;
            p.lrAudioSwap       = false;
            break;
        case HPSDRModel::ANAN8000D:
            p.effectiveBoard    = HPSDRHW::OrionMKII;
            p.adcCount          = 2;
            p.mkiiBpf           = true;
            p.adcSupplyVoltage  = 50;
            p.lrAudioSwap       = false;
            break;
        case HPSDRModel::ANAN_G2:
            p.effectiveBoard    = HPSDRHW::Saturn;
            p.adcCount          = 2;
            p.mkiiBpf           = true;
            p.adcSupplyVoltage  = 50;
            p.lrAudioSwap       = false;
            break;
        case HPSDRModel::ANAN_G2_1K:
            p.effectiveBoard    = HPSDRHW::Saturn;
            p.adcCount          = 2;
            p.mkiiBpf           = true;
            p.adcSupplyVoltage  = 50;
            p.lrAudioSwap       = false;
            break;
        case HPSDRModel::ANVELINAPRO3:
            p.effectiveBoard    = HPSDRHW::OrionMKII;
            p.adcCount          = 2;
            p.mkiiBpf           = true;
            p.adcSupplyVoltage  = 50;
            p.lrAudioSwap       = false;
            break;
        case HPSDRModel::HERMESLITE:
            p.effectiveBoard    = HPSDRHW::HermesLite;
            p.adcCount          = 1;
            p.mkiiBpf           = false;
            p.adcSupplyVoltage  = 33;
            p.lrAudioSwap       = true;
            break;
        case HPSDRModel::REDPITAYA:
            p.effectiveBoard    = HPSDRHW::OrionMKII;
            p.adcCount          = 2;
            p.mkiiBpf           = false;
            p.adcSupplyVoltage  = 50;
            p.lrAudioSwap       = false;
            break;
        case HPSDRModel::HPSDR:
            p.effectiveBoard    = HPSDRHW::Atlas;
            p.adcCount          = 1;
            p.mkiiBpf           = false;
            p.adcSupplyVoltage  = 33;
            p.lrAudioSwap       = true;
            break;
        case HPSDRModel::FIRST:
        case HPSDRModel::LAST:
            break;
    }

    p.caps = &BoardCapsTable::forBoard(p.effectiveBoard);
    return p;
}

HPSDRModel defaultModelForBoard(HPSDRHW board)
{
    // Walk HPSDRModel enum, return first match.
    for (int i = static_cast<int>(HPSDRModel::FIRST) + 1;
         i < static_cast<int>(HPSDRModel::LAST); ++i) {
        auto m = static_cast<HPSDRModel>(i);
        if (boardForModel(m) == board) {
            return m;
        }
    }
    return HPSDRModel::HERMES;
}

// From Thetis NetworkIO.cs:164-171
QList<HPSDRModel> compatibleModels(HPSDRHW board)
{
    QList<HPSDRModel> result;

    for (int i = static_cast<int>(HPSDRModel::FIRST) + 1;
         i < static_cast<int>(HPSDRModel::LAST); ++i) {
        auto m = static_cast<HPSDRModel>(i);

        // Primary match: model's native board matches discovered board.
        if (boardForModel(m) == board) {
            result.append(m);
            continue;
        }

        // Special cross-board compatibility cases.
        // From Thetis NetworkIO.cs:164-171
        switch (m) {
            case HPSDRModel::REDPITAYA:
                // RedPitaya is compatible with Hermes (0x01) or OrionMKII (0x05)
                if (board == HPSDRHW::Hermes || board == HPSDRHW::OrionMKII) {
                    result.append(m);
                }
                break;
            case HPSDRModel::ANAN10:
            case HPSDRModel::ANAN100:
                // ANAN-10/100 are compatible with Hermes or HermesII
                if (board == HPSDRHW::Hermes || board == HPSDRHW::HermesII) {
                    result.append(m);
                }
                break;
            case HPSDRModel::ANAN10E:
            case HPSDRModel::ANAN100B:
                // ANAN-10E/100B are compatible with Hermes or HermesII
                if (board == HPSDRHW::Hermes || board == HPSDRHW::HermesII) {
                    result.append(m);
                }
                break;
            default:
                break;
        }
    }

    return result;
}

} // namespace NereusSDR
