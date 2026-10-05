#!/usr/bin/env python3
"""Analyse a dry / competitor / BMO WAV triad for BMO Opto.

Takes matched-level WAV renders of the *same* input signal -- one
unprocessed, one run through a competitor plugin (Opto plug A or B for
Tele mode, Comp plug C or D for Stressed mode), one run
through BMO Opto (see `measure gen` / `measure render` in
tools/measure/opto/main.cpp for producing a matched BMO pass on the same
source) -- and reports the numbers a shootout actually needs:

    1. gain-reduction envelope over time, read as a sliding RMS ratio
       against the dry signal. This IS the attack/release curve -- there is
       no separate tool for that, it falls out of the same envelope.
    2. attack and release time constants fitted to that envelope
    3. band energy, level-matched
    4. null-residual peak/RMS dBFS: phase-flip the processed signal, sum
       with dry (or with the other processed file), report what's left

Nothing here is hardcoded to one plugin -- pass any competitor's bounce as
`--competitor` and it's read the same way BMO's own render is.

    python3 trace_measurements.py dry.wav bmo.wav
    python3 trace_measurements.py dry.wav bmo.wav --competitor opto-plug-a.wav --label "Opto plug A"

Needs numpy:

    uv venv && uv pip install numpy
"""

import argparse
import struct
import sys
from dataclasses import dataclass

import numpy as np


# ==============================================================================
# WAV I/O -- PCM 16/24/32 and IEEE float 32, any channel count, downmixed to
# mono. Matched to the C++ harness's own reader (tools/measure/opto/main.cpp)
# so a file either tool writes reads back the same way in the other.
# ==============================================================================

def read_wav(path):
    """Returns (mono_samples: float64 ndarray in [-1, 1], sample_rate)."""
    with open(path, "rb") as f:
        data = f.read()

    if data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file")

    fmt_tag, channels, rate, bits = 1, 1, 48000, 16
    at = 12
    samples = None

    while at + 8 <= len(data):
        chunk_id = data[at:at + 4]
        size = struct.unpack_from("<I", data, at + 4)[0]
        body = at + 8

        if chunk_id == b"fmt " and body + 16 <= len(data):
            fmt_tag, channels, rate = struct.unpack_from("<HHI", data, body)[0:3]
            bits = struct.unpack_from("<H", data, body + 14)[0]
        elif chunk_id == b"data":
            payload = data[body:body + min(size, len(data) - body)]
            bytes_per_sample = bits // 8
            frame_bytes = bytes_per_sample * channels
            frames = len(payload) // frame_bytes
            payload = payload[:frames * frame_bytes]

            if fmt_tag == 3 and bits == 32:
                raw = np.frombuffer(payload, dtype="<f4")
            elif bits == 16:
                raw = np.frombuffer(payload, dtype="<i2").astype(np.float64) / 32768.0
            elif bits == 24:
                b = np.frombuffer(payload, dtype=np.uint8).reshape(-1, 3)
                as_i32 = (b[:, 0].astype(np.int32)
                          | (b[:, 1].astype(np.int32) << 8)
                          | (b[:, 2].astype(np.int32) << 16))
                as_i32 = np.where(as_i32 & 0x800000, as_i32 - 0x1000000, as_i32)
                raw = as_i32.astype(np.float64) / 8388608.0
            elif bits == 32:
                raw = np.frombuffer(payload, dtype="<i4").astype(np.float64) / 2147483648.0
            else:
                raise ValueError(f"{path}: unsupported bit depth {bits}")

            raw = raw.astype(np.float64).reshape(-1, channels)
            samples = raw.mean(axis=1)  # downmix to mono
            break

        at = body + size + (size & 1)

    if samples is None:
        raise ValueError(f"{path}: no data chunk found")

    return samples, float(rate)


# ==============================================================================
# Metrics.
# ==============================================================================

def rms(x):
    return float(np.sqrt(np.mean(x.astype(np.float64) ** 2))) if len(x) else 0.0


def peak(x):
    return float(np.max(np.abs(x))) if len(x) else 0.0


def dbfs(linear):
    return 20.0 * np.log10(max(linear, 1e-9))


def align(*signals):
    """Truncate every signal to the shortest length so they compare sample
    for sample. Nothing here time-aligns a latency difference -- BMO Opto
    reports zero latency, but a competitor plugin might not, and no amount
    of truncation fixes that; a mismatch big enough to matter shows up as a
    smeared envelope and a poor null, and is worth checking by ear."""
    n = min(len(s) for s in signals)
    return tuple(s[:n] for s in signals)


def sliding_rms_ratio_db(dry, wet, rate, window_ms=10.0, hop_ms=2.5):
    """The GR/attack/release envelope: sliding-window RMS of `wet` against
    the same window of `dry`, in dB. This is the one measurement that both
    an attack/release-time fit and a "what does the envelope look like"
    plot come from -- there's no separate release-curve tool because this
    already is one.

    Includes whatever makeup gain and output trim are baked into the file,
    not gain reduction alone -- there's no way to separate the two from a
    rendered WAV without the plugin's own metering. Level-match the two
    renders' overall RMS first (this function does not) if a clean
    reduction-only curve, rather than net level change, is what's wanted.
    """
    window = max(1, int(round(window_ms * 0.001 * rate)))
    hop = max(1, int(round(hop_ms * 0.001 * rate)))
    n = min(len(dry), len(wet))

    times, ratios = [], []

    for start in range(0, n - window, hop):
        d = rms(dry[start:start + window])
        w = rms(wet[start:start + window])

        if d < 1e-7:
            continue

        times.append((start + window * 0.5) / rate)
        ratios.append(dbfs(w) - dbfs(d) if w > 1e-9 else -120.0)

    return np.array(times), np.array(ratios)


def fit_time_constant(t, y, target_db):
    """Fit y(t) = target + (y[0] - target) * exp(-(t - t[0]) / tau) by
    linear regression on log|y - target| against t. Returns tau in seconds,
    or None if the segment doesn't have enough range to fit meaningfully.
    """
    if len(t) < 3:
        return None

    diff = y - target_db
    valid = np.abs(diff) > 1e-3

    if valid.sum() < 3:
        return None

    t, diff = t[valid], diff[valid]
    log_diff = np.log(np.abs(diff))

    # log|diff| = log|diff0| - (t - t0) / tau  -->  slope = -1 / tau
    slope, _ = np.polyfit(t - t[0], log_diff, 1)

    if slope >= 0:
        return None  # not actually decaying toward target

    return -1.0 / slope


def attack_release_times(t, ratio_db):
    """Locate the deepest point of the envelope (peak gain reduction) and
    fit an exponential on either side of it: attack from where the
    envelope first departs from ~0 dB down to the deepest point, release
    from the deepest point back out to ~0 dB. A single hit is assumed --
    for a programme signal with many transients, run this per-hit."""
    if len(ratio_db) < 5:
        return None, None

    trough = int(np.argmin(ratio_db))
    baseline = float(np.median(ratio_db[:max(1, trough // 4)])) if trough > 0 else 0.0

    # Attack: from the last point near baseline before the trough, to the trough.
    pre = ratio_db[:trough + 1]
    near_baseline = np.where(np.abs(pre - baseline) < 0.5)[0]
    attack_start = int(near_baseline[-1]) if len(near_baseline) else 0
    attack_tau = fit_time_constant(t[attack_start:trough + 1], ratio_db[attack_start:trough + 1],
                                   ratio_db[trough])

    # Release: from the trough back out toward baseline.
    post = ratio_db[trough:]
    release_tau = fit_time_constant(t[trough:], post, baseline)

    return attack_tau, release_tau


def band_energy_db(x, rate, bands, size=16384):
    """Welch-averaged power spectrum, band energy relative to the signal's
    own total -- the same shape as tools/measure/sat's spectrum()/bandEnergy(),
    reused here because it's the right generic tool for "how is this
    signal's energy distributed," not something specific to saturation."""
    if len(x) < size:
        size = 1 << int(np.floor(np.log2(max(len(x), 2))))

    if size < 64:
        return {name: float("nan") for name, *_ in bands}

    hop = size // 2
    window = np.hanning(size)
    power = np.zeros(size // 2 + 1)
    frames = 0

    for start in range(0, len(x) - size + 1, hop):
        spectrum = np.fft.rfft(x[start:start + size] * window)
        power += np.abs(spectrum) ** 2
        frames += 1

    if frames == 0:
        return {name: float("nan") for name, *_ in bands}

    power /= frames
    bin_hz = rate / size
    freqs = np.arange(len(power)) * bin_hz

    out = {}
    for name, lo, hi in bands:
        mask = (freqs >= lo) & (freqs < hi)
        out[name] = float(power[mask].sum())

    return out


DEFAULT_BANDS = [
    ("20 Hz - 150 Hz", 20.0, 150.0),
    ("150 Hz - 600 Hz", 150.0, 600.0),
    ("600 Hz - 2.5 kHz", 600.0, 2500.0),
    ("2.5 kHz - 6 kHz", 2500.0, 6000.0),
    ("6 kHz - 18 kHz", 6000.0, 18000.0),
]


def null_residual(a, b):
    """Level-match b to a by RMS, phase-flip b, sum with a, report the
    residual's peak and RMS in dBFS -- the same delta-tracking format the
    Saturator docs use: a clean null is a large negative number in both: a
    perfect match nulls to silence, a processing difference leaves audible
    residual behind."""
    a, b = align(a, b)
    a_rms, b_rms = rms(a), rms(b)
    match = a_rms / b_rms if b_rms > 1e-9 else 1.0
    residual = a - b * match
    return dbfs(peak(residual)), dbfs(rms(residual)), 20.0 * np.log10(max(match, 1e-9))


# ==============================================================================
@dataclass
class Pass:
    label: str
    samples: "np.ndarray"


def report_pass(dry, p: Pass, rate, args):
    dry_a, wet_a = align(dry, p.samples)

    print(f"\n=== {p.label} ===")
    print(f"  level         dry {dbfs(rms(dry_a)):+6.1f} dBFS RMS / {dbfs(peak(dry_a)):+6.1f} dBFS peak"
          f"   wet {dbfs(rms(wet_a)):+6.1f} dBFS RMS / {dbfs(peak(wet_a)):+6.1f} dBFS peak")

    if len(dry) != len(p.samples):
        print(f"  NOTE: dry is {len(dry)} samples, {p.label} is {len(p.samples)} -- "
              f"truncated to {len(dry_a)} for comparison. A latency difference this size "
              f"will smear the envelope and null below.")

    t, ratio_db = sliding_rms_ratio_db(dry_a, wet_a, rate, args.window_ms, args.hop_ms)

    if len(ratio_db):
        print(f"  envelope      {len(ratio_db)} points, {args.window_ms:.1f} ms window / "
              f"{args.hop_ms:.1f} ms hop, deepest {ratio_db.min():+.2f} dB at t={t[np.argmin(ratio_db)]:.3f} s")

        attack_tau, release_tau = attack_release_times(t, ratio_db)
        print(f"  attack tau    {attack_tau * 1000.0:.1f} ms" if attack_tau else "  attack tau    (not fitted -- envelope too flat or too short)")
        print(f"  release tau   {release_tau:.3f} s" if release_tau else "  release tau   (not fitted -- envelope too flat or too short)")
    else:
        print("  envelope      (too short to compute -- need at least one window's worth of audio)")

    bands = band_energy_db(wet_a, rate, DEFAULT_BANDS)
    dry_bands = band_energy_db(dry_a, rate, DEFAULT_BANDS)
    total_wet = sum(bands.values()) or 1.0
    total_dry = sum(dry_bands.values()) or 1.0

    print("  band energy, level-matched, relative to each signal's own total:")
    print(f"    {'band':<18} {'dry':>8} {'wet':>8} {'delta':>8}")
    for name, *_ in DEFAULT_BANDS:
        d = 10.0 * np.log10(dry_bands[name] / total_dry) if dry_bands[name] > 0 else float("-inf")
        w = 10.0 * np.log10(bands[name] / total_wet) if bands[name] > 0 else float("-inf")
        print(f"    {name:<18} {d:+8.2f} {w:+8.2f} {w - d:+8.2f}")

    null_peak, null_rms_db, match_db = null_residual(dry_a, wet_a)
    print(f"  null vs. dry  matched {match_db:+.2f} dB, residual peak {null_peak:+.2f} dBFS, RMS {null_rms_db:+.2f} dBFS")

    return t, ratio_db


def main():
    parser = argparse.ArgumentParser(
        description="Analyse a dry / competitor / BMO WAV triad for BMO Opto.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("dry", help="unprocessed source WAV")
    parser.add_argument("bmo", help="BMO Opto's rendered WAV, same source, matched level")
    parser.add_argument("--competitor", help="a competitor plugin's rendered WAV, same source, matched level")
    parser.add_argument("--label", default="competitor", help="name for the competitor pass in the report")
    parser.add_argument("--window-ms", type=float, default=10.0, help="sliding RMS window (default 10 ms)")
    parser.add_argument("--hop-ms", type=float, default=2.5, help="sliding RMS hop (default 2.5 ms)")
    parser.add_argument("--csv", help="write the envelope trace(s) to this CSV path")
    args = parser.parse_args()

    dry, dry_rate = read_wav(args.dry)
    bmo, bmo_rate = read_wav(args.bmo)

    if abs(dry_rate - bmo_rate) > 1.0:
        print(f"sample rates differ: dry {dry_rate:.0f} Hz, bmo {bmo_rate:.0f} Hz -- not resampling, "
              f"results below assume they line up sample for sample, which they will not.")

    print(f"dry: {args.dry} ({len(dry)} samples, {dry_rate:.0f} Hz)")

    traces = {}
    t, ratio = report_pass(dry, Pass("BMO", bmo), dry_rate, args)
    traces["bmo"] = (t, ratio)

    if args.competitor:
        comp, comp_rate = read_wav(args.competitor)

        if abs(comp_rate - dry_rate) > 1.0:
            print(f"\nsample rates differ: dry {dry_rate:.0f} Hz, {args.label} {comp_rate:.0f} Hz -- not resampling.")

        t, ratio = report_pass(dry, Pass(args.label, comp), dry_rate, args)
        traces["competitor"] = (t, ratio)

        bmo_a, comp_a = align(bmo, comp)
        null_peak, null_rms_db, match_db = null_residual(comp_a, bmo_a)
        print(f"\n=== BMO vs. {args.label} ===")
        print(f"  null          matched {match_db:+.2f} dB, residual peak {null_peak:+.2f} dBFS, RMS {null_rms_db:+.2f} dBFS")
        print("  (a deep null here means the two plugins produced near-identical output at these\n"
              "   settings, not just similar RMS -- the strongest form of match this script can report.)")

    if args.csv:
        with open(args.csv, "w") as f:
            header = ["time_s", "bmo_ratio_db"]
            if "competitor" in traces:
                header.append(f"{args.label.replace(',', ' ')}_ratio_db")
            f.write(",".join(header) + "\n")

            bmo_t, bmo_ratio = traces["bmo"]
            comp_t, comp_ratio = traces.get("competitor", (np.array([]), np.array([])))

            n = len(bmo_t) if "competitor" not in traces else min(len(bmo_t), len(comp_t))
            for i in range(n):
                row = [f"{bmo_t[i]:.6f}", f"{bmo_ratio[i]:.3f}"]
                if "competitor" in traces:
                    row.append(f"{comp_ratio[i]:.3f}")
                f.write(",".join(row) + "\n")

        print(f"\nwrote envelope trace to {args.csv}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
