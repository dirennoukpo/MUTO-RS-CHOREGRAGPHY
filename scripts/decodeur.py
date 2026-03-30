#!/usr/bin/env python3
"""
╔══════════════════════════════════════════════════════════════════╗
║         PRO AUDIO → DANCE JSON GENERATOR  v2.0                  ║
║                                                                  ║
║  Pipeline:                                                       ║
║    1. HPSS separation (harmonic / percussive)                    ║
║    2. Beat tracking  (tightness=100, percussive signal)          ║
║    3. Downbeat + beat-position estimation (4/4 assumed)          ║
║    4. Onset detection  (percussive, filtered)                    ║
║    5. RMS energy  (smoothed, normalised)                         ║
║    6. Spectral flux  (smoothed, normalised)                      ║
║    7. INTENSITY = 0.5*energy + 0.5*flux  (master aggression key) ║
║    8. Structural segmentation via MFCC agglomerative clustering  ║
║    9. Automatic section labelling based on intensity quartiles:  ║
║         intro / verse / chorus / outro                           ║
║   10. All features serialised to a single JSON consumed by       ║
║       dance_leader.py                                            ║
║                                                                  ║
║  Usage:                                                          ║
║    python3 decodeur.py --input song.mp3 --output song_beats.json ║
╚══════════════════════════════════════════════════════════════════╝
"""

from __future__ import annotations

import argparse
import json
import sys
from typing import Any

import numpy as np
import librosa


# ═══════════════════════════════════════════════════════════════════
# UTILS
# ═══════════════════════════════════════════════════════════════════

def _normalize(x: np.ndarray) -> np.ndarray:
    mn, mx = x.min(), x.max()
    return (x - mn) / (mx - mn + 1e-9)


def _smooth(x: np.ndarray, window: int = 5) -> np.ndarray:
    kernel = np.ones(window) / window
    return np.convolve(x, kernel, mode="same")


def _interp_at(t: float, times: np.ndarray, values: np.ndarray) -> float:
    """Linear interpolation of values[times] at a given time t."""
    if len(times) == 0:
        return 0.0
    if t <= times[0]:
        return float(values[0])
    if t >= times[-1]:
        return float(values[-1])
    # numpy searchsorted for O(log n) lookup
    idx = int(np.searchsorted(times, t, side="right")) - 1
    t0, t1 = times[idx], times[idx + 1]
    v0, v1 = values[idx], values[idx + 1]
    ratio = (t - t0) / (t1 - t0 + 1e-9)
    return float(v0 + ratio * (v1 - v0))


def _mean_in_range(
    start: float, end: float, times: np.ndarray, values: np.ndarray
) -> float:
    """Mean value of a signal in the time window [start, end]."""
    mask = (times >= start) & (times < end)
    if not mask.any():
        return float(np.mean(values))
    return float(np.mean(values[mask]))


# ═══════════════════════════════════════════════════════════════════
# SECTION LABELLING
#
# decodeur.py clusters segments by MFCC similarity, producing labels
# "section_0" … "section_N".  We re-label them by comparing each
# segment's mean intensity against the global intensity quartiles:
#
#   intensity_mean < Q25                → intro     (agg 1)
#   Q25 ≤ intensity_mean < Q50          → verse     (agg 2)
#   Q50 ≤ intensity_mean < Q75          → chorus    (agg 3)
#   intensity_mean ≥ Q75                → chorus    (agg 3)
#
# The very first segment is always "intro" and the very last is
# always "outro", regardless of energy, to give the choreography
# a natural narrative arc.
# ═══════════════════════════════════════════════════════════════════

def _label_segments_by_intensity(
    segments: list[dict[str, Any]],
    intensity_times: np.ndarray,
    intensity_values: np.ndarray,
    duration: float,
) -> list[dict[str, Any]]:
    """Replace generic 'section_N' labels with musical section names."""

    if not segments:
        return segments

    # Compute mean intensity per segment
    means = np.array([
        _mean_in_range(
            seg["start"], seg["end"],
            intensity_times, intensity_values
        )
        for seg in segments
    ])

    q25, q50, q75 = np.percentile(means, [25, 50, 75])

    labelled: list[dict[str, Any]] = []
    n = len(segments)

    for i, seg in enumerate(segments):
        m = means[i]
        if i == 0:
            label = "intro"
        elif i == n - 1:
            label = "outro"
        elif m < q25:
            label = "intro"
        elif m < q50:
            label = "verse"
        elif m < q75:
            label = "verse"
        else:
            label = "chorus"

        labelled.append({
            "start": seg["start"],
            "end":   seg["end"],
            "label": label,
            "intensity_mean": float(m),
        })

    return labelled


# ═══════════════════════════════════════════════════════════════════
# MAIN AUDIO PIPELINE
# ═══════════════════════════════════════════════════════════════════

def process_audio(path: str) -> dict[str, Any]:

    # ── 1. Load ──────────────────────────────────────────────────────
    print("🎧  Loading audio…")
    y, sr = librosa.load(path, sr=None, mono=True)
    duration = float(librosa.get_duration(y=y, sr=sr))
    print(f"    {duration:.1f}s  sr={sr}Hz")

    # ── 2. HPSS ──────────────────────────────────────────────────────
    print("🔪  Separating harmonic / percussive…")
    _, y_perc = librosa.effects.hpss(y)

    # ── 3. Beat tracking ─────────────────────────────────────────────
    print("🥁  Detecting beats…")
    tempo_arr, beat_frames = librosa.beat.beat_track(
        y=y_perc,
        sr=sr,
        tightness=100,
        trim=False,
    )
    bpm: float = float(np.mean(tempo_arr)) if hasattr(tempo_arr, "__len__") else float(tempo_arr)
    beats: np.ndarray = librosa.frames_to_time(beat_frames, sr=sr)

    if len(beats) == 0:
        print("  ⚠  No beats detected — aborting", file=sys.stderr)
        sys.exit(1)

    print(f"    BPM={bpm:.1f}  beats={len(beats)}")

    # ── 4. Downbeats + beat positions (4/4 assumed) ───────────────────
    # Assume the first beat is beat 1.  Count 1-2-3-4-1-2-3-4 …
    beat_positions: list[int] = [((i % 4) + 1) for i in range(len(beats))]
    downbeats: list[float] = [float(beats[i]) for i in range(len(beats)) if beat_positions[i] == 1]

    # ── 5. Onset detection (percussive signal) ────────────────────────
    print("⚡  Detecting onsets…")
    onset_env = librosa.onset.onset_strength(y=y_perc, sr=sr)
    onset_frames = librosa.onset.onset_detect(
        onset_envelope=onset_env,
        sr=sr,
        backtrack=True,
        pre_max=20,
        post_max=20,
        pre_avg=50,
        post_avg=50,
        delta=0.25,
        wait=10,
    )
    onset_times: np.ndarray = librosa.frames_to_time(onset_frames, sr=sr)
    print(f"    onsets={len(onset_times)}")

    # ── 6. Frame-level time axis ──────────────────────────────────────
    hop_length = 512
    n_frames = 1 + len(y) // hop_length
    frame_times: np.ndarray = librosa.frames_to_time(np.arange(n_frames), sr=sr, hop_length=hop_length)

    # ── 7. RMS energy (smoothed + normalised) ─────────────────────────
    print("📊  Computing energy (RMS)…")
    rms_raw = librosa.feature.rms(y=y, hop_length=hop_length)[0]
    rms_raw = rms_raw[:n_frames]
    rms_smooth = _smooth(rms_raw.astype(float), window=11)
    rms_norm = _normalize(rms_smooth)

    # ── 8. Spectral flux (smoothed + normalised) ──────────────────────
    print("🌊  Computing spectral flux…")
    flux_raw = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop_length)
    flux_raw = flux_raw[:n_frames]
    flux_smooth = _smooth(flux_raw.astype(float), window=9)
    flux_norm = _normalize(flux_smooth)

    # Align lengths
    L = min(len(frame_times), len(rms_norm), len(flux_norm))
    frame_times = frame_times[:L]
    rms_norm    = rms_norm[:L]
    flux_norm   = flux_norm[:L]

    # ── 9. Intensity = master aggression signal ───────────────────────
    print("🔥  Computing intensity (aggression key)…")
    intensity_raw = 0.5 * rms_norm + 0.5 * flux_norm
    intensity_raw = _smooth(intensity_raw, window=15)
    intensity_norm = _normalize(intensity_raw)

    # ── 10. Per-beat feature values ───────────────────────────────────
    # Pre-interpolate all features at each beat timestamp for fast lookup
    beat_energy:    list[float] = [_interp_at(t, frame_times, rms_norm)   for t in beats]
    beat_flux:      list[float] = [_interp_at(t, frame_times, flux_norm)  for t in beats]
    beat_intensity: list[float] = [_interp_at(t, frame_times, intensity_norm) for t in beats]

    # Per-beat onset flag: True if an onset falls within ±30ms of the beat
    beat_onset: list[bool] = []
    for bt in beats:
        has_onset = any(abs(bt - ot) < 0.030 for ot in onset_times)
        beat_onset.append(bool(has_onset))

    # ── 11. Segmentation ─────────────────────────────────────────────
    # librosa.segment.agglomerative(data, k) returns an array of shape (k,)
    # containing the FRAME INDICES of each segment boundary — NOT per-frame
    # labels.  We convert boundaries → per-frame labels with searchsorted,
    # then build the segment list from consecutive boundary pairs.
    print("🧠  Segmenting structure…")
    n_segments = min(12, max(4, len(beats) // 16))  # adaptive k
    mfcc = librosa.feature.mfcc(y=y, sr=sr, n_mfcc=13, hop_length=hop_length)
    mfcc = mfcc[:, :L]

    # boundaries: shape (k,) — frame indices where each segment starts
    boundaries = librosa.segment.agglomerative(mfcc.T, k=n_segments)
    boundaries = np.sort(boundaries.astype(int))

    # Build segment list directly from boundary pairs
    raw_segments: list[dict[str, Any]] = []
    for seg_idx in range(len(boundaries)):
        start_frame = int(boundaries[seg_idx])
        end_frame   = int(boundaries[seg_idx + 1]) if seg_idx + 1 < len(boundaries) else L - 1
        # Clamp to valid frame range
        start_frame = max(0, min(start_frame, L - 1))
        end_frame   = max(start_frame + 1, min(end_frame, L - 1))
        raw_segments.append({
            "start": float(frame_times[start_frame]),
            "end":   float(frame_times[end_frame]),
            "label": f"section_{seg_idx}",
        })

    # Ensure the last segment reaches the true end of the track
    if raw_segments:
        raw_segments[-1]["end"] = duration

    # Re-label by intensity
    segments = _label_segments_by_intensity(
        raw_segments, frame_times, intensity_norm, duration
    )
    print(f"    segments={len(segments)}")
    for s in segments:
        print(f"      [{s['start']:7.2f} – {s['end']:7.2f}]  {s['label']:8s}  "
              f"intensity_mean={s['intensity_mean']:.3f}")

    # ── 12. Serialise full feature arrays (compact float precision) ───
    def _arr(times: np.ndarray, values: np.ndarray) -> list[dict[str, float]]:
        return [
            {"t": round(float(t), 4), "value": round(float(v), 4)}
            for t, v in zip(times, values)
        ]

    # ── 13. Build output JSON ─────────────────────────────────────────
    data: dict[str, Any] = {
        # ── identity ──────────────────────────────────────────────────
        "path":     path,
        "bpm":      round(bpm, 3),
        "duration": round(duration, 4),

        # ── beat grid ─────────────────────────────────────────────────
        "beats":          [round(float(t), 4) for t in beats],
        "downbeats":      [round(float(t), 4) for t in downbeats],
        "beat_positions": beat_positions,

        # ── per-beat features (pre-interpolated, ready for dance_leader)
        "beat_energy":    [round(v, 4) for v in beat_energy],
        "beat_flux":      [round(v, 4) for v in beat_flux],
        "beat_intensity": [round(v, 4) for v in beat_intensity],
        "beat_onset":     beat_onset,

        # ── full frame-level signals (for visualisation / debugging) ──
        "energy":         _arr(frame_times, rms_norm),
        "spectral_flux":  _arr(frame_times, flux_norm),
        "intensity":      _arr(frame_times, intensity_norm),

        # ── onset list ────────────────────────────────────────────────
        "onsets": [round(float(t), 4) for t in onset_times],

        # ── structure ─────────────────────────────────────────────────
        "segments": segments,
    }

    return data


# ═══════════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════════

def main() -> int:
    p = argparse.ArgumentParser(
        description="Audio → Dance JSON generator (pro pipeline)"
    )
    p.add_argument("--input",  "-i", required=True,  help="Input audio file (mp3/wav/flac/…)")
    p.add_argument("--output", "-o", required=True,  help="Output JSON path")
    p.add_argument("--pretty",       action="store_true", help="Pretty-print JSON")
    args = p.parse_args()

    data = process_audio(args.input)

    print("💾  Saving JSON…")
    with open(args.output, "w", encoding="utf-8") as f:
        if args.pretty:
            json.dump(data, f, indent=2, ensure_ascii=False)
        else:
            json.dump(data, f, separators=(",", ":"), ensure_ascii=False)

    beats_n = len(data["beats"])
    dur = data["duration"]
    print(f"✅  Done — {beats_n} beats  {dur:.1f}s  → {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())