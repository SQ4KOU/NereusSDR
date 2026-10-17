// no-port-check: NereusSDR-original test-only access to the IMD display plane.
#pragma once

#include "gui/ImdOverlay.h"
#include "gui/SpectrumWidget.h"
#include <QImage>
#include <QPainter>

namespace NereusSDR {

class ImdSpectrumTestAccess {
public:
    static void seedVisiblePixels(SpectrumWidget& widget, const QVector<float>& pixels)
    {
        widget.m_renderedPixels = pixels;
        widget.acceptImdVisibleSpectrum(++widget.m_imdLocalFrameSequence, true);
    }
    static void queueAcceptedLocalWaterfallRow(SpectrumWidget& widget,
                                               const QVector<float>& pixels)
    {
        widget.m_pendingWfPixelsDbm = pixels;
        widget.m_pendingWfPixelsDbmDirty = true;
        widget.m_pendingWfImdAcceptedOrdinal = widget.m_imdAcceptedFrameOrdinal;
    }
    static qint64 freshnessTimeoutMs(const SpectrumWidget& widget)
    {
        return widget.imdFreshnessTimeoutMs();
    }
    static void checkFreshnessAt(SpectrumWidget& widget, qint64 nowMs)
    {
        widget.checkImdFreshness(nowMs);
    }
    static const QVector<float>& visiblePixels(const SpectrumWidget& widget)
    {
        return widget.m_renderedPixels;
    }
    static QImage render(SpectrumWidget& widget)
    {
        QImage image(800, 360, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        if (widget.m_moxOverlay && widget.m_testingIMD
            && widget.m_showIMDMeasurements && widget.m_displayDuplex) {
            widget.drawImdOverlay(painter, QRect(0, 0, 799, 360));
        }
        return image;
    }
    static const ImdReadout& readout(const SpectrumWidget& widget)
    {
        static const ImdReadout invalid;
        const auto& summary = widget.imdMeasurementFrame().summary;
        return summary ? *summary : invalid;
    }
    static int frequencyX(const SpectrumWidget& widget, double hz, const QRect& rect)
    {
        return widget.hzToX(hz, rect);
    }
    static RemoteSpectrumCapture remoteCapture(const SpectrumWidget& widget)
    {
        return widget.m_remoteCapture;
    }
    static bool overlayDirty(const SpectrumWidget& widget) { return widget.overlayStaticDirtyForTest(); }
    static bool testing(const SpectrumWidget& widget) { return widget.m_testingIMD; }
    static bool shown(const SpectrumWidget& widget) { return widget.m_showIMDMeasurements; }
    static bool mox(const SpectrumWidget& widget) { return widget.m_moxOverlay; }
    static bool duplex(const SpectrumWidget& widget) { return widget.m_displayDuplex; }
};

} // namespace NereusSDR
