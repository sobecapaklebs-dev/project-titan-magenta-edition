#!/usr/bin/env python3
"""Compare two raw PCM audio dumps (s16le stereo 22050Hz).

Usage: compare_audio.py baseline.raw test.raw

Reports RMS difference, max amplitude, and zero-crossing rate.
Exit code 0 = match, 1 = regression detected.
"""
import struct, sys, math

def load_pcm(path):
    with open(path, "rb") as f:
        data = f.read()
    return struct.unpack(f"<{len(data)//2}h", data)

def rms(samples):
    if not samples:
        return 0.0
    return math.sqrt(sum(s*s for s in samples) / len(samples))

def zero_crossing_rate(samples):
    if len(samples) < 2:
        return 0.0
    crossings = sum(1 for i in range(1, len(samples))
                    if (samples[i] >= 0) != (samples[i-1] >= 0))
    return crossings / len(samples)

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} baseline.raw test.raw")
        sys.exit(2)

    base = load_pcm(sys.argv[1])
    test = load_pcm(sys.argv[2])

    if len(base) == 0 and len(test) == 0:
        print("Samples: both empty")
        print("PASS")
        sys.exit(0)
    if len(base) == 0 or len(test) == 0:
        print(f"FAIL: one file empty ({len(base)} vs {len(test)} samples)")
        sys.exit(1)

    min_len = min(len(base), len(test))
    if abs(len(base) - len(test)) > max(len(base), 1) * 0.1:
        print(f"WARNING: size mismatch: {len(base)} vs {len(test)} samples")

    # RMS of each
    rms_base = rms(base[:min_len])
    rms_test = rms(test[:min_len])

    # RMS of difference
    diff = [test[i] - base[i] for i in range(min_len)]
    rms_diff = rms(diff)

    # Zero crossing rates (high ZCR = noise/static)
    zcr_base = zero_crossing_rate(base[:min_len])
    zcr_test = zero_crossing_rate(test[:min_len])

    # Max amplitude
    max_base = max(abs(s) for s in base[:min_len])
    max_test = max(abs(s) for s in test[:min_len])

    print(f"Samples:    {len(base)} vs {len(test)} (compared {min_len})")
    print(f"RMS:        baseline={rms_base:.1f}  test={rms_test:.1f}  diff={rms_diff:.1f}")
    print(f"Max amp:    baseline={max_base}  test={max_test}")
    print(f"ZCR:        baseline={zcr_base:.4f}  test={zcr_test:.4f}")

    # Thresholds
    fail = False
    if rms_diff > 500:
        print(f"FAIL: RMS difference {rms_diff:.1f} > 500 (significant audio change)")
        fail = True
    if zcr_test > 0.8 and zcr_base < 0.5:
        print(f"FAIL: ZCR jumped from {zcr_base:.4f} to {zcr_test:.4f} (likely static/noise)")
        fail = True
    if rms_test < 10 and rms_base > 100:
        print(f"FAIL: Audio went silent (RMS {rms_base:.1f} -> {rms_test:.1f})")
        fail = True

    if fail:
        sys.exit(1)
    else:
        print("PASS")
        sys.exit(0)

if __name__ == "__main__":
    main()
