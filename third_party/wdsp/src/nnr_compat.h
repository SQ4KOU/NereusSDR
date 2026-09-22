/* NereusSDR NNR control/readback boundary.
 * Copyright (C) 2026 J.J. Boyd, KG4VCF
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
// no-port-check: NereusSDR-original ABI glue. DSP algorithms remain in nnr.c.
// 2026-09-21: J.J. Boyd (KG4VCF), with OpenAI Codex assistance.
#ifndef NEREUS_NNR_COMPAT_H
#define NEREUS_NNR_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NNRConfiguration {
    int model_slot;
    int position;
    double mask_floor_db;
    double alpha;
    double alpha_knee_db;
    double tau_seconds;
    double max_gain_db;
    double attack_ms;
    double release_ms;
} NNRConfiguration;

typedef struct NNRRuntimeStatus {
    NNRConfiguration configuration;
    int ready;
    int running;
    int rate_supported;
    int model_available[2];
    int model_source[2]; /* 0 unavailable, 1 bundled, 2 file */
    int dsp_rate_hz;
    int network_rate_hz;
    int delay_samples;
    int test_mode;
    int output_mode;
    int profiling_available;
} NNRRuntimeStatus;

/* Caller owns the channel lifetime. These never retain an output pointer.
 * Return 1 for a valid copy/accepted operation, 0 for invalid/unavailable.
 * Configuration is applied under one DSP lock; refusal applies no fields. */
int GetRXANNRStatus(int channel, NNRRuntimeStatus* out);
int ConfigureRXANNR(int channel, const NNRConfiguration* requested,
                   NNRRuntimeStatus* accepted);
int SetRXANNRDiagnostics(int channel, int test_mode, int output_mode);

#ifdef __cplusplus
}
#endif
#endif
