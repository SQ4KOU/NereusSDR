#pragma once
// no-port-check: NereusSDR-original Setup description transport.
#include "core/BoardCapabilities.h"
#include "core/HpsdrModel.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace NereusSDR {

// The string properties are fixed so MirrorSchema can announce the same
// ordinals to every client. Empty strings name categories not published yet.
class SetupDescription final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString general READ general NOTIFY descriptionsChanged)
    Q_PROPERTY(QString hardware READ hardware NOTIFY descriptionsChanged)
    Q_PROPERTY(QString audio READ audio NOTIFY descriptionsChanged)
    Q_PROPERTY(QString dsp READ dsp NOTIFY descriptionsChanged)
    Q_PROPERTY(QString display READ display NOTIFY descriptionsChanged)
    Q_PROPERTY(QString transmit READ transmit NOTIFY descriptionsChanged)
    Q_PROPERTY(QString appearance READ appearance NOTIFY descriptionsChanged)
    Q_PROPERTY(QString catNetwork READ catNetwork NOTIFY descriptionsChanged)
    Q_PROPERTY(QString test READ test NOTIFY descriptionsChanged)
    Q_PROPERTY(QString diagnostics READ diagnostics NOTIFY descriptionsChanged)
    Q_PROPERTY(quint32 revision READ revision NOTIFY descriptionsChanged)
    Q_PROPERTY(QString pa READ pa NOTIFY descriptionsChanged)
public:
    explicit SetupDescription(QObject* parent = nullptr);

    QJsonObject category(const QString& id) const;
    static bool validateActiveSlicePropertyBinding(const QJsonObject& control);
    static bool validateTransmitPropertyBinding(const QJsonObject& control);
    static bool validateHardwarePropertyBinding(const QJsonObject& control,
                                                HPSDRModel model = HPSDRModel::FIRST);
    static bool validatePaReadoutBinding(const QJsonObject& control);
    static bool validateTransmitSettingBinding(const QJsonObject& control);
    static bool validateAudioPropertyBinding(const QJsonObject& control);
    static bool validateDspSettingBinding(const QJsonObject& control);
    static bool validateSettingToggleEncoding(const QJsonObject& control);
    static bool validateCommandBinding(const QJsonObject& control, QString* error = nullptr);
    static bool validateTnfTable(const QJsonObject& control, QString* error = nullptr);
    /// Stateless per-session projection of a category string (empty if no ready pages).
    static QString fitCategoryForVersion(const QString& description, int version);
    void setBoardCapabilities(const BoardCapabilities& caps);
    void setRadioContext(const BoardCapabilities& caps, HPSDRModel model);
    quint32 revision() const { return m_revision; }
    QString general() const { return m_general; }
    QString hardware() const { return m_hardware; }
    QString audio() const { return m_audio; }
    QString dsp() const { return m_dsp; }
    QString display() const { return m_display; }
    QString transmit() const { return m_transmit; }
    QString appearance() const { return m_appearance; }
    QString catNetwork() const { return m_catNetwork; }
    QString test() const { return m_test; }
    QString diagnostics() const { return m_diagnostics; }
    QString pa() const { return m_pa; }

signals:
    void descriptionsChanged();

private:
    void rebuild();
    BoardCapabilities m_caps{};
    HPSDRModel m_model = HPSDRModel::FIRST;
    quint32 m_revision = 0;
    QString m_general;
    QString m_hardware;
    QString m_audio;
    QString m_dsp;
    QString m_display;
    QString m_transmit;
    QString m_appearance;
    QString m_catNetwork;
    QString m_test;
    QString m_diagnostics;
    QString m_pa;
};

using SetupDescriptionService = SetupDescription;

} // namespace NereusSDR
