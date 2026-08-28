/* ROM-free FID-0141 regression: the live queue controller must preserve its
 * configured cushion when the rendered-frame audio pump falls below 40 fps.
 * The production helper is exercised directly; no duplicate controller math. */
#include <stdint.h>
#include <stdio.h>

#include "audio_queue_controller.h"

#define OUTPUT_RATE     22050u
#define TICK_RATE       46875000u
#define NOMINAL_SAMPLES 368u
#define MIN_SAMPLES     352
#define MAX_SAMPLES     784
#define TARGET_SAMPLES  1104
#define FRAME_SIZE      736u
#define MAX_FRAME_SIZE  789u

static int g_failures;

/* Test-only hooks into the real audi_port.c wiring and its link stubs. */
extern void portAudioTestConfigureQueueController(uint32_t frame_size,
                                                   uint32_t max_frame_size);
extern int32_t portAudioTestSizeFrameSamples(void);
extern uint32_t portAudioTestGetPumpBaseSamples(void);
extern void portAudioTestStubSetCount(uint32_t value);
extern void portAudioTestStubSetQueuedAudioBytes(uint32_t value);

#define CHECK_TRUE(expr, ctx) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (ctx), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

static uint32_t ticksForFps(uint32_t fps) {
    return (TICK_RATE + fps / 2u) / fps;
}

static uint32_t convergeBase(uint32_t fps, int observations) {
    PortAudioPumpRate rate = {0};
    uint32_t base = NOMINAL_SAMPLES;
    int i;

    portAudioPumpRateReset(&rate, NOMINAL_SAMPLES);
    for (i = 0; i < observations; i++) {
        base = portAudioPumpRateObserve(&rate, ticksForFps(fps), TICK_RATE,
                                        OUTPUT_RATE, NOMINAL_SAMPLES, MAX_SAMPLES);
    }
    return base;
}

static double settleQueueBefore(uint32_t fps, int adaptive, int frames) {
    PortAudioPumpRate rate = {0};
    const double consumed = (double)OUTPUT_RATE / (double)fps;
    double queue_after = (double)TARGET_SAMPLES + (double)NOMINAL_SAMPLES;
    double queue_before = 0.0;
    int i;

    portAudioPumpRateReset(&rate, NOMINAL_SAMPLES);
    for (i = 0; i < frames; i++) {
        uint32_t base = NOMINAL_SAMPLES;
        int32_t produced;

        queue_before = queue_after - consumed;
        if (queue_before < 0.0) queue_before = 0.0;
        if (adaptive) {
            base = portAudioPumpRateObserve(&rate, ticksForFps(fps), TICK_RATE,
                                            OUTPUT_RATE, NOMINAL_SAMPLES, MAX_SAMPLES);
        }
        produced = portAudioQueueChooseSamples((int32_t)base,
                                               (int32_t)queue_before,
                                               TARGET_SAMPLES,
                                               MIN_SAMPLES,
                                               MAX_SAMPLES);
        queue_after = queue_before + (double)produced;
    }
    return queue_before;
}

int main(void) {
    PortAudioPumpRate rate = {0};
    uint32_t base;
    int i;

    portAudioPumpRateReset(&rate, NOMINAL_SAMPLES);
    base = portAudioPumpRateObserve(&rate, ticksForFps(60), TICK_RATE,
                                    OUTPUT_RATE, NOMINAL_SAMPLES, MAX_SAMPLES);
    CHECK_TRUE(base == NOMINAL_SAMPLES, "60 Hz keeps the proven 368-sample base");

    CHECK_TRUE(convergeBase(40, 48) == 544u,
               "40 Hz converges to the nearest aligned realtime production");
    CHECK_TRUE(convergeBase(30, 48) == 736u,
               "30 Hz converges to the nearest aligned realtime production");
    CHECK_TRUE(convergeBase(20, 48) == MAX_SAMPLES,
               "unsupported very-low rates clamp at the existing synth maximum");

    portAudioPumpRateReset(&rate, NOMINAL_SAMPLES);
    for (i = 0; i < 48; i++) {
        (void)portAudioPumpRateObserve(&rate, ticksForFps(30), TICK_RATE,
                                       OUTPUT_RATE, NOMINAL_SAMPLES, MAX_SAMPLES);
    }
    base = 0;
    for (i = 0; i < 48; i++) {
        base = portAudioPumpRateObserve(&rate, ticksForFps(60), TICK_RATE,
                                        OUTPUT_RATE, NOMINAL_SAMPLES, MAX_SAMPLES);
    }
    CHECK_TRUE(base == NOMINAL_SAMPLES, "EMA recovers to the 60 Hz base after a slowdown");

    {
        double fixed40 = settleQueueBefore(40, 0, 240);
        double adaptive40 = settleQueueBefore(40, 1, 240);
        double fixed30 = settleQueueBefore(30, 0, 240);
        double adaptive30 = settleQueueBefore(30, 1, 240);

        CHECK_TRUE(fixed40 < TARGET_SAMPLES * 0.75,
                   "negative control: fixed 60 Hz base thins the 40 fps cushion");
        CHECK_TRUE(adaptive40 > TARGET_SAMPLES * 0.90,
                   "adaptive base preserves the configured cushion at 40 fps");
        CHECK_TRUE(fixed30 < TARGET_SAMPLES * 0.45,
                   "negative control: fixed 60 Hz base thins the 30 fps cushion");
        CHECK_TRUE(adaptive30 > TARGET_SAMPLES * 0.90,
                   "adaptive base preserves the configured cushion at 30 fps");
    }

    /* Integration seam: drive portAudioSizeFrame itself with target occupancy,
     * so the selected frame size equals its pump base. A production wiring
     * revert to the fixed realtime_60hz constant reddens this assertion even if
     * the pure helper remains correct. */
    portAudioTestStubSetCount(0);
    portAudioTestStubSetQueuedAudioBytes(TARGET_SAMPLES * 4u);
    portAudioTestConfigureQueueController(FRAME_SIZE, MAX_FRAME_SIZE);
    CHECK_TRUE(portAudioTestSizeFrameSamples() == (int32_t)NOMINAL_SAMPLES,
               "real portAudioSizeFrame starts at the 60 Hz base");
    for (i = 1; i <= 48; i++) {
        portAudioTestStubSetCount((uint32_t)i * ticksForFps(30));
        (void)portAudioTestSizeFrameSamples();
    }
    CHECK_TRUE(portAudioTestGetPumpBaseSamples() == 736u,
               "real portAudioSizeFrame wiring follows the 30 Hz EMA");

    if (g_failures != 0) {
        fprintf(stderr, "test_audio_queue_controller: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_audio_queue_controller: OK (60/40/30 fps + recovery + wiring)\n");
    return 0;
}
