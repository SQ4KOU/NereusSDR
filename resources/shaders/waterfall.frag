#version 440

// =================================================================
// resources/shaders/waterfall.frag  (NereusSDR)
// =================================================================
//
// Ported from AetherSDR (ten9876/AetherSDR, GPLv3) shader source:
//   resources/shaders/texturedquad.frag — ring-buffer UV-offset
//   sampling pattern (line 19 below). AetherSDR has no per-file
//   shader headers; project-level citation per
//   docs/attribution/HOW-TO-PORT.md rule 6.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in GLSL 440 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code. The texture sampler binding, std140
//                 uniform layout, and rowOffset uniform are NereusSDR
//                 additions on top of AetherSDR's UV-fract pattern.
//   2026-10-11 - The sample row is held between the centers of the newest
//                 and the oldest row, so neither the sub-row glide nor the
//                 linear filter reaches across the ring's seam; rowOffset
//                 became newestRow, glideRows and ringRows. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-11 - Ported AetherSDR's four-row cubic B-spline reconstruction
//                 and its clamp of each tap to the rows held, from
//                 resources/shaders/texturedquad.frag [@a9a83e66] (added
//                 there by aa9336ae and e5cd51cd), in place of the two-row
//                 linear mix: a noisy waterfall no longer pulses as it
//                 glides. Kept NereusSDR's uniforms; AetherSDR's floor
//                 color row is not taken. J.J. Boyd (KG4VCF), AI-assisted
//                 via Anthropic Claude Code.
// =================================================================

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D tex;

layout(std140, binding = 0) uniform Uniforms {
    float newestRow;  // ring row that holds the newest waterfall row
    float glideRows;  // rows the picture has still to glide down, 1 to 0
    float ringRows;   // rows in the ring texture
    float padding3;
};

void main()
{
    // Apply ring buffer offset per-pixel (not per-vertex, to avoid fract() interpolation issue)
    // From AetherSDR texturedquad.frag
    //
    // The row to sample, counted down from the newest row: 0.0 is that row's
    // center. The glide wants one row more below the oldest than the ring
    // holds, and a blend of neighboring rows wants more again at each end.
    // Left to wrap, they took the rows from the other end of the ring: the
    // bottom line of the waterfall flashed each new row as it arrived, and
    // where a row is two screen pixels tall the top line carried a quarter
    // of the oldest row. Every tap below is held to the rows the ring has
    // before it is mapped into the ring, so the edge lines repeat their own
    // row instead.
    //
    // From AetherSDR texturedquad.frag [@a9a83e66]: the reconstruction and
    // the clamp of each tap. AetherSDR passes the ring origin, the glide and
    // one row's height in texture units; here they come as rows (newestRow,
    // glideRows, ringRows), so the same position reads
    // v_uv.y * rows + glideRows - 0.5 and a tap maps into the ring with
    // mod() on whole rows. Each tap is taken at its row's center, so the
    // sampler's own linear filter adds nothing down the picture.
    float rows = max(ringRows, 1.0);
    float logicalTexel = v_uv.y * rows + glideRows - 0.5;
    float base = floor(logicalTexel);
    float f = fract(logicalTexel);
    float f2 = f * f;
    float f3 = f2 * f;

    // Cubic B-spline reconstruction keeps a stable vertical frequency response
    // at every sub-row phase. Bilinear filtering alternates between a sharp row
    // and a 50/50 blend, which makes noisy waterfalls appear to pulse.
    float w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
    float w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
    float w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0;
    float w3 = f3 / 6.0;
    float y0 = (mod(newestRow + clamp(base - 1.0, 0.0, rows - 1.0), rows) + 0.5) / rows;
    float y1 = (mod(newestRow + clamp(base,       0.0, rows - 1.0), rows) + 0.5) / rows;
    float y2 = (mod(newestRow + clamp(base + 1.0, 0.0, rows - 1.0), rows) + 0.5) / rows;
    float y3 = (mod(newestRow + clamp(base + 2.0, 0.0, rows - 1.0), rows) + 0.5) / rows;
    fragColor =
          texture(tex, vec2(v_uv.x, y0)) * w0
        + texture(tex, vec2(v_uv.x, y1)) * w1
        + texture(tex, vec2(v_uv.x, y2)) * w2
        + texture(tex, vec2(v_uv.x, y3)) * w3;
}
