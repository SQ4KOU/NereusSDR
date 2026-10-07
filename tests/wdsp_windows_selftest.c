/*
 * Windows-only linked WDSP 2.10 receive-path self-test.
 *
 * Deliberately has no NereusCore, Qt, radio protocol, mixer or PortAudio
 * dependency. It links directly to the same wdsp_static + FFTW used by the
 * Windows application and feeds a deterministic complex tone through a real
 * RXA channel. The WDSP RX trace tap checks every observed block at:
 *   fexchange2 input -> rxa.outbuff after xrxa -> r2 write -> fexchange2 out.
 *
 * Independently implemented from RXA.h interface; this file contains no
 * ported Thetis implementation code.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fftw3.h>

#include "channel.h"
#include "RXA.h"
#include "bandpass.h"
#include "dsplock.h"
#include "iobuffs.h"
#include "nbp.h"
#include "patchpanel.h"
#include "wcpAGC.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum {
    TRACE_FEX_IN = 1,
    TRACE_RXA_OUT = 2,
    TRACE_R2_WRITE = 3,
    TRACE_FEX_OUT = 4,
    TRACE_AFTER_HB = 5,
    TRACE_AFTER_NBP = 6,
    TRACE_AFTER_DEMOD = 7,
    TRACE_AFTER_NR0 = 8,
    TRACE_AFTER_BP0 = 9,
    TRACE_AFTER_AGC = 10,
    TRACE_AFTER_BP1 = 11,
    TRACE_AFTER_PANEL = 12,
    TRACE_AFTER_RSMP_OUT = 13,
    TRACE_STAGE_COUNT = 14
};

enum {
    TRACE_FLOAT_SPLIT = 1,
    TRACE_DOUBLE_INTERLEAVED = 2
};

typedef struct {
    unsigned long long blocks;
    unsigned long long checked;
    unsigned long long failures;
    double baseline_rms;
    double min_rms;
    double max_rms;
    double max_peak;
    double max_period_error;
} StageStats;

static StageStats g_stage[TRACE_STAGE_COUNT];
static int g_input_period = 48;
static const int g_output_period = 48;
static const double g_input_amplitude = 0.05;

/* dsp_outsize is 4096 in both cases below. dexchange() must copy the prior
   rxa.outbuff into r2 bit-for-bit before xrxa() produces the next block. */
static double g_previous_rxa_out[2 * 4096];
static int g_previous_rxa_count = 0;
static int g_previous_rxa_valid = 0;

static const char* stage_name(int stage)
{
    switch (stage) {
    case TRACE_FEX_IN: return "fexchange2.in";
    case TRACE_RXA_OUT: return "rxa.out.after_xrxa";
    case TRACE_R2_WRITE: return "r2.write";
    case TRACE_FEX_OUT: return "fexchange2.out";
    case TRACE_AFTER_HB: return "xrxa.after_input_resampler";
    case TRACE_AFTER_NBP: return "xrxa.after_nbp";
    case TRACE_AFTER_DEMOD: return "xrxa.after_demod";
    case TRACE_AFTER_NR0: return "xrxa.after_nr_position0";
    case TRACE_AFTER_BP0: return "xrxa.after_bandpass_pos0";
    case TRACE_AFTER_AGC: return "xrxa.after_agc";
    case TRACE_AFTER_BP1: return "xrxa.after_bandpass_pos1";
    case TRACE_AFTER_PANEL: return "xrxa.after_panel";
    case TRACE_AFTER_RSMP_OUT: return "xrxa.after_output_resampler";
    default: return "unknown";
    }
}

static unsigned long long warmup_blocks(int stage)
{
    switch (stage) {
    case TRACE_FEX_IN: return 2;
    case TRACE_RXA_OUT: return 10;
    case TRACE_R2_WRITE: return 11; /* r2 is one worker block behind xrxa */
    case TRACE_FEX_OUT: return 768; /* 64 initial ring blocks + >10 RXA blocks */
    case TRACE_AFTER_HB:
    case TRACE_AFTER_NBP:
    case TRACE_AFTER_DEMOD:
    case TRACE_AFTER_NR0:
    case TRACE_AFTER_BP0:
    case TRACE_AFTER_AGC:
    case TRACE_AFTER_BP1:
    case TRACE_AFTER_PANEL:
    case TRACE_AFTER_RSMP_OUT:
        return 10;
    default: return 0;
    }
}

static void fail_block(int stage, unsigned long long seq, const char* why,
                       double rms, double peak, double perr)
{
    StageStats* s = &g_stage[stage];
    ++s->failures;
    if (s->failures <= 12) {
        fprintf(stderr,
                "WDSP_SELFTEST FAIL stage=%s seq=%llu reason=%s "
                "rms=%.9g peak=%.9g period_err=%.9g\n",
                stage_name(stage), seq, why, rms, peak, perr);
    }
}

static void rx_trace_hook(int channel, int stage, int layout,
                          const void* data0, const void* data1, int count)
{
    StageStats* s;
    unsigned long long seq;
    double sum_sq = 0.0;
    double peak = 0.0;
    double err_sq = 0.0;
    double ref_sq = 0.0;
    double rms;
    double period_error = 0.0;
    int period;
    int nonfinite = 0;
    int n;

    (void)channel;
    if (stage <= 0 || stage >= TRACE_STAGE_COUNT || data0 == NULL || count <= 0)
        return;

    s = &g_stage[stage];
    seq = ++s->blocks;
    period = (stage == TRACE_FEX_IN) ? g_input_period : g_output_period;

    if (layout == TRACE_FLOAT_SPLIT && data1 != NULL) {
        const float* i = (const float*)data0;
        const float* q = (const float*)data1;
        for (n = 0; n < count; ++n) {
            const double vi = i[n];
            const double vq = q[n];
            if (!isfinite(vi) || !isfinite(vq)) {
                ++nonfinite;
                continue;
            }
            sum_sq += vi * vi + vq * vq;
            if (fabs(vi) > peak) peak = fabs(vi);
            if (fabs(vq) > peak) peak = fabs(vq);
        }
        if (count > period) {
            for (n = period; n < count; ++n) {
                const double di = (double)i[n] - (double)i[n - period];
                const double dq = (double)q[n] - (double)q[n - period];
                err_sq += di * di + dq * dq;
                ref_sq += (double)i[n] * i[n] + (double)q[n] * q[n];
            }
        }
    } else if (layout == TRACE_DOUBLE_INTERLEAVED) {
        const double* iq = (const double*)data0;
        for (n = 0; n < count * 2; ++n) {
            const double v = iq[n];
            if (!isfinite(v)) {
                ++nonfinite;
                continue;
            }
            sum_sq += v * v;
            if (fabs(v) > peak) peak = fabs(v);
        }
        if (count > period) {
            for (n = period; n < count; ++n) {
                const double di = iq[2*n]     - iq[2*(n-period)];
                const double dq = iq[2*n + 1] - iq[2*(n-period) + 1];
                err_sq += di * di + dq * dq;
                ref_sq += iq[2*n] * iq[2*n] + iq[2*n + 1] * iq[2*n + 1];
            }
        }
    } else {
        fail_block(stage, seq, "bad-layout", 0.0, 0.0, 0.0);
        return;
    }

    rms = sqrt(sum_sq / (2.0 * count));
    if (ref_sq > 1.0e-30)
        period_error = sqrt(err_sq / ref_sq);

    /* RXA_OUT from worker block N must be exactly the block dexchange writes
       to r2 at the start of worker block N+1. Both callbacks execute on that
       same WDSP worker, so this comparison adds no cross-thread test state. */
    if (stage == TRACE_R2_WRITE && g_previous_rxa_valid) {
        const double* iq = (const double*)data0;
        if (layout != TRACE_DOUBLE_INTERLEAVED
            || count != g_previous_rxa_count
            || memcmp(iq, g_previous_rxa_out,
                      (size_t)(2 * count) * sizeof(double)) != 0) {
            fail_block(stage, seq, "rxa-to-r2-copy-mismatch",
                       rms, peak, period_error);
        }
    }

    /* NaN/Inf is never a settling transient. Check it from the first sample
       of the first block; warm-up below applies only to amplitude/periodicity
       while the RXA filter and AGC settle. */
    if (nonfinite != 0) {
        fail_block(stage, seq, "nan-or-inf", rms, peak, period_error);
        return;
    }

    if (stage == TRACE_RXA_OUT && layout == TRACE_DOUBLE_INTERLEAVED) {
        if (count > 4096) {
            fail_block(stage, seq, "unexpected-rxa-block-size",
                       rms, peak, period_error);
            g_previous_rxa_valid = 0;
        } else {
            memcpy(g_previous_rxa_out, data0,
                   (size_t)(2 * count) * sizeof(double));
            g_previous_rxa_count = count;
            g_previous_rxa_valid = 1;
        }
    }

    if (seq <= warmup_blocks(stage))
        return;

    ++s->checked;
    if (s->checked == 1) {
        s->baseline_rms = rms;
        s->min_rms = rms;
        s->max_rms = rms;
    } else {
        if (rms < s->min_rms) s->min_rms = rms;
        if (rms > s->max_rms) s->max_rms = rms;
    }
    if (peak > s->max_peak) s->max_peak = peak;
    if (period_error > s->max_period_error)
        s->max_period_error = period_error;

    if (stage == TRACE_FEX_IN) {
        if (peak < 0.049 || peak > 0.051)
            fail_block(stage, seq, "input-amplitude", rms, peak, period_error);
    } else {
        if (rms < 1.0e-7)
            fail_block(stage, seq, "silent", rms, peak, period_error);
        if (peak > 16.0)
            fail_block(stage, seq, "amplitude-runaway", rms, peak, period_error);
        if (s->baseline_rms > 1.0e-7
            && (rms < s->baseline_rms * 0.25 || rms > s->baseline_rms * 4.0))
            fail_block(stage, seq, "block-amplitude-step", rms, peak, period_error);
    }

    /* A stable 1 kHz synthetic tone repeats every 48 output samples.
       Filter/AGC settling is excluded by the per-stage warmup above. */
    if (period_error > 0.20)
        fail_block(stage, seq, "periodic-pattern", rms, peak, period_error);
}

static int validate_case(int input_rate)
{
    const int channel = 20;
    const int dsp_rate = 48000;
    const int output_rate = 48000;
    const int dsp_size = 4096;
    const int in_size = 64 * (input_rate / output_rate);
    const int exchanges = 64 * 48;
    float* in_i = NULL;
    float* in_q = NULL;
    float* out_i = NULL;
    float* out_q = NULL;
    double phase = 0.0;
    const double step = +2.0 * M_PI * 1000.0 / (double)input_rate;
    int error = 0;
    int failures = 0;
    int k, n;

    if (input_rate % output_rate != 0 || in_size <= 0) {
        fprintf(stderr, "WDSP_SELFTEST invalid input rate %d\n", input_rate);
        return 1;
    }

    memset(g_stage, 0, sizeof(g_stage));
    g_previous_rxa_count = 0;
    g_previous_rxa_valid = 0;
    memset(g_previous_rxa_out, 0, sizeof(g_previous_rxa_out));
    g_input_period = input_rate / 1000;

    in_i = (float*)calloc((size_t)in_size, sizeof(float));
    in_q = (float*)calloc((size_t)in_size, sizeof(float));
    out_i = (float*)calloc(64u, sizeof(float));
    out_q = (float*)calloc(64u, sizeof(float));
    if (!in_i || !in_q || !out_i || !out_q) {
        fprintf(stderr, "WDSP_SELFTEST allocation failed\n");
        failures = 1;
        goto done;
    }

    printf("WDSP_SELFTEST case input_rate=%d in_size=%d dsp_size=%d tone_hz=+1000 lsb_passband=-2850..-150\n",
           input_rate, in_size, dsp_size);

    WDSPSetRxTraceHook(rx_trace_hook);
    OpenChannel(channel, in_size, dsp_size,
                input_rate, dsp_rate, output_rate,
                0, 0, 0.010, 0.025, 0.000, 0.010, 1);

    SetRXAMode(channel, RXA_LSB);
    SetRXABandpassFreqs(channel, -2850.0, -150.0);
    RXANBPSetFreqs(channel, -2850.0, -150.0);
    SetRXAAGCMode(channel, 3);
    SetRXAAGCTop(channel, 80.0);
    SetRXAPanelGain1(channel, 1.0);
    SetRXAPanelBinaural(channel, 0);
    SetChannelState(channel, 1, 0);

    for (k = 0; k < exchanges; ++k) {
        for (n = 0; n < in_size; ++n) {
            in_i[n] = (float)(g_input_amplitude * cos(phase));
            in_q[n] = (float)(g_input_amplitude * sin(phase));
            phase += step;
            if (phase < -M_PI)
                phase += 2.0 * M_PI;
        }

        error = 0;
        fexchange2(channel, in_i, in_q, out_i, out_q, &error);
        if (error != 0) {
            fprintf(stderr,
                    "WDSP_SELFTEST FAIL fexchange2 error=%d exchange=%d rate=%d\n",
                    error, k, input_rate);
            ++failures;
            break;
        }
    }

    /* Close waits for the WDSP worker, so the stage counters are stable
       before we inspect them. */
    SetChannelState(channel, 0, 0);
    CloseChannel(channel);
    WDSPSetRxTraceHook(NULL);

    for (n = TRACE_FEX_IN; n < TRACE_STAGE_COUNT; ++n) {
        const StageStats* s = &g_stage[n];
        printf("WDSP_SELFTEST stage=%s blocks=%llu checked=%llu failures=%llu "
               "rms_min=%.9g rms_max=%.9g peak_max=%.9g period_err_max=%.9g\n",
               stage_name(n), s->blocks, s->checked, s->failures,
               s->min_rms, s->max_rms, s->max_peak, s->max_period_error);
        failures += (int)s->failures;
    }

    if (g_stage[TRACE_FEX_IN].blocks != (unsigned long long)exchanges
        || g_stage[TRACE_FEX_OUT].blocks != (unsigned long long)exchanges) {
        fprintf(stderr, "WDSP_SELFTEST FAIL missing fexchange trace blocks\n");
        ++failures;
    }
    if (g_stage[TRACE_RXA_OUT].checked < 20
        || g_stage[TRACE_R2_WRITE].checked < 20) {
        fprintf(stderr, "WDSP_SELFTEST FAIL too few checked worker blocks\n");
        ++failures;
    }

done:
    free(in_i);
    free(in_q);
    free(out_i);
    free(out_q);
    return failures != 0;
}

int main(void)
{
    int failed = 0;

    /* Same double-precision planner synchronization primitive used by the
       Windows application, but no application code is linked here. */
    fftw_make_planner_thread_safe();

    failed |= validate_case(48000);
    failed |= validate_case(192000);

    if (failed) {
        fprintf(stderr, "WDSP_SELFTEST RESULT=FAIL\n");
        return 1;
    }

    printf("WDSP_SELFTEST RESULT=PASS\n");
    return 0;
}
