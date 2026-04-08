# Multi-Panadapter Layout Architecture

**Status:** Phase 2B — Design pending

## Overview

NereusSDR supports up to 4 independent panadapters, each with its own
spectrum display, waterfall, and associated slice(s).

## Design Goals

- Composable, independent panadapter widgets
- Support up to 4 panadapters simultaneously
- Independent state per pan (VFO, mode, filters, zoom)
- Shared radio connection across all pans
- Multiple layout configurations (1-up, 2-up, 2x2, etc.)
- User-resizable and reconfigurable at runtime

## Layout Configurations

1. **1-up** — Single full-width panadapter
2. **2-up side-by-side** — Two pans horizontally
3. **2-up stacked** — Two pans vertically
4. **2x2 grid** — Four equal pans
5. **1 wide + 2 stacked** — Large pan top, two smaller below
6. **Custom** — User-defined split ratios

## Key Difference from AetherSDR

In AetherSDR, the radio computes FFT/waterfall data and streams it to
each panadapter. In NereusSDR, the client computes FFT from raw I/Q,
so adding more panadapters increases client CPU/GPU load.
