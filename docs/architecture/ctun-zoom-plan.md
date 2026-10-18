# CTUN Zoom — Bin Subsetting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make spectrum trace, waterfall, and signal widths visually scale when the user zooms via the frequency scale bar drag or Ctrl+scroll.

**Architecture:** Add `m_ddcCenterHz` and `m_sampleRateHz` to SpectrumWidget so it can compute which FFT bins fall within the display window. A `visibleBinRange()` helper returns the first/last bin indices. GPU and CPU render paths iterate only those bins. The zoom drag emits `bandwidthChangeRequested` only on mouse release (hybrid: smooth subsetting during drag, FFT replan on release). Waterfall rows are pushed using only the visible bin subset; old rows are preserved across zoom changes.

**Tech Stack:** C++20, Qt6 (QRhiWidget GPU path + QPainter CPU fallback)

**Design spec:** `docs/architecture/ctun-zoom-design.md`

---

### Task 1: Add DDC center and sample rate members to SpectrumWidget

**Files:**
- Modify: `src/gui/SpectrumWidget.h:160-162`
- Modify: `src/gui/SpectrumWidget.cpp:224` (setCenterFrequency area)

- [ ] **Step 1: Add members and setters to header**

In `src/gui/SpectrumWidget.h`, after the public `setCenterFrequency` / `bandwidth()` block (line 69), add:

```cpp
    void setDdcCenterFrequency(double hz);
    double ddcCenterFrequency() const { return m_ddcCenterHz; }
    void setSampleRate(double hz);
    double sampleRate() const { return m_sampleRateHz; }
```

In the private member section, after `m_bandwidthHz` (line 162), add:

```cpp
    double m_ddcCenterHz{14225000.0};   // DDC hardware center frequency
    double m_sampleRateHz{768000.0};    // DDC sample rate
```

- [ ] **Step 2: Implement setters in .cpp**

In `src/gui/SpectrumWidget.cpp`, after `setCenterFrequency()` (around line 230), add:

```cpp
void SpectrumWidget::setDdcCenterFrequency(double hz)
{
    if (!qFuzzyCompare(m_ddcCenterHz, hz)) {
        m_ddcCenterHz = hz;
#ifdef NEREUS_GPU_SPECTRUM
        markOverlayDirty();
#else
        update();
#endif
    }
}

void SpectrumWidget::setSampleRate(double hz)
{
    if (!qFuzzyCompare(m_sampleRateHz, hz)) {
        m_sampleRateHz = hz;
#ifdef NEREUS_GPU_SPECTRUM
        markOverlayDirty();
#else
        update();
#endif
    }
}
```

- [ ] **Step 3: Build and verify no regressions**

Run:
```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```
Expected: Build succeeds with no errors.

- [ ] **Step 4: Commit**

```bash
git add src/gui/SpectrumWidget.h src/gui/SpectrumWidget.cpp
git commit -m "feat: add m_ddcCenterHz and m_sampleRateHz to SpectrumWidget"
```

---

### Task 2: Implement visibleBinRange() helper

**Files:**
- Modify: `src/gui/SpectrumWidget.h` (private section)
- Modify: `src/gui/SpectrumWidget.cpp`

- [ ] **Step 1: Declare helper in header**

In `src/gui/SpectrumWidget.h`, in the private section after `dbmToY` (line 150), add:

```cpp
    // Returns the first and last FFT bin indices visible in the current
    // display window (m_centerHz ± m_bandwidthHz/2), mapped against
    // the DDC center and sample rate. Clamped to [0, binCount-1].
    std::pair<int, int> visibleBinRange(int binCount) const;
```

- [ ] **Step 2: Implement in .cpp**

In `src/gui/SpectrumWidget.cpp`, after the `xToHz` function (around line 560), add:

```cpp
std::pair<int, int> SpectrumWidget::visibleBinRange(int binCount) const
{
    if (binCount <= 0 || m_sampleRateHz <= 0.0) {
        return {0, 0};
    }

    double binWidth = m_sampleRateHz / binCount;
    double fftLowHz = m_ddcCenterHz - m_sampleRateHz / 2.0;

    double displayLowHz  = m_centerHz - m_bandwidthHz / 2.0;
    double displayHighHz = m_centerHz + m_bandwidthHz / 2.0;

    int firstBin = static_cast<int>(std::floor((displayLowHz - fftLowHz) / binWidth));
    int lastBin  = static_cast<int>(std::ceil((displayHighHz - fftLowHz) / binWidth));

    firstBin = std::clamp(firstBin, 0, binCount - 1);
    lastBin  = std::clamp(lastBin, 0, binCount - 1);

    if (firstBin > lastBin) {
        firstBin = lastBin;
    }

    return {firstBin, lastBin};
}
```

- [ ] **Step 3: Build and verify**

Run:
```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/gui/SpectrumWidget.h src/gui/SpectrumWidget.cpp
git commit -m "feat: add visibleBinRange() for zoom bin subsetting"
```

---

### Task 3: Apply bin subsetting to GPU spectrum renderer

**Files:**
- Modify: `src/gui/SpectrumWidget.cpp:1537-1604` (renderGpuFrame FFT vertex generation)
- Modify: `src/gui/SpectrumWidget.cpp:1629-1653` (GPU draw calls)

- [ ] **Step 1: Replace full-array loop with bin subset in vertex generation**

In `src/gui/SpectrumWidget.cpp`, replace the FFT spectrum vertex block (lines 1537-1604):

Replace from `// ---- FFT spectrum vertices ----` through the `batch->updateDynamicBuffer` calls with:

```cpp
    // ---- FFT spectrum vertices ----
    if (!m_smoothed.isEmpty() && m_fftLineVbo && m_fftFillVbo) {
        auto [firstBin, lastBin] = visibleBinRange(m_smoothed.size());
        const int count = lastBin - firstBin + 1;
        const int n = qMin(count, kMaxFftBins);
        const float minDbm = m_refLevel - m_dynamicRange;
        const float range = m_dynamicRange;
        const float yBot = -1.0f;
        const float yTop = 1.0f;

        const float fr = m_fillColor.redF();
        const float fg = m_fillColor.greenF();
        const float fb = m_fillColor.blueF();
        const float fa = m_fillAlpha;

        QVector<float> lineVerts(n * kFftVertStride);
        QVector<float> fillVerts(n * 2 * kFftVertStride);

        for (int j = 0; j < n; ++j) {
            int i = firstBin + j;
            float x = (n > 1) ? 2.0f * j / (n - 1) - 1.0f : 0.0f;
            float t = qBound(0.0f, (m_smoothed[i] - minDbm) / range, 1.0f);
            float y = yBot + t * (yTop - yBot);

            // Heat map: blue → cyan → green → yellow → red
            // From AetherSDR SpectrumWidget.cpp:2298-2310
            float cr, cg, cb2;
            if (t < 0.25f) {
                float s = t / 0.25f;
                cr = 0.0f; cg = s; cb2 = 1.0f;
            } else if (t < 0.5f) {
                float s = (t - 0.25f) / 0.25f;
                cr = 0.0f; cg = 1.0f; cb2 = 1.0f - s;
            } else if (t < 0.75f) {
                float s = (t - 0.5f) / 0.25f;
                cr = s; cg = 1.0f; cb2 = 0.0f;
            } else {
                float s = (t - 0.75f) / 0.25f;
                cr = 1.0f; cg = 1.0f - s; cb2 = 0.0f;
            }

            // Line vertex
            int li = j * kFftVertStride;
            lineVerts[li]     = x;
            lineVerts[li + 1] = y;
            lineVerts[li + 2] = cr;
            lineVerts[li + 3] = cg;
            lineVerts[li + 4] = cb2;
            lineVerts[li + 5] = 0.9f;

            // Fill vertices (top at signal, bottom at base)
            int fi = j * 2 * kFftVertStride;
            fillVerts[fi]     = x;
            fillVerts[fi + 1] = y;
            fillVerts[fi + 2] = cr;
            fillVerts[fi + 3] = cg;
            fillVerts[fi + 4] = cb2;
            fillVerts[fi + 5] = fa * 0.3f;
            fillVerts[fi + 6] = x;
            fillVerts[fi + 7] = yBot;
            fillVerts[fi + 8]  = 0.0f;
            fillVerts[fi + 9]  = 0.0f;
            fillVerts[fi + 10] = 0.3f;
            fillVerts[fi + 11] = fa;
        }

        batch->updateDynamicBuffer(m_fftLineVbo, 0,
            n * kFftVertStride * sizeof(float), lineVerts.constData());
        batch->updateDynamicBuffer(m_fftFillVbo, 0,
            n * 2 * kFftVertStride * sizeof(float), fillVerts.constData());
    }
```

- [ ] **Step 2: Update GPU draw calls to use visible count**

In the draw call section (lines 1629-1653), the `n` variable used for `cb->draw(n)` and `cb->draw(n * 2)` needs to use the visible count. Store it as a member or recompute. Simplest: add a member `m_visibleBinCount{0}` to the header (private section, near the GPU resources), set it in the vertex generation block above, and use it in the draw calls.

In `src/gui/SpectrumWidget.h`, in the `#ifdef NEREUS_GPU_SPECTRUM` block (after line 299), add:

```cpp
    int m_visibleBinCount{0};  // bins rendered this frame (for draw call count)
```

In the vertex generation block (Task 3 Step 1), after computing `n`, add:

```cpp
        m_visibleBinCount = n;
```

Replace the draw call block (lines 1629-1653) with:

```cpp
    // Draw FFT spectrum
    if (m_fftFillPipeline && m_fftLinePipeline && m_visibleBinCount > 0) {
        float specVpX = static_cast<float>(specRect.x()) * dpr;
        float specVpY = static_cast<float>(h - specRect.bottom() - 1) * dpr;
        float specVpW = static_cast<float>(specRect.width()) * dpr;
        float specVpH = static_cast<float>(specRect.height()) * dpr;
        QRhiViewport specVp(specVpX, specVpY, specVpW, specVpH);

        // Fill pass
        cb->setGraphicsPipeline(m_fftFillPipeline);
        cb->setShaderResources(m_fftSrb);
        cb->setViewport(specVp);
        const QRhiCommandBuffer::VertexInput fillVbuf(m_fftFillVbo, 0);
        cb->setVertexInput(0, 1, &fillVbuf);
        cb->draw(m_visibleBinCount * 2);

        // Line pass
        cb->setGraphicsPipeline(m_fftLinePipeline);
        cb->setShaderResources(m_fftSrb);
        cb->setViewport(specVp);
        const QRhiCommandBuffer::VertexInput lineVbuf(m_fftLineVbo, 0);
        cb->setVertexInput(0, 1, &lineVbuf);
        cb->draw(m_visibleBinCount);
    }
```

- [ ] **Step 3: Build and verify**

Run:
```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/gui/SpectrumWidget.h src/gui/SpectrumWidget.cpp
git commit -m "feat: apply bin subsetting to GPU spectrum renderer"
```

---

### Task 4: Apply bin subsetting to CPU spectrum renderer

**Files:**
- Modify: `src/gui/SpectrumWidget.cpp:406-446` (drawSpectrum)

- [ ] **Step 1: Replace full-array loop with bin subset**

Replace `drawSpectrum` (lines 407-446) with:

```cpp
void SpectrumWidget::drawSpectrum(QPainter& p, const QRect& specRect)
{
    if (m_smoothed.isEmpty()) {
        return;
    }

    auto [firstBin, lastBin] = visibleBinRange(m_smoothed.size());
    int count = lastBin - firstBin + 1;
    if (count < 2) {
        return;
    }

    float xStep = static_cast<float>(specRect.width()) / static_cast<float>(count - 1);

    // Build polyline for visible bin subset
    QVector<QPointF> points(count);
    for (int j = 0; j < count; ++j) {
        float x = specRect.left() + static_cast<float>(j) * xStep;
        float y = static_cast<float>(dbmToY(m_smoothed[firstBin + j], specRect));
        points[j] = QPointF(x, y);
    }

    // Fill under the trace (if enabled)
    // From AetherSDR: fill alpha 0.70, cyan color
    if (m_panFill && count > 1) {
        QPainterPath fillPath;
        fillPath.moveTo(points.first().x(), specRect.bottom());
        for (const QPointF& pt : points) {
            fillPath.lineTo(pt);
        }
        fillPath.lineTo(points.last().x(), specRect.bottom());
        fillPath.closeSubpath();

        QColor fill = m_fillColor;
        fill.setAlphaF(m_fillAlpha * 0.4f);  // softer fill
        p.fillPath(fillPath, fill);
    }

    // Draw trace line
    // From Thetis display.cs:2184 — data_line_color = Color.White
    // We use the fill color for consistency with AetherSDR style
    QPen tracePen(m_fillColor, 1.5);
    p.setPen(tracePen);
    p.drawPolyline(points.data(), count);
}
```

- [ ] **Step 2: Build and verify**

Run:
```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/gui/SpectrumWidget.cpp
git commit -m "feat: apply bin subsetting to CPU spectrum renderer"
```

---

### Task 5: Apply bin subsetting to waterfall

**Files:**
- Modify: `src/gui/SpectrumWidget.cpp:568-587` (pushWaterfallRow)

- [ ] **Step 1: Push only visible bin subset to waterfall**

Replace `pushWaterfallRow` (lines 568-587) with:

```cpp
void SpectrumWidget::pushWaterfallRow(const QVector<float>& bins)
{
    if (m_waterfall.isNull()) {
        return;
    }

    auto [firstBin, lastBin] = visibleBinRange(bins.size());
    int subsetCount = lastBin - firstBin + 1;
    if (subsetCount <= 0) {
        return;
    }

    int h = m_waterfall.height();
    // Decrement write pointer (wrapping) — newest data at top of display
    m_wfWriteRow = (m_wfWriteRow - 1 + h) % h;

    int w = m_waterfall.width();
    QRgb* scanline = reinterpret_cast<QRgb*>(m_waterfall.scanLine(m_wfWriteRow));
    float binScale = static_cast<float>(subsetCount) / static_cast<float>(w);

    for (int x = 0; x < w; ++x) {
        int srcBin = firstBin + static_cast<int>(static_cast<float>(x) * binScale);
        srcBin = qBound(firstBin, srcBin, lastBin);
        scanline[x] = dbmToRgb(bins[srcBin]);
    }
}
```

- [ ] **Step 2: Build and verify**

Run:
```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/gui/SpectrumWidget.cpp
git commit -m "feat: apply bin subsetting to waterfall rows"
```

---

### Task 6: Move bandwidthChangeRequested to mouse release (hybrid zoom)

**Files:**
- Modify: `src/gui/SpectrumWidget.cpp:1029-1049` (drag handler)
- Modify: `src/gui/SpectrumWidget.cpp:1117-1148` (mouseReleaseEvent)
- Modify: `src/gui/SpectrumWidget.cpp:1150-1189` (wheelEvent)

- [ ] **Step 1: Remove signal emission from drag handler**

In the `m_draggingBandwidth` block (lines 1029-1049), remove line 1041:

```cpp
        emit bandwidthChangeRequested(newBw);
```

The block should now read:

```cpp
    if (m_draggingBandwidth) {
        // Zoom bandwidth by horizontal drag
        // From AetherSDR SpectrumWidget.cpp:868-876
        // Drag right = zoom out (wider), drag left = zoom in (narrower)
        int dx = mx - m_bwDragStartX;
        double factor = 1.0 + dx * 0.003;  // 0.3% per pixel
        double newBw = m_bwDragStartBw * factor;
        newBw = std::clamp(newBw, 1000.0, 768000.0);
        m_bandwidthHz = newBw;
        // Recenter on VFO when zooming so the signal stays visible
        m_centerHz = m_vfoHz;
        emit centerChanged(m_centerHz);
        updateVfoPositions();
#ifdef NEREUS_GPU_SPECTRUM
        markOverlayDirty();
#else
        update();
#endif
        return;
    }
```

- [ ] **Step 2: Emit bandwidthChangeRequested on mouse release**

In `mouseReleaseEvent` (lines 1117-1148), add bandwidth replan trigger before the reset block. After the `if (m_draggingDbm || m_draggingDivider)` block (line 1137), add:

```cpp
        // Hybrid zoom: replan FFT on drag-end for sharp resolution
        if (m_draggingBandwidth) {
            emit bandwidthChangeRequested(m_bandwidthHz);
        }
```

- [ ] **Step 3: Keep wheel zoom emitting immediately (discrete event)**

The `wheelEvent` Ctrl+scroll handler (line 1170) already emits `bandwidthChangeRequested` — this is correct for discrete scroll events. No change needed.

- [ ] **Step 4: Build and verify**

Run:
```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/gui/SpectrumWidget.cpp
git commit -m "feat: hybrid zoom — defer FFT replan to mouse release"
```

---

### Task 7: Wire DDC center and sample rate from MainWindow

**Files:**
- Modify: `src/gui/MainWindow.cpp:260-274` (wireSliceToSpectrum)
- Modify: `src/gui/MainWindow.cpp:440-463` (centerChanged handler)

- [ ] **Step 1: Set initial DDC center and sample rate in wireSliceToSpectrum**

In `wireSliceToSpectrum()` (around line 270), after `setFrequencyRange`, add:

```cpp
    m_spectrumWidget->setDdcCenterFrequency(freq);
    m_spectrumWidget->setSampleRate(768000.0);
```

- [ ] **Step 2: Update DDC center when hardware frequency changes**

In the `centerChanged` lambda (lines 441-463), after `forceHardwareFrequency` (line 452), add:

```cpp
                m_spectrumWidget->setDdcCenterFrequency(centerHz);
```

Also in the `frequencyChanged` lambda (around line 312), after `forceHardwareFrequency`, add:

```cpp
                m_spectrumWidget->setDdcCenterFrequency(freq);
```

- [ ] **Step 3: Replace hardcoded 768000.0 in zoom slider with sample rate from FFTEngine**

In the zoom slider handler (lines 134-138), no change needed — `setFrequencyRange` doesn't need `m_ddcCenterHz`. The `bandwidthChangeRequested` handler (line 178) still uses hardcoded 768000.0 for FFT size calculation, which is correct since FFT always processes the full DDC bandwidth regardless of display zoom.

- [ ] **Step 4: Build and verify**

Run:
```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/gui/MainWindow.cpp
git commit -m "feat: wire DDC center frequency to SpectrumWidget"
```

---

### Task 8: Clamp zoom bandwidth to DDC sample rate

**Files:**
- Modify: `src/gui/SpectrumWidget.cpp:1036,1165` (bandwidth clamp sites)

- [ ] **Step 1: Use m_sampleRateHz instead of hardcoded 768000.0**

In the `m_draggingBandwidth` handler (line 1036), change:

```cpp
        newBw = std::clamp(newBw, 1000.0, 768000.0);
```

to:

```cpp
        newBw = std::clamp(newBw, 1000.0, m_sampleRateHz);
```

In the `wheelEvent` Ctrl+scroll handler (line 1165), change:

```cpp
        newBw = std::clamp(newBw, 1000.0, 768000.0);
```

to:

```cpp
        newBw = std::clamp(newBw, 1000.0, m_sampleRateHz);
```

- [ ] **Step 2: Build and verify**

Run:
```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/gui/SpectrumWidget.cpp
git commit -m "refactor: use m_sampleRateHz instead of hardcoded 768000"
```

---

### Task 9: Integration test — build, launch, verify zoom behavior

**Files:** None (manual verification)

- [ ] **Step 1: Full clean build**

Run:
```bash
cmake --build build --clean-first -j$(nproc) 2>&1 | tail -10
```
Expected: Build succeeds with no warnings in modified files.

- [ ] **Step 2: Launch and test zoom**

Run:
```bash
pkill -f NereusSDR; ./build/NereusSDR &
```

Manual verification checklist:
1. Drag frequency scale bar left (zoom in) — spectrum trace should narrow and signals grow wider
2. Drag frequency scale bar right (zoom out) — spectrum should widen back
3. Ctrl+scroll to zoom — same visual effect
4. During drag: display should zoom smoothly (no hitches from FFT replan)
5. After drag release: resolution should sharpen as FFT replans
6. Waterfall: new rows should render at zoomed scale; old rows preserved
7. VFO marker and filter passband overlay should stay aligned with the spectrum
8. At maximum zoom-out (768 kHz): display should look identical to pre-change behavior (all bins visible)

- [ ] **Step 3: Commit docs**

```bash
git add docs/architecture/ctun-zoom-design.md docs/architecture/ctun-zoom-plan.md
git commit -m "docs: add CTUN zoom bin subsetting design and plan"
```
