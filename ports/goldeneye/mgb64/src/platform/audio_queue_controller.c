#include "audio_queue_controller.h"

#define PORT_AUDIO_SAMPLE_ALIGNMENT 16u
#define PORT_AUDIO_PUMP_EMA_SHIFT   2u  /* 1/4 new observation, 3/4 history */

static uint32_t portAudioAlignNearest16(uint32_t samples) {
    return (samples + (PORT_AUDIO_SAMPLE_ALIGNMENT / 2u))
         & ~(PORT_AUDIO_SAMPLE_ALIGNMENT - 1u);
}

void portAudioPumpRateReset(PortAudioPumpRate *rate, uint32_t nominal_samples) {
    if (rate != 0) {
        rate->ema_samples_q8 = nominal_samples << 8;
    }
}

uint32_t portAudioPumpRateObserve(PortAudioPumpRate *rate,
                                  uint32_t elapsed_ticks,
                                  uint64_t ticks_per_second,
                                  uint32_t output_rate,
                                  uint32_t nominal_samples,
                                  uint32_t max_samples) {
    uint64_t observed_q8;
    uint64_t min_q8;
    uint64_t max_q8;
    int64_t delta;
    uint32_t base_samples;

    if (rate == 0 || ticks_per_second == 0 || output_rate == 0) {
        return nominal_samples;
    }
    if (max_samples < nominal_samples) {
        max_samples = nominal_samples;
    }
    if (rate->ema_samples_q8 == 0) {
        portAudioPumpRateReset(rate, nominal_samples);
    }

    observed_q8 = ((uint64_t)elapsed_ticks * (uint64_t)output_rate * 256u
                 + ticks_per_second / 2u) / ticks_per_second;
    min_q8 = (uint64_t)nominal_samples << 8;
    max_q8 = (uint64_t)max_samples << 8;

    /* This correction exists only for pumps slower than the canonical 60 Hz
     * cadence. Faster observations are scheduler jitter/catch-up, not a reason
     * to thin the already-proven latency cushion. */
    if (observed_q8 < min_q8) observed_q8 = min_q8;
    if (observed_q8 > max_q8) observed_q8 = max_q8;

    delta = (int64_t)observed_q8 - (int64_t)rate->ema_samples_q8;
    rate->ema_samples_q8 = (uint32_t)((int64_t)rate->ema_samples_q8
                           + delta / (1 << PORT_AUDIO_PUMP_EMA_SHIFT));

    base_samples = portAudioAlignNearest16((rate->ema_samples_q8 + 128u) >> 8);
    if (base_samples < nominal_samples) base_samples = nominal_samples;
    if (base_samples > max_samples) base_samples = max_samples;
    return base_samples;
}

int32_t portAudioQueueChooseSamples(int32_t base_samples,
                                    int32_t queued_samples,
                                    int32_t target_samples,
                                    int32_t min_samples,
                                    int32_t max_samples) {
    int32_t chosen;

    if (min_samples < (int32_t)PORT_AUDIO_SAMPLE_ALIGNMENT) {
        min_samples = (int32_t)PORT_AUDIO_SAMPLE_ALIGNMENT;
    }
    if (max_samples < min_samples) {
        max_samples = min_samples;
    }

    chosen = base_samples + ((target_samples - queued_samples) / 2);
    if (chosen < min_samples) chosen = min_samples;
    if (chosen > max_samples) chosen = max_samples;
    return chosen & ~((int32_t)PORT_AUDIO_SAMPLE_ALIGNMENT - 1);
}
