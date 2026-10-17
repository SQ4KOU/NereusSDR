/* NereusSDR-original. Internal setup checks for pinned PortAudio v19.7.0
 * (147dd722548358763a8b649b3e4b41dfffbcfbb6).
 *
 * pa_process.c:249-284,319-354 allocates through the signed-long API in
 * pa_util.h:121; offsets at999/1142 first multiply unsigned int fields.
 * pa_linux_alsa.c:3542-3566 uses signed-int per-frame byte offsets.
 * Check the existing representation domains before calculating products.
 *
 * Modification history (NereusSDR):
 * 2026-10-14: J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex:
 * reject unrepresentable setup sizes without changing valid audio values.
 */
#pragma once

#include <limits.h>
#include <stddef.h>

static inline int nereusPaBufferSizesFit(unsigned long frames, int sampleBytes,
                                       int channels, unsigned long descriptorBytes,
                                       int nonInterleaved, unsigned long allocationLimit)
{
    unsigned long count;
    unsigned long frameBytes;
    if (sampleBytes <= 0 || channels <= 0 || descriptorBytes == 0) {
        return 0;
    }
    if ((unsigned int)channels > UINT_MAX / (unsigned int)sampleBytes) {
        return 0;
    }
    count = (unsigned long)channels;
    frameBytes = count * (unsigned long)sampleBytes;
    if (frames > allocationLimit / frameBytes) {
        return 0;
    }
    if (count > allocationLimit / descriptorBytes / 2) {
        return 0;
    }
    if (nonInterleaved) {
        if (count > allocationLimit / sizeof(void*) ||
            frames > UINT_MAX / (unsigned int)sampleBytes) {
            return 0;
        }
    }
    return 1;
}

static inline int nereusPaHostChannelBytesFit(int hostChannels, int userChannels,
                                            int sampleBytes)
{
    if (sampleBytes <= 0 || userChannels <= 0 || hostChannels < userChannels) {
        return 0;
    }
    return hostChannels <= INT_MAX / sampleBytes;
}
