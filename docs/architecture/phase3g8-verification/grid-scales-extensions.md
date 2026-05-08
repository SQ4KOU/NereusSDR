# Grid & Scales Extensions — v0.3.0

Added in Task 2.9 (Section 2E of the Thetis Display + DSP parity audit). Extends
the Grid & Scales setup page with three NF-aware grid tracking controls (ported
from Thetis `setup.cs:24202–24213 [v2.10.3.13]` `chkAdjustGridMinToNFRX1`) and
a one-shot copy button.

## How to use

Open **Settings → Display → Grid & Scales**, scroll to the NF-tracking section.
Screenshot prefix convention: `GS-` for Grid & Scales extensions.

---

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| GS-1 | Adjust grid min to noise floor | `DisplayAdjustGridMinToNoiseFloor` | False | Grid dB Min (bottom of display) automatically tracks the estimated noise floor | `gs-adjust-grid-min-on.png` |
| GS-2 | NF grid follow offset (dB) | `DisplayNFOffsetGridFollow` | 0 | Change to −20: grid minimum is placed 20 dB below the noise floor estimate | `gs-nf-offset-grid.png` |
| GS-3 | Maintain NF adjust delta | `DisplayMaintainNFAdjustDelta` | False | Preserves the offset between noise floor and grid min when the noise floor estimate changes, preventing grid jumps | `gs-maintain-nf-delta.png` |
| GS-4 | Copy waterfall min/max → spectrum (button) | *(no AppSettings — one-shot action)* | N/A | Copies waterfall Low/High threshold spinboxes into the spectrum grid Min/Max spinboxes | `gs-copy-waterfall-to-spectrum.png` |

## Notes

- GS-2 range: −60 to +60 dB.
- GS-1 and GS-3 are interdependent: GS-3 has no effect unless GS-1 is enabled.
  Verify GS-3 sub-controls are disabled when GS-1 is off (enabled-state gating
  is applied in the constructor after `loadFromRenderer()`).
- GS-4 is a one-shot copy; verify the spectrum dB Max/Min spinboxes update and
  the band grid slot for the current band persists across a restart.
