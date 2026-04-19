#include "TransmitModel.h"
#include "core/AppSettings.h"

namespace NereusSDR {

QString vaxSlotToString(VaxSlot s)
{
    switch (s) {
        case VaxSlot::None:      return QStringLiteral("None");
        case VaxSlot::MicDirect: return QStringLiteral("MicDirect");
        case VaxSlot::Vax1:      return QStringLiteral("Vax1");
        case VaxSlot::Vax2:      return QStringLiteral("Vax2");
        case VaxSlot::Vax3:      return QStringLiteral("Vax3");
        case VaxSlot::Vax4:      return QStringLiteral("Vax4");
    }
    return QStringLiteral("MicDirect");
}

VaxSlot vaxSlotFromString(const QString& s)
{
    if (s == QLatin1String("None"))      { return VaxSlot::None; }
    if (s == QLatin1String("Vax1"))      { return VaxSlot::Vax1; }
    if (s == QLatin1String("Vax2"))      { return VaxSlot::Vax2; }
    if (s == QLatin1String("Vax3"))      { return VaxSlot::Vax3; }
    if (s == QLatin1String("Vax4"))      { return VaxSlot::Vax4; }
    if (s == QLatin1String("MicDirect")) { return VaxSlot::MicDirect; }
    return VaxSlot::MicDirect;  // unknown-string fallback
}

TransmitModel::TransmitModel(QObject* parent)
    : QObject(parent)
{
}

TransmitModel::~TransmitModel() = default;

void TransmitModel::setMox(bool mox)
{
    if (m_mox != mox) {
        m_mox = mox;
        emit moxChanged(mox);
    }
}

void TransmitModel::setTune(bool tune)
{
    if (m_tune != tune) {
        m_tune = tune;
        emit tuneChanged(tune);
    }
}

void TransmitModel::setPower(int power)
{
    if (m_power != power) {
        m_power = power;
        emit powerChanged(power);
    }
}

void TransmitModel::setMicGain(float gain)
{
    if (!qFuzzyCompare(m_micGain, gain)) {
        m_micGain = gain;
        emit micGainChanged(gain);
    }
}

void TransmitModel::setPureSigEnabled(bool enabled)
{
    if (m_pureSigEnabled != enabled) {
        m_pureSigEnabled = enabled;
        emit pureSigChanged(enabled);
    }
}

void TransmitModel::setTxOwnerSlot(VaxSlot s)
{
    const VaxSlot prev = m_txOwnerSlot.exchange(s, std::memory_order_acq_rel);
    if (prev == s) { return; }

    AppSettings::instance().setValue(
        QStringLiteral("tx/OwnerSlot"), vaxSlotToString(s));
    // No eager save() — matches TransmitModel's existing flush policy
    // (no other setters call AppSettings::instance().save() here).

    emit txOwnerSlotChanged(s);
}

void TransmitModel::loadFromSettings()
{
    const QString v = AppSettings::instance()
        .value(QStringLiteral("tx/OwnerSlot"), QStringLiteral("MicDirect"))
        .toString();
    const VaxSlot s = vaxSlotFromString(v);
    if (s != m_txOwnerSlot.load(std::memory_order_acquire)) {
        m_txOwnerSlot.store(s, std::memory_order_release);
        emit txOwnerSlotChanged(s);
    }
}

} // namespace NereusSDR
