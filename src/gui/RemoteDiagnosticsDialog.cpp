// =================================================================
// src/gui/RemoteDiagnosticsDialog.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original remote telemetry presentation adapter.
// It consumes the separately attributed, protocol-neutral
// TimeSeriesGraphWidget and TelemetryHistory port without adding protocol or
// collection behavior here.
// =================================================================

#include "gui/RemoteDiagnosticsDialog.h"

#include "gui/RemoteTelemetryController.h"
#include "gui/TelemetryHistory.h"
#include "gui/TimeSeriesGraphWidget.h"

#include <QColor>
#include <QComboBox>
#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QTabWidget>
#include <QVBoxLayout>

#include <initializer_list>
#include <utility>

namespace NereusSDR {
namespace {

using Metric = TelemetryHistory::Metric;

struct GraphSeriesSpec {
    Metric metric;
    const char* label;
    const char* color;
    const char* unit;
};

TimeSeriesGraphWidget::Series toGraphSeries(const TelemetryHistory& history,
                                             Metric metric,
                                             const GraphSeriesSpec& spec,
                                             qint64 nowMs, int rangeSeconds)
{
    const TelemetryHistory::Series source = history.series(metric, nowMs, rangeSeconds);
    TimeSeriesGraphWidget::Series result;
    result.label = QObject::tr(spec.label);
    result.color = QColor(QLatin1String(spec.color));
    result.unitSuffix = QLatin1String(spec.unit);
    result.maxConnectGapSeconds = source.maxConnectGapSeconds;
    result.points.reserve(source.points.size());
    result.breakBefore.reserve(source.points.size());
    for (const TelemetryHistory::Point& point : source.points) {
        result.points.append({point.seconds, point.value});
        result.breakBefore.append(point.breakBefore);
    }
    return result;
}

void setGraph(TimeSeriesGraphWidget* graph, const TelemetryHistory& history,
              qint64 nowMs, int rangeSeconds,
              std::initializer_list<GraphSeriesSpec> specs)
{
    QVector<TimeSeriesGraphWidget::Series> series;
    series.reserve(static_cast<qsizetype>(specs.size()));
    for (const GraphSeriesSpec& spec : specs) {
        series.append(toGraphSeries(history, spec.metric, spec, nowMs, rangeSeconds));
    }
    graph->setSeries(std::move(series), rangeSeconds);
}

} // namespace

RemoteDiagnosticsDialog::RemoteDiagnosticsDialog(RemoteTelemetryController* controller,
                                                 QWidget* parent)
    : QDialog(parent)
    , m_controller(controller)
{
    setWindowTitle(tr("Remote Network Diagnostics"));
    setMinimumSize(760, 620);
    resize(980, 760);

    buildUi();
    m_refreshTimer.setInterval(1000);
    connect(&m_refreshTimer, &QTimer::timeout, this, &RemoteDiagnosticsDialog::refresh);
    if (controller) {
        // Controller sampling can notify at a higher cadence. It remains the
        // owner of data; this dialog deliberately renders only on its 1 Hz
        // visible timer.
        connect(controller, &QObject::destroyed, this, [this] {
            m_controller = nullptr;
            if (isVisible()) {
                refreshDetail();
            }
        });
    }
}

RemoteTelemetryController* RemoteDiagnosticsDialog::controller() const
{
    return m_controller.data();
}

void RemoteDiagnosticsDialog::buildUi()
{
    setStyleSheet(QStringLiteral(
        "QDialog { background: #0f0f1a; }"
        "QTabWidget::pane { border: 1px solid #203040; }"
        "QTabBar::tab { background: #172535; color: #8aa8c0; padding: 6px 12px; }"
        "QTabBar::tab:selected { background: #203b52; color: #c8d8e8; }"
        "QLabel { color: #c8d8e8; }"
        "QComboBox { background: #172535; color: #c8d8e8; border: 1px solid #203040; padding: 3px 6px; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(8);

    auto* controls = new QHBoxLayout;
    auto* rangeLabel = new QLabel(tr("History:"), this);
    controls->addWidget(rangeLabel);
    m_rangeSelector = new QComboBox(this);
    m_rangeSelector->setObjectName(QStringLiteral("remoteDiagnosticsRange"));
    const std::initializer_list<std::pair<const char*, int>> ranges{
        {"1 min", 60}, {"5 min", 5 * 60}, {"15 min", 15 * 60},
        {"1 h", 60 * 60}, {"24 h", 24 * 60 * 60}, {"7 d", 7 * 24 * 60 * 60},
    };
    for (const auto& [label, seconds] : ranges) {
        m_rangeSelector->addItem(tr(label), seconds);
    }
    m_rangeSelector->setCurrentIndex(m_rangeSelector->findData(m_rangeSeconds));
    controls->addWidget(m_rangeSelector);
    controls->addStretch();
    root->addLayout(controls);
    connect(m_rangeSelector, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &RemoteDiagnosticsDialog::setRangeFromSelector);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("remoteDiagnosticsTabs"));
    QWidget* connection = buildTab(tr("Connection"));
    m_radioLinkGraph = addGraph(connection, tr("Radio link throughput"), tr(" Mbps"));
    m_radioLinkGraph->setObjectName(QStringLiteral("remoteRadioLinkGraph"));
    m_controlPayloadGraph = addGraph(connection, tr("Control payload throughput"), tr(" kbit/s"));
    m_controlPayloadGraph->setObjectName(QStringLiteral("remoteControlPayloadGraph"));
    tabs->addTab(connection, tr("Connection"));

    QWidget* roundTrip = buildTab(tr("Round trip"));
    m_roundTripGraph = addGraph(roundTrip, tr("Round-trip time"), tr(" ms"));
    m_roundTripGraph->setObjectName(QStringLiteral("remoteRoundTripGraph"));
    m_roundTripGraph->setToolTip(tr("RTT graphs hold the last measurement between pings; age advances independently and stale values disappear."));
    m_packetAgeGraph = addGraph(roundTrip, tr("Last admitted audio packet age"), tr(" ms"));
    m_packetAgeGraph->setObjectName(QStringLiteral("remotePacketAgeGraph"));
    tabs->addTab(roundTrip, tr("Round trip"));

    QWidget* audio = buildTab(tr("Audio"));
    m_audioPacketsGraph = addGraph(audio, tr("Audio packet activity"), tr(" packets/s"));
    m_audioPacketsGraph->setObjectName(QStringLiteral("remoteAudioPacketsGraph"));
    m_sourceFramesGraph = addGraph(audio, tr("Core source frames"), tr(" frames/s"));
    m_sourceFramesGraph->setObjectName(QStringLiteral("remoteSourceFramesGraph"));
    m_audioEventsGraph = addGraph(audio, tr("Audio interruption events"), tr(" events/s"));
    m_audioEventsGraph->setObjectName(QStringLiteral("remoteAudioEventsGraph"));
    tabs->addTab(audio, tr("Audio"));
    root->addWidget(tabs, 1);

    auto* detailScroll = new QScrollArea(this);
    detailScroll->setWidgetResizable(true);
    detailScroll->setMinimumHeight(110);
    detailScroll->setMaximumHeight(170);
    detailScroll->setStyleSheet(QStringLiteral("QScrollArea { border: 1px solid #203040; background: #101a26; }"));
    m_detailLabel = new QLabel(detailScroll);
    m_detailLabel->setObjectName(QStringLiteral("remoteDiagnosticsDetail"));
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailLabel->setContentsMargins(8, 6, 8, 6);
    detailScroll->setWidget(m_detailLabel);
    root->addWidget(detailScroll);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    for (QAbstractButton* button : buttons->buttons()) {
        if (auto* pushButton = qobject_cast<QPushButton*>(button)) {
            pushButton->setAutoDefault(false);
        }
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    root->addWidget(buttons);
}

QWidget* RemoteDiagnosticsDialog::buildTab(const QString& title)
{
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setObjectName(title);
    auto* content = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    layout->addStretch();
    scroll->setWidget(content);
    return scroll;
}

TimeSeriesGraphWidget* RemoteDiagnosticsDialog::addGraph(QWidget* tab, const QString& title,
                                                          const QString& suffix)
{
    auto* scroll = qobject_cast<QScrollArea*>(tab);
    auto* graph = new TimeSeriesGraphWidget(title, suffix, scroll->widget());
    auto* layout = qobject_cast<QVBoxLayout*>(scroll->widget()->layout());
    layout->insertWidget(layout->count() - 1, graph);
    return graph;
}

void RemoteDiagnosticsDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    refresh(); // Show current data immediately; later renders are limited to 1 Hz.
    m_refreshTimer.start();
}

void RemoteDiagnosticsDialog::hideEvent(QHideEvent* event)
{
    m_refreshTimer.stop();
    QDialog::hideEvent(event);
}

void RemoteDiagnosticsDialog::refresh()
{
    if (!isVisible()) {
        return;
    }
    refreshGraphs();
    refreshDetail();
}

void RemoteDiagnosticsDialog::setRangeFromSelector(int index)
{
    if (index < 0 || !m_rangeSelector) {
        return;
    }
    const int range = m_rangeSelector->itemData(index).toInt();
    if (range <= 0 || range == m_rangeSeconds) {
        return;
    }
    m_rangeSeconds = range;
    if (isVisible()) {
        refresh();
    }
}

void RemoteDiagnosticsDialog::refreshGraphs()
{
    if (!m_controller) {
        return;
    }
    const auto& history = m_controller->history();
    const qint64 nowMs = m_controller->nowMs();
    setGraph(m_radioLinkGraph, history, nowMs, m_rangeSeconds, {
        {Metric::RadioRxMbps, "Radio RX", "#00b4d8", " Mbps"},
        {Metric::RadioTxMbps, "Radio TX", "#5fff8a", " Mbps"},
    });
    setGraph(m_controlPayloadGraph, history, nowMs, m_rangeSeconds, {
        {Metric::SessionPayloadRxKbps, "Control RX", "#5fa8ff", " kbit/s"},
        {Metric::SessionPayloadTxKbps, "Control TX", "#ffd700", " kbit/s"},
    });
    setGraph(m_roundTripGraph, history, nowMs, m_rangeSeconds, {
        {Metric::RadioRttMs, "Last radio RTT", "#00b4d8", " ms"},
        {Metric::SessionRttMs, "Last Core RTT", "#ffb86c", " ms"},
    });
    setGraph(m_packetAgeGraph, history, nowMs, m_rangeSeconds, {
        {Metric::PlaybackPacketAgeMs, "Packet age", "#c792ea", " ms"},
    });
    setGraph(m_audioPacketsGraph, history, nowMs, m_rangeSeconds, {
        {Metric::AudioEncodedPacketsPerSecond, "Core encoded", "#00b4d8", " packets/s"},
        {Metric::AudioSendAcceptedPerSecond, "Core accepted", "#5fff8a", " packets/s"},
        {Metric::AudioSendRejectedPerSecond, "Core refused", "#ff6060", " packets/s"},
        {Metric::PlaybackDecodedPacketsPerSecond, "GUI decoded", "#5fa8ff", " packets/s"},
        {Metric::PlaybackConcealedPacketsPerSecond, "GUI concealed", "#ffd700", " packets/s"},
        {Metric::PlaybackLatePacketsPerSecond, "GUI late", "#ff8c00", " packets/s"},
    });
    setGraph(m_sourceFramesGraph, history, nowMs, m_rangeSeconds, {
        {Metric::AudioSourceFramesPerSecond, "Core source", "#00b4d8", " frames/s"},
    });
    setGraph(m_audioEventsGraph, history, nowMs, m_rangeSeconds, {
        {Metric::AudioSourceDropsPerSecond, "Core source drops", "#ff6060", " events/s"},
        {Metric::PlaybackUnderflowsPerSecond, "GUI underflows", "#ffd700", " events/s"},
        {Metric::PlaybackOverflowsPerSecond, "GUI overflows", "#ff8c00", " events/s"},
    });
}

void RemoteDiagnosticsDialog::refreshDetail()
{
    if (!m_detailLabel) {
        return;
    }
    m_detailLabel->setText(m_controller
        ? m_controller->detailText()
        : tr("Remote telemetry controller is unavailable."));
}

} // namespace NereusSDR
