#ifndef MGB64_AUDIO_QUEUE_CONTROLLER_H
#define MGB64_AUDIO_QUEUE_CONTROLLER_H

#include <stdint.h>

/* Wall-clock pump-rate estimator for the live (non-deterministic) audio queue
 * controller. The EMA is kept in Q8 sample frames so normal 60 Hz jitter does
 * not make the 16-sample-aligned synth size chatter. */
typedef struct PortAudioPumpRate {
    uint32_t ema_samples_q8;
} PortAudioPumpRate;

void portAudioPumpRateReset(PortAudioPumpRate *rate, uint32_t nominal_samples);

uint32_t portAudioPumpRateObserve(PortAudioPumpRate *rate,
                                  uint32_t elapsed_ticks,
                                  uint64_t ticks_per_second,
                                  uint32_t output_rate,
                                  uint32_t nominal_samples,
                                  uint32_t max_samples);

int32_t portAudioQueueChooseSamples(int32_t base_samples,
                                    int32_t queued_samples,
                                    int32_t target_samples,
                                    int32_t min_samples,
                                    int32_t max_samples);

#endif
