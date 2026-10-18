// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
//   2026-10-12 — J.J. Boyd (KG4VCF); AI-assisted via OpenAI Codex.
// no-port-check: NereusSDR-original.
#include "MultiDeviceHarness.h"
#include "core/session/StationClient.h"
#include "core/settings/SettingsProxy.h"
#include "gui/widgets/VfoWidget.h"
#include <QComboBox>
#include <QSignalSpy>

class TestRadeV2LinkGate : public QObject {
    Q_OBJECT
    struct Link {
        Core core{true};
        RadioModel remote{RadioModel::Role::Remote};
        SettingsProxy settings;
        StationClient client{&remote, &settings};
        QPointer<LoopbackTransport> stationEnd;
        QPointer<LoopbackTransport> clientEnd;
        static LoopbackTransport* ownedTransport(const QString& description, QObject* owner)
        {
            // Session replacement schedules the retired transport for deletion.
            // Qt parent ownership, as in the existing station-link fixtures.
            return std::make_unique<LoopbackTransport>(description, owner).release();
        }
        explicit Link(QObject* owner)
            : stationEnd(ownedTransport(QStringLiteral("V2-Core"), owner))
            , clientEnd(ownedTransport(QStringLiteral("V2-client"), owner)) {}

        bool connect()
        {
            QSignalSpy completed(&client, &StationClient::handshakeComplete);
            stationEnd->linkTo(clientEnd);
            client.startSession(clientEnd, core.server->token());
            core.server->acceptTransport(stationEnd);
            return QTest::qWaitFor([&] { return completed.count() == 1; });
        }
        void catalog(const QJsonValue& enabled)
        {
            QJsonObject json{{QStringLiteral("modes"), QJsonArray{
                QJsonObject{{QStringLiteral("id"), 14}}, QJsonObject{{QStringLiteral("id"), 15}}}}};
            if (!enabled.isUndefined()) { json.insert(QStringLiteral("radeV2Enabled"), enabled); }
            stationEnd->sendText(SessionMessages::encode(SessionMessages::delta("catalog", {
                MirrorUpdate{0, "json", MirrorWireKind::Utf8,
                    QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact))}})));
        }
    };
private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }
    void coreCatalogControlsRemoteChoicesAndResetsOnDisconnect()
    {
        Link link(this);
        QVERIFY(link.connect());
        QCOMPARE(link.remote.radeV2Enabled(), link.core.model->radeV2Enabled());
        VfoWidget vfo;
        vfo.setRadioModel(&link.remote);
        QComboBox* combo = nullptr;
        for (QComboBox* candidate : vfo.findChildren<QComboBox*>()) {
            if (candidate->findText(QStringLiteral("RADE-U")) >= 0) { combo = candidate; break; }
        }
        QVERIFY(combo);
        QSignalSpy requests(&vfo, &VfoWidget::modeChanged);
        StationCapabilities caps = link.client.capabilities();
        caps.audioQualityVersion = 0;
        link.stationEnd->sendText(SessionMessages::encode(SessionMessages::capabilities(caps.toUpdates())));
        link.catalog(true);
        QTRY_VERIFY(link.remote.radeV2Enabled());
        QTRY_VERIFY(combo->findText(QStringLiteral("RADE2-U")) >= 0);
        for (const QJsonValue& flag : {QJsonValue(false), QJsonValue("true"), QJsonValue(1),
                                       QJsonValue(QJsonValue::Undefined)}) {
            link.catalog(flag);
            QTRY_VERIFY(!link.remote.radeV2Enabled());
            QTRY_COMPARE(combo->findText(QStringLiteral("RADE2-U")), -1);
            link.catalog(true);
            QTRY_VERIFY(link.remote.radeV2Enabled());
        }
        QCOMPARE(requests.count(), 0);
        LoopbackTransport* replacementCore = Link::ownedTransport(QStringLiteral("replacement-Core"), this);
        LoopbackTransport* replacementClient = Link::ownedTransport(QStringLiteral("replacement-client"), this);
        replacementCore->linkTo(replacementClient);
        QSignalSpy completed(&link.client, &StationClient::handshakeComplete);
        const QPointer<LoopbackTransport> retiredClient = link.clientEnd;
        link.client.startSession(replacementClient, link.core.server->token());
        QVERIFY(!link.remote.radeV2Enabled());
        // Qt has scheduled deletion, but has not drained it yet. A stale
        // catalogue from the retired sender cannot authorize this session.
        QVERIFY(retiredClient);
        emit retiredClient->textReceived(SessionMessages::encode(SessionMessages::delta("catalog", {
            MirrorUpdate{0, "json", MirrorWireKind::Utf8, QStringLiteral("{\"radeV2Enabled\":true}")}})));
        QVERIFY(!link.remote.radeV2Enabled());
        link.core.server->acceptTransport(replacementCore);
        QTRY_COMPARE(completed.count(), 1);
        QTRY_COMPARE(link.remote.radeV2Enabled(), link.core.model->radeV2Enabled());
        replacementCore->sendText(SessionMessages::encode(SessionMessages::delta("catalog", {
            MirrorUpdate{0, "json", MirrorWireKind::Utf8, QStringLiteral("{\"radeV2Enabled\":true}")}})));
        QTRY_VERIFY(link.remote.radeV2Enabled());
        link.client.disconnectFromStation(QStringLiteral("gate test"));
        QVERIFY(!link.remote.radeV2Enabled());
        QCOMPARE(combo->findText(QStringLiteral("RADE2-U")), -1);
        QCOMPARE(requests.count(), 0);
    }
    void inboundCurrentV2SurvivesWithoutOfferingNewV2Writes()
    {
        Link link(this);
        const QString key = QStringLiteral("Slice0/Band20m/DspMode");
        AppSettings::instance().setValue(key, 15);
        link.core.model->sliceById(0)->restoreFromSettings(Band::Band20m);
        QVERIFY(link.connect());
        QTRY_COMPARE(link.remote.sliceById(0)->dspMode(), DSPMode::RADE2_L);
        link.catalog(QJsonValue(QJsonValue::Undefined));
        QTRY_VERIFY(!link.remote.radeV2Enabled());
        SliceModel* slice = link.remote.sliceById(0);
        slice->setDspMode(DSPMode::RADE2_U);
        QCOMPARE(slice->dspMode(), DSPMode::RADE2_L);
        QCOMPARE(AppSettings::instance().value(key).toInt(), 15);
    }
    void coreRejectsDisabledRawV2RequestsWithReason()
    {
        Link link(this);
        QVERIFY(link.connect());
        const DSPMode original = link.core.model->sliceById(0)->dspMode();
        link.clientEnd->sendText(SessionMessages::encode(SessionMessages::propertyWrite("slice:0", {
            MirrorUpdate{0, "dspMode", MirrorWireKind::Enum, qlonglong(14)}}, 71)));
        SessionMessage result;
        QTRY_VERIFY(([&] {
            for (const QByteArray& wire : link.clientEnd->received()) {
                SessionMessage candidate;
                if (SessionMessages::decode(wire, &candidate)
                    && candidate.kind == SessionMessageKind::PropertyResult && candidate.writeId == 71) {
                    result = candidate;
                    return true;
                }
            }
            return false;
        })());
        QCOMPARE(result.propertyResults.size(), 1);
        if (link.core.model->radeV2Enabled()) {
            QVERIFY2(result.propertyResults.first().accepted, qPrintable(result.propertyResults.first().reason));
            QCOMPARE(link.core.model->sliceById(0)->dspMode(), DSPMode::RADE2_U);
        } else {
            QVERIFY(!result.propertyResults.first().accepted);
            QVERIFY(result.propertyResults.first().reason.contains(QStringLiteral("NEREUS_ENABLE_RADE_V2=1")));
            QCOMPARE(link.core.model->sliceById(0)->dspMode(), original);
        }
    }
};
QTEST_MAIN(TestRadeV2LinkGate)
#include "tst_rade_v2_link_gate.moc"
