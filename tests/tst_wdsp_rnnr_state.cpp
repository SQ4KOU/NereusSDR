// no-port-check: NereusSDR-original state regression, authored by J.J. Boyd
// (KG4VCF), with OpenAI Codex assistance. The target compiles production
// rnnr.c unchanged; only its external dependencies below are test doubles.
#include <QtGlobal>

#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

extern "C" {
#include "comm.h"
void RNNRloadModel(const char* filePath);
}

// The vendor platform header defines these for C sources.
#undef min
#undef max
#undef EnterCriticalSection

// Stable arena objects deliberately stay allocated until process exit.
// Destroy marks an object inactive; it never frees a model or state.
struct DenoiseState {
    bool active = false;
};
struct RNNModel {};

namespace {

constexpr int kFrameSize = 4; // A small synthetic RNNoise frame, not a DSP claim.
constexpr int kRate = 48000;
constexpr int kPosition = 1;
std::array<DenoiseState, 32> g_states;
std::size_t g_nextState = 0;
RNNModel g_model;
bool g_failCreate = false;
int g_processCalls = 0;
int g_unavailableProcessCalls = 0;
std::array<RNNR, 2> g_observedInstances{};
int g_observedCount = 0;
int g_modelObservations = 0;
bool g_unpublishedDuringLoad = true;

LPCRITICAL_SECTION g_watchedLock = nullptr;
std::mutex g_admissionMutex;
std::condition_variable g_admissionChanged;
bool g_attemptedAdmission = false;
int g_failures = 0;

void check(bool condition, const char* description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    }
}

void platformEnter(LPCRITICAL_SECTION cs)
{
#if defined(Q_OS_WIN)
    ::EnterCriticalSection(cs);
#else
    pthread_mutex_lock(cs);
#endif
}

using Instance = std::unique_ptr<rnnr, decltype(&destroy_rnnr)>;
using Samples = std::array<double, 2 * kFrameSize>;

Samples inputSamples()
{
    return {0.000001, 0.25, -0.000002, -0.5,
            0.000003, 0.75, -0.000004, -1.0};
}

Instance instance(int run, Samples& input, Samples& output)
{
    return Instance(create_rnnr(run, kPosition, kFrameSize,
                               input.data(), output.data(), kRate), &destroy_rnnr);
}

void resetProcessObservations()
{
    g_processCalls = 0;
    g_unavailableProcessCalls = 0;
}

void bypassCopiesInput()
{
    for (int mode = 0; mode < 3; ++mode) {
        for (bool inPlace : {false, true}) {
            Samples input = inputSamples();
            Samples output{};
            Instance a = instance(mode == 0 ? 0 : 1, input, inPlace ? input : output);
            DenoiseState* liveState = a->st;
            if (mode == 2) {
                a->st = nullptr;
            }
            resetProcessObservations();
            xrnnr(a.get(), mode == 1 ? kPosition + 1 : kPosition);
            check(g_processCalls == 0, "disabled, other-position and unavailable states bypass processing");
            check((inPlace ? input : output) == inputSamples(), "bypass preserves every I/Q sample in place and out of place");
            a->st = liveState;
        }
    }
}

void enabledProcessingUsesLiveState()
{
    Samples input = inputSamples();
    Samples output{};
    Instance a = instance(1, input, output);
    resetProcessObservations();
    xrnnr(a.get(), kPosition);
    check(g_processCalls == 1 && g_unavailableProcessCalls == 0,
          "enabled matching-position block sends one frame to the live dependency");
    for (int i = 0; i < kFrameSize; ++i) {
        check(std::abs(output[2 * i] - input[2 * i]) < 1e-12,
              "identity dependency returns the real sample through the production frame path");
        check(output[2 * i + 1] == 0.0, "processed output clears the imaginary component");
    }
}

void admissionObservesStateAfterLock()
{
    Samples input = inputSamples();
    Samples output{};
    Instance a = instance(1, input, output);
    DenoiseState* liveState = a->st;
    resetProcessObservations();
    platformEnter(&a->cs);
    g_watchedLock = &a->cs;
    g_attemptedAdmission = false;
    std::thread worker([&a] { xrnnr(a.get(), kPosition); });
    bool attempted;
    {
        std::unique_lock<std::mutex> lock(g_admissionMutex);
        attempted = g_admissionChanged.wait_for(lock, std::chrono::seconds(5), [] {
            return g_attemptedAdmission;
        });
    }
    // Publish the normal unavailable state while owning the existing lock.
    // The retained live object is restored after the worker joins for cleanup.
    a->run = 0;
    a->st = nullptr;
    LeaveCriticalSection(&a->cs);
    worker.join();
    g_watchedLock = nullptr;
    check(attempted, "worker reaches existing lock admission without sleeps");
    check(g_processCalls == 0, "admission checks availability after acquiring the existing lock");
    check(output == input, "state changed before admission produces unmodified passthrough");
    a->st = liveState;
    a->run = 1;
}

void modelReplacementUnpublishesStates()
{
    Samples firstInput = inputSamples();
    Samples firstOutput{};
    Samples secondInput = inputSamples();
    Samples secondOutput{};
    Instance enabled = instance(1, firstInput, firstOutput);
    Instance disabled = instance(0, secondInput, secondOutput);
    g_observedInstances = {enabled.get(), disabled.get()};
    g_observedCount = 2;
    g_modelObservations = 0;
    g_unpublishedDuringLoad = true;
    DenoiseState* oldEnabled = enabled->st;
    DenoiseState* oldDisabled = disabled->st;
    RNNRloadModel("test-only-model");
    check(g_modelObservations == 1 && g_unpublishedDuringLoad,
          "model loader observes all replaced states unpublished and stopped");
    check(!oldEnabled->active && !oldDisabled->active,
          "replacement retires both previous dependency states");
    check(enabled->st && enabled->st->active && disabled->st && disabled->st->active,
          "replacement publishes live states for both instances");
    check(enabled->run == 1 && disabled->run == 0, "model replacement restores each instance's run state");
    resetProcessObservations();
    xrnnr(enabled.get(), kPosition);
    xrnnr(disabled.get(), kPosition);
    check(g_processCalls == 1 && g_unavailableProcessCalls == 0,
          "only the previously enabled instance processes after replacement");
    check(secondOutput == secondInput, "previously disabled instance still passes through after replacement");

    g_failCreate = true;
    g_unpublishedDuringLoad = true;
    RNNRloadModel("test-only-create-failure");
    g_failCreate = false;
    check(g_modelObservations == 2 && g_unpublishedDuringLoad,
          "failed replacement also unpublishes previous states before loading");
    check(!enabled->st && !disabled->st && enabled->run == 1 && disabled->run == 0,
          "creation failure leaves unavailable state and retains requested run values");
    firstOutput.fill(0);
    resetProcessObservations();
    xrnnr(enabled.get(), kPosition);
    check(g_processCalls == 0 && firstOutput == firstInput,
          "failed model creation yields normal passthrough");
    g_observedCount = 0;
}

} // namespace

extern "C" {

struct _rxa rxa[1]{};
struct _ch ch[1]{};

void* malloc0(int size)
{
    void* allocation = _aligned_malloc(static_cast<std::size_t>(size), 16);
    if (!allocation) {
        std::abort();
    }
    std::memset(allocation, 0, static_cast<std::size_t>(size));
    return allocation;
}

void RXAbp1Check(int) {}
void RXAbp1Set(int) {}

#if !defined(Q_OS_WIN)
void InitializeCriticalSectionAndSpinCount(pthread_mutex_t* cs, int)
{
    pthread_mutex_init(cs, nullptr);
}
void LeaveCriticalSection(pthread_mutex_t* cs)
{
    pthread_mutex_unlock(cs);
}
void DeleteCriticalSection(pthread_mutex_t* cs)
{
    pthread_mutex_destroy(cs);
}
#endif

void WdspEnterCS(LPCRITICAL_SECTION cs)
{
    if (cs == g_watchedLock) {
        std::lock_guard<std::mutex> lock(g_admissionMutex);
        g_attemptedAdmission = true;
        g_admissionChanged.notify_one();
    }
    platformEnter(cs);
}

int rnnoise_get_frame_size() { return kFrameSize; }
DenoiseState* rnnoise_create(RNNModel*)
{
    if (g_failCreate) {
        return nullptr;
    }
    if (g_nextState == g_states.size()) {
        std::abort();
    }
    DenoiseState* state = &g_states[g_nextState++];
    state->active = true;
    return state;
}
void rnnoise_destroy(DenoiseState* state)
{
    if (state) {
        state->active = false;
    }
}
float rnnoise_process_frame(DenoiseState* state, float* output, const float* input)
{
    ++g_processCalls;
    if (!state || !state->active) {
        ++g_unavailableProcessCalls;
    }
    std::memcpy(output, input, kFrameSize * sizeof(float));
    return 0.0f;
}
RNNModel* rnnoise_model_from_filename(const char*)
{
    ++g_modelObservations;
    for (int i = 0; i < g_observedCount; ++i) {
        RNNR a = g_observedInstances[i];
        platformEnter(&a->cs);
        g_unpublishedDuringLoad = g_unpublishedDuringLoad && a->st == nullptr && a->run == 0;
        LeaveCriticalSection(&a->cs);
    }
    return &g_model;
}
void rnnoise_model_free(RNNModel*) {}

} // extern "C"

int main()
{
    bypassCopiesInput();
    enabledProcessingUsesLiveState();
    admissionObservesStateAfterLock();
    modelReplacementUnpublishesStates();
    RNNRloadModel(nullptr);
    std::printf("NR3 normal state consistency: %d failure(s)\n", g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
