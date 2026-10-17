/* NereusSDR-original. Device-free setup validation; never allocates audio
 * buffers, initializes PortAudio, enumerates devices or invokes callbacks.
 * Removing a guard must fail the corresponding rejected-size assertion.
 */
#include "../../cmake/patches/portaudio-bounds.h"

#include <limits.h>
#include <stdio.h>

static int failures = 0;

static void expect(int actual, int wanted, const char *description)
{
    if (actual != wanted) {
        fprintf(stderr, "FAIL: %s (got %d, wanted %d)\n", description, actual, wanted);
        ++failures;
    }
}

int main(void)
{
    /* LP64 host still checks the exact LLP64 allocator boundary. These
     * expectations are hand-derived from 2^31-1 / (4 bytes * 2 channels).
     */
    const unsigned long winLongMax = 2147483647UL;
    expect(nereusPaBufferSizesFit(128, 4, 2, 16, 0, winLongMax), 1,
           "default stereo buffer remains valid");
    expect(nereusPaBufferSizesFit(256, 4, 2, 16, 0, winLongMax), 1,
           "host-selected frames for unspecified user buffer remain valid");
    expect(nereusPaBufferSizesFit(268435455UL, 4, 2, 16, 0, winLongMax), 1,
           "last stereo frame count within signed-long32 allocation");
    expect(nereusPaBufferSizesFit(268435456UL, 4, 2, 16, 0, winLongMax), 0,
           "first stereo frame count beyond signed-long32 allocation");
    expect(nereusPaBufferSizesFit(ULONG_MAX, 4, 2, 16, 0, winLongMax), 0,
           "unsigned frame product cannot wrap before allocation check");
    expect(nereusPaBufferSizesFit(1, 4, 2, 16, 0, 63), 0,
           "two channel-descriptor blocks must fit allocator");
    expect(nereusPaBufferSizesFit(1, 4, 2, 16, 0, 64), 1,
           "exact channel-descriptor allocation fits");
    /* Two one-byte descriptors per channel need only 4 bytes. On the
     * supported 4/8-byte pointer platforms, both limits below fit those
     * descriptors, so only the rejected pointer-table boundary can fail.
     */
    const unsigned long pointerTableBytes = 2UL * sizeof(void*);
    expect(nereusPaBufferSizesFit(1, 1, 2, 1, 1, pointerTableBytes), 1,
           "exact noninterleaved channel-pointer table allocation fits");
    expect(nereusPaBufferSizesFit(1, 1, 2, 1, 1, pointerTableBytes - 1), 0,
           "noninterleaved channel-pointer table beyond allocation limit");
    expect(nereusPaBufferSizesFit(1, 4, 0, 16, 0, winLongMax), 0,
           "active side requires positive channels");
    expect(nereusPaBufferSizesFit(1, 0, 2, 16, 0, winLongMax), 0,
           "sample size must be positive");
    expect(nereusPaBufferSizesFit(1, 4, -1, 16, 0, winLongMax), 0,
           "negative channel count cannot become unsigned");

#if ULONG_MAX > 4294967295UL && UINT_MAX == 4294967295U
    expect(nereusPaBufferSizesFit(1, 4, 1073741824, 16, 0, ULONG_MAX), 0,
           "unsigned-int sample-byte/channel intermediate must fit");
    expect(nereusPaBufferSizesFit(1073741824UL, 4, 1, 16, 1, ULONG_MAX), 0,
           "noninterleaved unsigned-int channel byte stride must fit");
#endif

    expect(nereusPaHostChannelBytesFit(2, 1, 4), 1,
           "ordinary interleaved mono-to-stereo layout remains valid");
    expect(nereusPaHostChannelBytesFit(INT_MAX / 4, 1, 4), 1,
           "last signed-int native channel-byte layout fits");
    expect(nereusPaHostChannelBytesFit(INT_MAX / 4 + 1, 1, 4), 0,
           "first signed-int native channel-byte layout beyond limit");
    expect(nereusPaHostChannelBytesFit(1, 2, 4), 0,
           "host layout cannot contain fewer than requested channels");
    expect(nereusPaHostChannelBytesFit(2, 0, 4), 0,
           "host layout requires positive user channels");
    expect(nereusPaHostChannelBytesFit(2, 1, 0), 0,
           "native byte width zero is invalid");
    expect(nereusPaHostChannelBytesFit(2, 1, -1), 0,
           "native byte width error is invalid");

    if (failures != 0) {
        return 1;
    }
    puts("PortAudio setup representation boundaries passed (device-free)");
    return 0;
}
