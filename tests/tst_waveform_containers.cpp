// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
#include "core/AppSettings.h"
#include "gui/containers/ContainerArrangeController.h"
#include "gui/containers/ContainerContentHost.h"
#include "gui/containers/ContainerContentRegistry.h"
#include "gui/containers/ContainerDocumentCodec.h"
#include "gui/containers/ContainerWorkspaceStore.h"
#include "gui/containers/ContentPropertyEditor.h"
#include "gui/meters/WaveformMeterItem.h"
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
using namespace NereusSDR;
class TestWaveformContainers : public QObject
{
    Q_OBJECT
  private slots:
    void propertiesUseTypedWindowAndStageAndPreserveOpaqueConfig()
    {
        ContainerContentRegistry r;
        auto entry = r.makeEntry("meter.radeMicWaveform");
        entry.config["opaque"] = 42;
        entry.context["sliceId"] = 2;
        ContentPropertyEditor editor(r);
        QSignalSpy edits(&editor, &ContentPropertyEditor::entryEdited);
        editor.setEntry(entry);
        auto* window = editor.findChild<QComboBox*>("viewWindowMs");
        auto* stage = editor.findChild<QComboBox*>("sourceStage");
        QVERIFY(window);
        QVERIFY(stage);
        QVERIFY(!editor.findChild<QLineEdit*>("mmioGuid"));
        QVERIFY(!editor.findChild<QComboBox*>("bindingId"));
        QCOMPARE(edits.size(), 0);
        window->setCurrentIndex(window->findData(100));
        stage->setCurrentIndex(stage->findData("RawMic"));
        QVERIFY(edits.size() >= 2);
        const auto captured = qvariant_cast<ContentEntry>(edits.last()[0]);
        QCOMPARE(captured.config["opaque"].toInt(), 42);
        QCOMPARE(captured.context["sliceId"].toInt(), 2);
        std::unique_ptr<MeterItem> item(r.createMeterItem(captured, nullptr, ContentRenderMode::Validation));
        auto* face = qobject_cast<WaveformMeterItem*>(item.get());
        QVERIFY(face);
        QCOMPARE(face->viewWindowMs(), 100);
        QCOMPARE(face->waveformKind(), WaveformKind::RawMicrophone);
    }
    void documentStoreAndDuplicationRetainAllAxes()
    {
        QTemporaryDir directory;
        AppSettings settings(directory.filePath("settings.xml"));
        ContainerWorkspaceStore store(settings);
        ContainerContentRegistry registry;
        WorkspaceDocument workspace;
        workspace.mainContainerId = "main";
        ContainerDocument container;
        container.id = "main";
        container.layout = ContentLayout::FreeCanvas;
        auto entry = registry.makeEntry("meter.radeMicWaveform");
        entry.config["opaque"] = QJsonObject{{"future", true}};
        entry.config["properties"] = QJsonObject{{"viewWindowMs", 100}, {"sourceStage", "RawMic"}};
        entry.context = {{"sliceId", 2}, {"sessionId", "station"}, {"onlyWhenRx", true}};
        entry.visible = false;
        entry.paintOrder = 19;
        entry.setFreeCanvasRect(QRectF(40, 50, 460, 280));
        container.contents = {entry};
        workspace.containers = {container};
        auto decoded = ContainerDocumentCodec::decode(ContainerDocumentCodec::encode(workspace));
        QVERIFY(decoded.ok);
        QCOMPARE(decoded.document, workspace);
        QCOMPARE(store.commit(workspace, 0).status, CommitStatus::Saved);
        ContainerArrangeController arrange(store);
        QVERIFY(arrange.canDuplicate(entry));
        QVERIFY(arrange.duplicateEntry(entry.id, "main", 1).ok);
        const auto saved = store.snapshot();
        QCOMPARE(saved.containers[0].contents.size(), 2);
        const auto duplicate = saved.containers[0].contents[1];
        QVERIFY(duplicate.id != entry.id);
        QCOMPARE(duplicate.config, entry.config);
        QCOMPARE(duplicate.context, entry.context);
        QCOMPARE(duplicate.freeCanvasRect(), entry.freeCanvasRect());
        QCOMPARE(duplicate.paintOrder, 19);
        QVERIFY(!duplicate.visible);
        AppSettings reloadedSettings(directory.filePath("settings.xml"));
        reloadedSettings.load();
        ContainerWorkspaceStore reloaded(reloadedSettings);
        QVERIFY(reloaded.load().ok);
        QCOMPARE(reloaded.snapshot(), saved);
    }
    void hostUsesOrdinaryStackAndCanvasGeometry()
    {
        ContainerContentRegistry r;
        ContainerContentHost host(r);
        ContainerDocument doc;
        doc.id = "wave";
        doc.layout = ContentLayout::VerticalStack;
        auto mic = r.makeEntry("meter.radeMicWaveform"), speech = r.makeEntry("meter.radeSpeechWaveform"),
             power = r.makeEntry("meter.powerSwr");
        doc.contents = {mic, speech, power};
        host.reconcile(doc);
        QCOMPARE(host.entryRows().size(), 3);
        QVERIFY(host.entryRows()[0].item);
        QCOMPARE(host.entryRows()[0].height, 300);
        doc = host.captureDocument();
        doc.layout = ContentLayout::FreeCanvas;
        doc.contents[0].setFreeCanvasRect(QRectF(30, 40, 490, 300));
        doc.contents[1].setFreeCanvasRect(QRectF(540, 40, 490, 300));
        host.reconcile(doc);
        QCOMPARE(host.entryRows()[0].widget->property("freeCanvasMinimum").toSizeF(), QSizeF(240, 180));
        QCOMPARE(host.captureDocument().contents[0].freeCanvasRect(), doc.contents[0].freeCanvasRect());
        doc = host.captureDocument();
        doc.layout = ContentLayout::VerticalStack;
        host.reconcile(doc);
        QCOMPARE(host.entryRows().size(), 3);
        QCOMPARE(qobject_cast<WaveformMeterItem*>(host.entryRows()[1].item)->preferredFaceHeight(), 300);
    }
    void saveActualHostFaceCompositions()
    {
        const QString directory = qEnvironmentVariable("NEREUS_WAVEFORM_SHOTS");
        if (directory.isEmpty()) {
            return;
        }
        QVERIFY(QDir().mkpath(directory));
        ContainerContentRegistry registry;
        ContainerContentHost host(registry);
        ContainerDocument document;
        document.id = "shots";
        document.layout = ContentLayout::VerticalStack;
        auto mic = registry.makeEntry("meter.radeMicWaveform"),
             speech = registry.makeEntry("meter.radeSpeechWaveform"),
             power = registry.makeEntry("meter.powerSwr");
        document.contents = {mic, speech, power};
        host.reconcile(document);
        auto sample = std::make_shared<WaveformSnapshot>();
        sample->sampleRate = 16000;
        sample->asOfNs = 5000000000LL;
        sample->availability = WaveformAvailability::Available;
        sample->samples.resize(80000);
        sample->segments = {{0, 80000, 0, 0}};
        for (int i = 0; i < sample->samples.size(); ++i) {
            sample->samples[i] = .7f * std::sin(i * .072f) * std::sin(i * .00042f);
        }
        for (const auto& row : host.entryRows()) {
            if (auto* waveform = qobject_cast<WaveformMeterItem*>(row.item.data())) {
                auto frame = std::make_shared<WaveformSnapshot>(*sample);
                frame->kind = waveform->waveformKind();
                if (frame->kind == WaveformKind::DecodedSpeech) {
                    frame->sampleRate = 24000;
                    frame->samples.resize(120000);
                    frame->segments = {{0, 120000, 0, 0}};
                    for (int i = 0; i < 120000; ++i) {
                        frame->samples[i] = .6f * std::sin(i * .048f) * std::sin(i * .00028f);
                    }
                }
                waveform->setSnapshot(frame);
            }
        }
        for (int dpr : {1, 2}) {
            QImage image(600 * dpr, 1000 * dpr, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(dpr);
            image.fill(QColor("#0c0c0e"));
            QPainter painter(&image);
            int top = 0;
            for (const auto& row : host.entryRows()) {
                QVERIFY(row.item);
                const float x = row.item->x(), y = row.item->y(), w = row.item->itemWidth(),
                            h = row.item->itemHeight();
                row.item->setRect(0, 0, 1, 1);
                painter.save();
                painter.translate(0, top);
                row.item->paint(painter, 600, row.height);
                painter.restore();
                row.item->setRect(x, y, w, h);
                top += row.height;
            }
            painter.end();
            QVERIFY(image.save(directory + QString("/stack-dpr%1.png").arg(dpr)));
        }
        document = host.captureDocument();
        document.layout = ContentLayout::FreeCanvas;
        document.contents[0].setFreeCanvasRect(QRectF(20, 20, 490, 300));
        document.contents[1].setFreeCanvasRect(QRectF(530, 20, 490, 300));
        document.contents[2].setFreeCanvasRect(QRectF(20, 340, 490, 300));
        host.reconcile(document);
        for (const auto& row : host.entryRows()) {
            if (auto* waveform = qobject_cast<WaveformMeterItem*>(row.item.data())) {
                auto frame = std::make_shared<WaveformSnapshot>(*sample);
                frame->kind = waveform->waveformKind();
                if (frame->kind == WaveformKind::DecodedSpeech) {
                    frame->sampleRate = 24000;
                    frame->samples.resize(120000);
                    frame->segments = {{0, 120000, 0, 0}};
                    for (int j = 0; j < 120000; ++j) {
                        frame->samples[j] = .6f * std::sin(j * .048f) * std::sin(j * .00028f);
                    }
                }
                waveform->setSnapshot(frame);
                if (waveform->waveformKind() == WaveformKind::DecodedSpeech) {
                    QVERIFY(waveform->applyConfiguration({{"viewWindowMs", 100}}));
                }
            }
        }
        QImage image(1040 * 2, 660 * 2, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(2);
        image.fill(QColor("#0c0c0e"));
        QPainter painter(&image);
        for (int i = 0; i < host.entryRows().size(); ++i) {
            const auto& row = host.entryRows()[i];
            const QRectF rect = *document.contents[i].freeCanvasRect();
            painter.save();
            painter.translate(rect.topLeft());
            row.item->paint(painter, int(rect.width()), int(rect.height()));
            painter.restore();
        }
        painter.end();
        QVERIFY(image.save(directory + "/canvas-dpr2.png"));
    }
};
QTEST_MAIN(TestWaveformContainers)
#include "tst_waveform_containers.moc"
