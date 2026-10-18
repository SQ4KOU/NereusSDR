/* NereusSDR-original. Exercise the real patched buffer initializer with
 * ordinary small buffers and format errors. Never initialize PortAudio,
 * enumerate devices, open a stream, invoke callbacks or request large buffers.
 */
#include "pa_process.h"

#include <stdio.h>

static int callback(const void *input, void *output, unsigned long frames,
                    const PaStreamCallbackTimeInfo *timeInfo,
                    PaStreamCallbackFlags flags, void *data)
{
    (void)input; (void)output; (void)frames; (void)timeInfo; (void)flags; (void)data;
    return paContinue;
}

int main(void)
{
    PaUtilBufferProcessor processor;
    PaError result = PaUtil_InitializeBufferProcessor(&processor,
        0, 0, 0, 2, paFloat32, paFloat32, 48000, 0,
        paFramesPerBufferUnspecified, 256, paUtilFixedHostBufferSize,
        callback, NULL);
    if (result != paNoError) {
        fprintf(stderr, "unspecified user buffer rejected: %d\n", result);
        return 1;
    }
    if (processor.framesPerUserBuffer != 0 || processor.framesPerTempBuffer != 256 ||
        processor.bytesPerUserOutputSample != 4 || processor.tempOutputBuffer == NULL) {
        fprintf(stderr, "unspecified user buffer did not retain selected host frames\n");
        PaUtil_TerminateBufferProcessor(&processor);
        return 1;
    }
    PaUtil_TerminateBufferProcessor(&processor);

    result = PaUtil_InitializeBufferProcessor(&processor,
        2, paFloat32, paFloat32, 0, 0, 0, 48000, 0,
        128, 256, paUtilFixedHostBufferSize, callback, NULL);
    if (result != paNoError || processor.framesPerTempBuffer != 128) {
        fprintf(stderr, "ordinary configured capture buffer rejected: %d\n", result);
        if (result == paNoError) {
            PaUtil_TerminateBufferProcessor(&processor);
        }
        return 1;
    }
    PaUtil_TerminateBufferProcessor(&processor);

    /* Unsupported output format must preserve the sample-format error.
     * The input side must not allocate before the output preflight fails.
     */
    result = PaUtil_InitializeBufferProcessor(&processor,
        2, paFloat32, paFloat32, 2, paCustomFormat, paFloat32, 48000, 0,
        128, 256, paUtilFixedHostBufferSize, callback, NULL);
    if (result != paSampleFormatNotSupported || processor.tempInputBuffer != NULL ||
        processor.tempOutputBuffer != NULL || processor.hostInputChannels[0] != NULL ||
        processor.hostOutputChannels[0] != NULL) {
        fprintf(stderr, "both-side format preflight/error contract failed: %d\n", result);
        /* An initializer returning an error already owns its error cleanup. */
        if (result == paNoError) {
            PaUtil_TerminateBufferProcessor(&processor);
        }
        return 1;
    }
    PaUtil_TerminateBufferProcessor(&processor);
    puts("Real PortAudio buffer setup preserved host selection and error cleanup (device-free)");
    return 0;
}
