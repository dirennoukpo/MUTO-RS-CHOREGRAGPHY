#!/usr/bin/env python3
"""
╔══════════════════════════════════════════════════════════════════╗
║      MUTO-RS  DANCE LEADER  v3.0  —  ROS2                       ║
║                                                                  ║
║  Publishes real-time dance commands on /dance_cmd so every       ║
║  follower robot executes the choreography in lock-step with      ║
║  the music.                                                      ║
║                                                                  ║
║  Protocol (std_msgs/String):                                     ║
║    SPEED:<1-5>    set locomotion speed                           ║
║    ACTION:<1-8>   built-in pose/action                           ║
║      1=Stretch  2=Greeting  3=Retreat  4=Warm_up                 ║
║      5=Turn_around  6=Say_no  7=Crouching  8=Stride              ║
║    MOVE:<dir>     forward|back|left|right|turnleft|turnright      ║
║    STOP           halt current movement                          ║
║    RESET          neutral stance                                  ║
║    DONE           choreography finished                          ║
║                                                                  ║
║  Usage:                                                          ║
║    python3 dance_leader.py \\                                     ║
║        --timeline song_beats.json \\                              ║
║        --audio-file song.mp3                                     ║
║                                                                  ║
║    python3 dance_leader.py --loops 2 --beat 0.8 --speed 3        ║
╚══════════════════════════════════════════════════════════════════╝
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from typing import Any

import rclpy
from rclpy.node import Node
from std_msgs.msg import String


DANCE_TOPIC = "/dance_cmd"

# ─────────────────────────────────────────────────────────────────
# AUDIO LATENCY COMPENSATION (seconds)
#
# After Popen() returns, the audio player needs this many seconds
# before the first sample actually reaches the DAC.
# Measured empirically:
#   ffplay  ≈ 90–120 ms
#   mpg123  ≈ 60–90 ms
#
# Model:
#   T_spawn  = time.monotonic() just before Popen()
#   T_sound  = T_spawn + AUDIO_PLAYER_LATENCY_S   (1st audible sample)
#   start_t  = T_spawn + AUDIO_PLAYER_LATENCY_S
#   cue fires at: start_t + cue.t_s
#              = T_spawn + L + cue.t_s
#   For cue.t_s = 0 → fires at T_sound → in sync. ✓
#
# Tune this constant:
#   robot moves BEFORE the beat → increase value
#   robot moves AFTER  the beat → decrease value
# ─────────────────────────────────────────────────────────────────
AUDIO_PLAYER_LATENCY_S: float = 0.10


# ═══════════════════════════════════════════════════════════════════
# SECTION AGGRESSION TABLE
#
# Maps any segment label → aggression level 0–3.
# Covers both hand-crafted labels (chorus, verse …) AND the
# automatic labels emitted by decodeur.py (intro/verse/chorus/outro).
# ═══════════════════════════════════════════════════════════════════
SECTION_AGGRESSION: dict[str, int] = {
    # ── calm ──────────────────────────────────────────────────────
    "start":        0,
    "silent":       0,
    "silence":      0,
    # ── low ───────────────────────────────────────────────────────
    "intro":        1,
    "outro":        1,
    "fade":         1,
    "fade_in":      1,
    "fade_out":     1,
    # ── medium ────────────────────────────────────────────────────
    "verse":        2,
    "bridge":       2,
    "pre-chorus":   2,
    "pre_chorus":   2,
    "breakdown":    2,
    "interlude":    2,
    # ── high ──────────────────────────────────────────────────────
    "chorus":       3,
    "drop":         3,
    "hook":         3,
    "refrain":      3,
    "climax":       3,
    "build":        3,
}


# ═══════════════════════════════════════════════════════════════════
# MOVEMENT PALETTES
#
# Structure:   palette[beat_pos (1-4)]  →  list[(cmd, hold_factor)]
# hold_factor: fraction of beat interval to hold before sending STOP.
# The list rotates by bar_index, giving musical variety without RNG.
#
# Hold rules (tuned per BPM range — see _select_move):
#   Beat 1 (downbeat) : 0.72–0.80 × dt   biggest accent
#   Beat 2            : 0.48–0.55 × dt   lateral snap
#   Beat 3 (backbeat) : 0.62–0.70 × dt   counter-punch
#   Beat 4            : 0.42–0.50 × dt   anticipation
#
# Hard constraint: STOP fires ≥ 120 ms before next beat (enforced
# in _select_move, not here).
# ═══════════════════════════════════════════════════════════════════

# Level 0 — calm / start / silence
_PAL0: dict[int, list[tuple[str, float]]] = {
    1: [("ACTION:1", 0.70), ("ACTION:4", 0.68)],
    2: [("MOVE:left", 0.42), ("MOVE:right", 0.42)],
    3: [("ACTION:6", 0.50), ("MOVE:back",  0.44)],
    4: [("MOVE:right", 0.38), ("MOVE:left", 0.38)],
}

# Level 1 — intro / outro
_PAL1: dict[int, list[tuple[str, float]]] = {
    1: [
        ("MOVE:forward", 0.72), ("ACTION:2",  0.72),
        ("ACTION:1",     0.70), ("MOVE:forward", 0.72),
    ],
    2: [("MOVE:left", 0.50), ("MOVE:right", 0.50)],
    3: [
        ("MOVE:back",      0.60), ("ACTION:6",      0.58),
        ("MOVE:turnright", 0.58), ("ACTION:3",      0.56),
    ],
    4: [("MOVE:right", 0.44), ("MOVE:left", 0.44)],
}

# Level 2 — verse / bridge
_PAL2: dict[int, list[tuple[str, float]]] = {
    1: [
        ("MOVE:forward",   0.75), ("ACTION:8",  0.75),
        ("MOVE:turnleft",  0.72), ("ACTION:1",  0.72),
        ("MOVE:forward",   0.75), ("ACTION:7",  0.73),
        ("MOVE:turnright", 0.70), ("ACTION:4",  0.70),
    ],
    2: [
        ("MOVE:left",  0.52), ("MOVE:right", 0.52),
        ("MOVE:left",  0.52), ("MOVE:right", 0.52),
    ],
    3: [
        ("MOVE:back",      0.65), ("MOVE:turnright", 0.65),
        ("ACTION:6",       0.62), ("MOVE:turnleft",  0.63),
    ],
    4: [
        ("MOVE:right", 0.46), ("MOVE:left",  0.46),
        ("MOVE:right", 0.46), ("MOVE:left",  0.46),
    ],
}

# Level 3 — chorus / drop / hook — MAXIMUM AGGRESSION
_PAL3: dict[int, list[tuple[str, float]]] = {
    1: [
        ("MOVE:forward",   0.78), ("ACTION:8",  0.78),
        ("MOVE:turnleft",  0.76), ("ACTION:7",  0.76),
        ("ACTION:5",       0.78), ("MOVE:forward", 0.78),
        ("ACTION:8",       0.78), ("MOVE:turnright", 0.76),
    ],
    2: [
        ("MOVE:left",  0.54), ("MOVE:right", 0.54),
        ("MOVE:left",  0.54), ("MOVE:right", 0.54),
        ("MOVE:left",  0.54), ("MOVE:right", 0.54),
        ("MOVE:left",  0.54), ("MOVE:right", 0.54),
    ],
    3: [
        ("MOVE:back",      0.68), ("MOVE:turnright", 0.68),
        ("ACTION:6",       0.65), ("MOVE:turnleft",  0.68),
        ("MOVE:back",      0.68), ("ACTION:7",       0.70),
        ("MOVE:turnright", 0.68), ("ACTION:6",       0.65),
    ],
    4: [
        ("MOVE:right", 0.50), ("MOVE:left",  0.50),
        ("MOVE:right", 0.50), ("MOVE:left",  0.50),
        ("MOVE:right", 0.50), ("MOVE:left",  0.50),
        ("MOVE:right", 0.50), ("MOVE:left",  0.50),
    ],
}

_PALETTES: dict[int, dict[int, list[tuple[str, float]]]] = {
    0: _PAL0, 1: _PAL1, 2: _PAL2, 3: _PAL3
}


# ═══════════════════════════════════════════════════════════════════
# DATA CLASSES
# ═══════════════════════════════════════════════════════════════════

class Step:
    __slots__ = ("name", "cmd", "pause_s")
    def __init__(self, name: str, cmd: str, pause_s: float) -> None:
        self.name    = name
        self.cmd     = cmd
        self.pause_s = pause_s


class TimelineCue:
    __slots__ = ("t_s", "name", "cmd", "hold_s")
    def __init__(self, t_s: float, name: str, cmd: str, hold_s: float = 0.0) -> None:
        self.t_s    = t_s
        self.name   = name
        self.cmd    = cmd
        self.hold_s = hold_s


class TimelineSelection:
    def __init__(
        self,
        cues:          list[TimelineCue],
        source_type:   str,
        timeline_file: str,
        audio_path:    str | None = None,
        bpm:           float | None = None,
        duration_s:    float | None = None,
    ) -> None:
        self.cues          = cues
        self.source_type   = source_type
        self.timeline_file = timeline_file
        self.audio_path    = audio_path
        self.bpm           = bpm
        self.duration_s    = duration_s


# ═══════════════════════════════════════════════════════════════════
# SEGMENT INDEX & HELPERS
# ═══════════════════════════════════════════════════════════════════

def _build_seg_index(
    segments: list[dict[str, Any]]
) -> list[tuple[float, float, str]]:
    index: list[tuple[float, float, str]] = []
    for seg in segments:
        try:
            s     = float(seg["start"])
            e     = float(seg["end"])
            label = str(seg.get("label", "intro")).lower().strip()
            index.append((s, e, label))
        except (KeyError, TypeError, ValueError):
            continue
    index.sort(key=lambda x: x[0])
    return index


def _label_at(t: float, index: list[tuple[float, float, str]]) -> str:
    last = "intro"
    for s, e, lbl in index:
        if s <= t < e:
            return lbl
        if s > t:
            break
        last = lbl
    return last


def _aggression_at(
    t: float,
    seg_index: list[tuple[float, float, str]],
) -> int:
    label = _label_at(t, seg_index)
    return SECTION_AGGRESSION.get(label, 1)


# ═══════════════════════════════════════════════════════════════════
# FEATURE HELPERS  (reading decodeur.py JSON format)
# ═══════════════════════════════════════════════════════════════════

def _extract_beat_feature(data: dict[str, Any], key: str) -> list[float]:
    """Return per-beat feature list or empty list if absent."""
    v = data.get(key, [])
    if not isinstance(v, list):
        return []
    out: list[float] = []
    for x in v:
        try:
            out.append(float(x))
        except (TypeError, ValueError):
            out.append(0.0)
    return out


def _extract_beat_onset(data: dict[str, Any]) -> list[bool]:
    """Return per-beat boolean onset list."""
    v = data.get("beat_onset", [])
    if not isinstance(v, list):
        return []
    return [bool(x) for x in v]


# ═══════════════════════════════════════════════════════════════════
# CHOREOGRAPHY ENGINE
# ═══════════════════════════════════════════════════════════════════

def _compute_aggression(
    base: int,
    intensity: float,
    onset: bool,
) -> int:
    """Combine section-level and beat-level signals into 0–3 aggression.

    Rules:
      - intensity ≥ 0.80  → +1 (very hot moment)
      - intensity ≥ 0.60  → +0 (normal)
      - intensity < 0.30  → −1 (quiet moment, restrain)
      - onset on beat     → +1 (percussive hit, amplify)
    Final value is clamped to [0, 3].
    """
    a = base
    if intensity >= 0.80:
        a += 1
    elif intensity < 0.30:
        a -= 1
    if onset:
        a += 1
    return max(0, min(3, a))


def _select_move(
    beat_pos:   int,
    bar_idx:    int,
    dt:         float,
    aggression: int,
    intensity:  float,
    onset:      bool,
    bpm:        float,
) -> tuple[str, float]:
    """Return (command, hold_seconds) for one beat.

    Selection logic:
    ─────────────────────────────────────────────────────────────────
    1. Choose base command + hold_factor from palette[aggression][beat_pos].
    2. Override command for special musical events:
         onset on beat    → action from an impact-move set, chosen by
                            aggression so a calm onset ≠ chorus onset.
         Very high flux (captured via intensity > 0.85)
                          → spin (turnleft/right), adds visual drama.
    3. Scale hold_factor by a "dynamic" coefficient tied to intensity
       so louder beats hold longer and quieter beats release sooner.
    4. Apply BPM-aware clamp so STOP always fires ≥ 120 ms before
       the next beat, even at fast tempos (≥160 BPM).

    No randomness.  Results are deterministic and reproducible.
    """
    palette  = _PALETTES[max(0, min(3, aggression))]
    eff_pos  = beat_pos if beat_pos in (1, 2, 3, 4) else 4
    moves    = palette[eff_pos]
    cmd, factor = moves[bar_idx % len(moves)]

    # ── Event overrides ───────────────────────────────────────────
    if onset:
        # Map aggression level to an impact action: subtle → explosive
        impact_map = {
            0: "ACTION:4",   # Warm-up (gentle)
            1: "ACTION:2",   # Greeting (expressive)
            2: "ACTION:8",   # Stride   (energetic)
            3: "ACTION:7",   # Crouching (explosive power hit)
        }
        # On a downbeat (beat_pos == 1) at high aggression, escalate further
        if beat_pos == 1 and aggression == 3:
            cmd = "ACTION:5"   # Turn-around — maximum drama
        else:
            cmd = impact_map[max(0, min(3, aggression))]

    elif intensity > 0.85 and beat_pos in (1, 3):
        # Very intense non-onset moment → explosive spin
        cmd = "MOVE:turnleft" if bar_idx % 2 == 0 else "MOVE:turnright"

    # ── Dynamic hold scaling ──────────────────────────────────────
    # intensity in [0, 1] → dynamic coefficient in [0.75, 1.10]
    dynamic = 0.75 + 0.35 * intensity
    hold_s  = dt * factor * dynamic

    # ── BPM-aware clamping ────────────────────────────────────────
    # Rule 1: STOP must arrive >= 120 ms before the next beat.
    # Rule 2: hold must never exceed 2× avg beat interval, so the robot
    #         never freezes across a structural gap (e.g. intro → chorus).
    min_gap    = 0.120
    abs_ceil   = 2.0 * (60.0 / max(20.0, bpm))   # 2 beats at current BPM
    max_hold   = max(0.05, min(dt - min_gap, abs_ceil))
    min_hold   = 0.080
    hold_s     = max(min_hold, min(max_hold, hold_s))

    return cmd, hold_s


def build_timeline_from_beats(data: dict[str, Any]) -> list[TimelineCue]:
    """Convert a decodeur.py beat-analysis JSON into a choreography cue list.

    Reads every available field:
      beats / downbeats / beat_positions       → rhythm grid
      beat_energy / beat_flux / beat_intensity → per-beat loudness
      beat_onset                               → percussive hit flag
      segments                                 → section structure
      bpm                                      → tempo for BPM-aware clamping
    """
    beats_raw      = data.get("beats", [])
    downbeats_raw  = data.get("downbeats", [])
    beat_pos_raw   = data.get("beat_positions", [])
    segments_raw   = data.get("segments", [])
    bpm            = float(data.get("bpm", data.get("tempo", 120.0)) or 120.0)

    if not beats_raw:
        raise ValueError("JSON has no 'beats' list — run decodeur.py first.")

    beat_times: list[float] = [float(x) for x in beats_raw]
    n = len(beat_times)

    # ── Per-beat features ─────────────────────────────────────────
    # decodeur.py v2 writes pre-interpolated per-beat arrays.
    # Fall back to zeros if absent (for older JSONs).
    b_intensity = _extract_beat_feature(data, "beat_intensity")
    b_onset     = _extract_beat_onset(data)

    def _feat(lst: list[float], idx: int) -> float:
        return lst[idx] if idx < len(lst) else 0.5

    def _ons(lst: list[bool], idx: int) -> bool:
        return lst[idx] if idx < len(lst) else False

    # ── Segment index ─────────────────────────────────────────────
    seg_index = _build_seg_index(segments_raw) if segments_raw else []

    # ── Average beat interval (fallback for last beat) ────────────
    avg_dt = (beat_times[-1] - beat_times[0]) / max(1, n - 1) if n > 1 else 60.0 / bpm

    # ── Downbeat set (timestamp-based, ±50 ms tolerance) ─────────
    downbeat_ts: set[int] = set()
    for db_raw in downbeats_raw:
        try:
            db_f = float(db_raw)
        except (TypeError, ValueError):
            continue
        closest = min(range(n), key=lambda i, db=db_f: abs(beat_times[i] - db))
        if abs(beat_times[closest] - db_f) < 0.050:
            downbeat_ts.add(closest)

    # ── Bar counter ───────────────────────────────────────────────
    bar_idx   = 0
    bar_counter: list[int] = []
    for i in range(n):
        try:
            bp = int(beat_pos_raw[i]) if i < len(beat_pos_raw) else (i % 4) + 1
        except (TypeError, ValueError):
            bp = (i % 4) + 1
        if bp == 1 or i in downbeat_ts:
            bar_idx += 1
        bar_counter.append(bar_idx)

    # ── Build cue list ────────────────────────────────────────────
    cues: list[TimelineCue] = []

    for i, t_s in enumerate(beat_times):

        # Beat position
        try:
            bp = int(beat_pos_raw[i]) if i < len(beat_pos_raw) else (i % 4) + 1
        except (TypeError, ValueError):
            bp = (i % 4) + 1
        if bp not in (1, 2, 3, 4):
            bp = (i % 4) + 1

        # Is this a downbeat?
        is_downbeat = (i in downbeat_ts) or (bp == 1)

        # Beat interval
        dt = max(0.08, beat_times[i + 1] - t_s) if i + 1 < n else avg_dt

        # Per-beat features
        intensity = _feat(b_intensity, i)
        onset     = _ons(b_onset, i)

        # Section aggression (base)
        base_agg  = _aggression_at(t_s, seg_index) if seg_index else 1

        # Final aggression (modulated by intensity + onset)
        aggression = _compute_aggression(base_agg, intensity, onset)

        # Command + hold
        cmd, hold_s = _select_move(
            beat_pos   = bp,
            bar_idx    = bar_counter[i],
            dt         = dt,
            aggression = aggression,
            intensity  = intensity,
            onset      = onset,
            bpm        = bpm,
        )

        # Cue label (for ROS log)
        section  = _label_at(t_s, seg_index) if seg_index else ""
        star     = "★" if is_downbeat else " "
        name = (
            f"{star} t={t_s:.3f}s "
            f"bar={bar_counter[i]:02d} pos={bp} "
            f"agg={aggression} I={intensity:.2f} "
            f"{'ONSET ' if onset else ''}"
            f"[{section}]"
        )

        cues.append(TimelineCue(t_s=t_s, name=name, cmd=cmd, hold_s=hold_s))

    # ── Final pose (after last beat STOP) ─────────────────────────
    last  = cues[-1]
    end_t = last.t_s + last.hold_s + 0.20
    cues.append(TimelineCue(
        t_s    = end_t,
        name   = "★ FINAL POSE",
        cmd    = "ACTION:7",
        hold_s = min(0.85, avg_dt * 0.80),
    ))

    return cues


# ═══════════════════════════════════════════════════════════════════
# JSON LOADERS  (universal — handles any beat JSON layout)
# ═══════════════════════════════════════════════════════════════════

def _safe_float(v: Any) -> float | None:
    try:
        f = float(v)
        return f if f >= 0.0 else None
    except (TypeError, ValueError):
        return None


def _compute_cues_end_s(cues: list[TimelineCue]) -> float:
    if not cues:
        return 0.0
    return max(c.t_s + max(0.0, c.hold_s) for c in cues)


def _extract_duration_s(raw: dict[str, Any]) -> float | None:
    candidates: list[float] = []
    for key in ("duration", "duration_s", "song_duration", "track_duration", "length"):
        v = _safe_float(raw.get(key))
        if v:
            candidates.append(v)
    meta = raw.get("metadata")
    if isinstance(meta, dict):
        for key in ("duration", "duration_s"):
            v = _safe_float(meta.get(key))
            if v:
                candidates.append(v)
    segs = raw.get("segments")
    if isinstance(segs, list):
        for seg in segs:
            if isinstance(seg, dict):
                v = _safe_float(seg.get("end"))
                if v:
                    candidates.append(v)
    beats = raw.get("beats")
    if isinstance(beats, list) and beats:
        v = _safe_float(max(beats))
        if v:
            candidates.append(v)
    return max(candidates) if candidates else None


def _normalize_beat_dict(raw: dict[str, Any]) -> dict[str, Any] | None:
    """Normalize any beat-JSON layout into a canonical payload dict."""
    containers: list[dict[str, Any]] = [raw]
    for key in ("analysis", "rhythm", "timeline", "music"):
        nested = raw.get(key)
        if isinstance(nested, dict):
            containers.append(nested)

    def _pick_list(d: dict[str, Any], *keys: str) -> list[Any] | None:
        for k in keys:
            v = d.get(k)
            if isinstance(v, list) and v:
                return v
        return None

    beats: list[Any] | None = None
    src:   dict[str, Any] | None = None
    for c in containers:
        beats = _pick_list(c, "beats", "beat_times", "beatTimes")
        if beats is not None:
            src = c
            break

    if beats is None or src is None:
        return None

    downbeats = (
        _pick_list(src, "downbeats", "down_beats", "downBeats")
        or _pick_list(raw, "downbeats", "down_beats", "downBeats")
        or []
    )
    beat_positions = (
        _pick_list(src, "beat_positions", "beatPositions", "positions")
        or _pick_list(raw, "beat_positions", "beatPositions", "positions")
        or []
    )
    segments = (
        _pick_list(raw, "segments") or _pick_list(src, "segments") or []
    )
    audio_path = (
        raw.get("path") or raw.get("audio_path") or raw.get("audioFile")
        or src.get("path") or src.get("audio_path") or src.get("audioFile")
    )
    bpm = (
        raw.get("bpm") or raw.get("tempo")
        or src.get("bpm") or src.get("tempo")
    )

    return {
        "beats":          beats,
        "downbeats":      downbeats,
        "beat_positions": beat_positions,
        "segments":       segments,
        "audio_path":     str(audio_path) if audio_path else None,
        "bpm":            _safe_float(bpm),
        "duration_s":     _extract_duration_s(raw),
        # ── decodeur.py v2 per-beat features ─────────────────────
        "beat_intensity": _pick_list(raw, "beat_intensity") or [],
        "beat_energy":    _pick_list(raw, "beat_energy")    or [],
        "beat_flux":      _pick_list(raw, "beat_flux")      or [],
        "beat_onset":     raw.get("beat_onset", []),
        # ── legacy full-array features (ignored but passed through) ─
        "intensity":      _pick_list(raw, "intensity")       or [],
        "energy":         _pick_list(raw, "energy")          or [],
        "spectral_flux":  _pick_list(raw, "spectral_flux")   or [],
        "onsets":         raw.get("onsets", []),
    }


def _load_native_cues(path: str) -> list[TimelineCue]:
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)
    if not isinstance(raw, list):
        raise ValueError("Native timeline JSON must be a top-level list")
    cues: list[TimelineCue] = []
    for i, item in enumerate(raw):
        if not isinstance(item, dict):
            raise ValueError(f"Item #{i} must be an object")
        t_s    = float(item["t"])
        name   = str(item.get("name", f"Cue {i + 1}"))
        cmd    = str(item["cmd"])
        hold_s = float(item.get("hold", 0.0))
        if t_s < 0:
            raise ValueError(f"Item #{i}: negative t={t_s}")
        cues.append(TimelineCue(t_s=t_s, name=name, cmd=cmd, hold_s=hold_s))
    cues.sort(key=lambda c: c.t_s)
    return cues


def load_timeline_or_beats(path: str) -> TimelineSelection:
    """Universal loader: beat JSON (decodeur output) or native cue list."""
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)

    if isinstance(raw, list):
        cues = _load_native_cues(path)
        return TimelineSelection(
            cues=cues, source_type="timeline", timeline_file=path,
            duration_s=_compute_cues_end_s(cues),
        )

    if isinstance(raw, dict):
        payload = _normalize_beat_dict(raw)
        if payload and payload.get("beats"):
            cues = build_timeline_from_beats(payload)
            return TimelineSelection(
                cues          = cues,
                source_type   = "beats",
                timeline_file = path,
                audio_path    = payload["audio_path"],
                bpm           = payload["bpm"],
                duration_s    = payload["duration_s"],
            )

    cues = _load_native_cues(path)
    return TimelineSelection(
        cues=cues, source_type="timeline", timeline_file=path,
        duration_s=_compute_cues_end_s(cues),
    )


# ═══════════════════════════════════════════════════════════════════
# STATIC CHOREOGRAPHY  (no JSON — loop-based fallback)
# ═══════════════════════════════════════════════════════════════════

def choreography(speed: int, step_width: int) -> list[Step]:
    _ = step_width
    base = [
        Step("Forward groove", "MOVE:forward",   0.9),
        Step("Forward groove", "MOVE:forward",   0.9),
        Step("Left hit",       "MOVE:left",      0.5),
        Step("Right hit",      "MOVE:right",     0.5),
        Step("Turn left",      "MOVE:turnleft",  0.7),
        Step("Turn right",     "MOVE:turnright", 0.7),
        Step("Back groove",    "MOVE:back",      0.7),
        Step("Pose",           "ACTION:1",       1.2),
        Step("Slide left",     "MOVE:left",      0.5),
        Step("Slide right",    "MOVE:right",     0.5),
        Step("Slide left",     "MOVE:left",      0.5),
        Step("Slide right",    "MOVE:right",     0.5),
        Step("Forward hit",    "MOVE:forward",   0.6),
        Step("Forward hit",    "MOVE:forward",   0.6),
        Step("Spin",           "MOVE:turnleft",  1.0),
        Step("Crouch",         "ACTION:7",       1.2),
    ]
    variation = [
        Step("Fast left",      "MOVE:left",      0.4),
        Step("Fast right",     "MOVE:right",     0.4),
        Step("Fast left",      "MOVE:left",      0.4),
        Step("Fast right",     "MOVE:right",     0.4),
        Step("Forward attack", "MOVE:forward",   0.6),
        Step("Back attack",    "MOVE:back",      0.6),
        Step("Turn combo",     "MOVE:turnright", 0.8),
        Step("Turn combo",     "MOVE:turnleft",  0.8),
        Step("Big pose",       "ACTION:8",       1.5),
    ]
    finale = [
        Step("Slow forward",   "MOVE:forward",  1.0),
        Step("Slow forward",   "MOVE:forward",  1.0),
        Step("Final spin",     "MOVE:turnleft", 1.2),
        Step("Final pose",     "ACTION:1",      2.0),
        Step("Final crouch",   "ACTION:7",      2.0),
    ]
    steps = list(base)
    for _ in range(6):
        steps += base
        steps += variation
    steps += finale
    return steps


# ═══════════════════════════════════════════════════════════════════
# ROS2 NODE
# ═══════════════════════════════════════════════════════════════════

class DanceLeader(Node):

    def __init__(
        self,
        loops:        int,
        beat:         float,
        speed:        int,
        step_width:   int,
        timeline_path: str | None,
        song_delay:   float,
        audio_file:   str | None,
        audio_name:   str | None,
        audio_dir:    str,
        audio_player: str,
        play_audio:   bool,
    ) -> None:
        super().__init__("dance_leader")
        self.loops         = loops
        self.beat          = beat
        self.speed         = speed
        self.step_width    = step_width
        self.timeline_path = timeline_path
        self.song_delay    = song_delay
        self.audio_file    = audio_file
        self.audio_name    = audio_name
        self.audio_dir     = audio_dir
        self.audio_player  = audio_player
        self.play_audio    = play_audio
        self._audio_proc: subprocess.Popen[str] | None = None

        self._pub = self.create_publisher(String, DANCE_TOPIC, qos_profile=10)
        self.get_logger().info(f"Dance leader — topic: '{DANCE_TOPIC}'")
        self.get_logger().info(
            f"loops={loops}  beat={beat}  speed={speed}  step_width={step_width}"
        )
        if timeline_path:
            self.get_logger().info(f"Timeline: {timeline_path}  delay={song_delay}s")

    # ── Audio helpers ────────────────────────────────────────────────

    def _pick_audio_player(self) -> list[str] | None:
        if self.audio_player != "auto":
            if shutil.which(self.audio_player) is None:
                self.get_logger().warning(
                    f"Audio player '{self.audio_player}' not in PATH"
                )
                return None
            return [self.audio_player]
        for player, args in [
            ("ffplay",  ["-nodisp", "-autoexit", "-loglevel", "error"]),
            ("mpg123",  ["-q"]),
            ("cvlc",    ["--play-and-exit", "--intf", "dummy"]),
            ("paplay",  []),
        ]:
            if shutil.which(player):
                return [player, *args]
        return None

    def _spawn_audio(self, audio_path: str) -> subprocess.Popen[str] | None:
        """Launch audio player in background — no blocking sleep."""
        prefix = self._pick_audio_player()
        if prefix is None:
            self.get_logger().warning(
                "No audio player found (ffplay / mpg123 / cvlc / paplay). "
                "Install one or use --audio-player."
            )
            return None
        cmd = [*prefix, audio_path]
        try:
            proc = subprocess.Popen(cmd)
            self.get_logger().info(f"Audio spawned: {' '.join(cmd)}")
            return proc
        except Exception as exc:
            self.get_logger().warning(f"Failed to spawn audio: {exc}")
            return None

    def _resolve_audio(self, selection: TimelineSelection) -> str | None:
        """Priority: --audio-file  >  --audio-name  >  JSON path field."""
        if self.audio_file:
            p = Path(self.audio_file).expanduser()
            return str(p) if p.exists() else None
        if self.audio_name:
            base = Path(self.audio_dir).expanduser()
            direct = base / self.audio_name
            if direct.exists():
                return str(direct)
            for ext in (".mp3", ".wav", ".ogg", ".flac"):
                c = base / f"{self.audio_name}{ext}"
                if c.exists():
                    return str(c)
            return None
        if selection.audio_path:
            p = Path(selection.audio_path)
            if p.exists():
                return str(p)
        return None

    def _stop_audio(self) -> None:
        if self._audio_proc is None:
            return
        if self._audio_proc.poll() is not None:
            self._audio_proc = None
            return
        self.get_logger().info("Stopping audio…")
        self._audio_proc.terminate()
        try:
            self._audio_proc.wait(timeout=1.5)
        except Exception:
            self._audio_proc.kill()
        self._audio_proc = None

    def _wait_audio_done(self) -> None:
        if self._audio_proc is None:
            return
        self.get_logger().info("Waiting for audio to finish…")
        while rclpy.ok():
            if self._audio_proc is None:
                return
            if self._audio_proc.poll() is not None:
                self._audio_proc = None
                self.get_logger().info("Audio finished.")
                return
            rclpy.spin_once(self, timeout_sec=0.20)

    # ── ROS helpers ──────────────────────────────────────────────────

    def _pub_cmd(self, cmd: str) -> None:
        msg      = String()
        msg.data = cmd
        self._pub.publish(msg)
        self.get_logger().info(f">> {cmd}")

    def _sleep(self, seconds: float) -> None:
        deadline = time.time() + seconds
        while time.time() < deadline:
            rclpy.spin_once(self, timeout_sec=0.05)

    def _wait_until(self, target_t: float) -> None:
        """Busy-spin until monotonic clock reaches target_t.

        Spins ROS callbacks at ≤ 15 ms granularity so the event loop
        stays alive without sacrificing timing accuracy.
        """
        while True:
            now = time.monotonic()
            if now >= target_t:
                return
            rem = target_t - now
            rclpy.spin_once(self, timeout_sec=min(0.015, max(0.001, rem)))

    # ── Timeline runner ──────────────────────────────────────────────

    def _run_timeline(
        self,
        cues:           list[TimelineCue],
        selected_audio: str | None,
    ) -> bool:
        """Execute cues in perfect lock-step with the audio.

        Synchronisation model
        ─────────────────────
        T_spawn  ← time.monotonic() recorded just BEFORE Popen()
        T_sound  = T_spawn + AUDIO_PLAYER_LATENCY_S  (1st audible sample)
        start_t  = T_spawn + AUDIO_PLAYER_LATENCY_S

        Each cue fires at:
            wall_clock = start_t + cue.t_s

        For cue.t_s = 0  →  wall_clock = T_sound  →  in sync with beat 0. ✓

        STOP fires at:
            wall_clock = start_t + cue.t_s + cue.hold_s

        _wait_until() spins ROS at ≤ 15 ms to keep callbacks alive
        while maintaining sub-20 ms timing accuracy.
        """
        if not cues:
            self.get_logger().warning("Timeline is empty.")
            return False

        n = len(cues)
        self.get_logger().info(f"Timeline: {n} cues loaded")

        # ── 1. Countdown ──────────────────────────────────────────
        if self.song_delay > 0:
            self.get_logger().info(f"Countdown: {self.song_delay:.1f}s…")
            self._sleep(self.song_delay)

        # ── 2. Record spawn epoch, then launch audio ──────────────
        # We record T_spawn BEFORE Popen so latency compensation is
        # relative to the instant we handed the command to the OS.
        T_spawn = time.monotonic()
        audio_started = False
        if self.play_audio and selected_audio:
            self._audio_proc = self._spawn_audio(selected_audio)
            audio_started    = self._audio_proc is not None

        # start_t is the estimated wall-clock time of the 1st musical sample
        start_t = T_spawn + AUDIO_PLAYER_LATENCY_S

        self.get_logger().info(
            f"▶▶▶  MUSIC + CHOREOGRAPHY  —  "
            f"latency_comp={AUDIO_PLAYER_LATENCY_S*1000:.0f}ms  "
            f"audio={'YES' if audio_started else 'NO'}"
        )

        # ── 3. Hot cue loop ───────────────────────────────────────
        for i, cue in enumerate(cues, start=1):
            fire_t = start_t + cue.t_s
            self._wait_until(fire_t)

            self.get_logger().info(
                f"[{i:04d}/{n}]  {cue.name}  →  {cue.cmd}"
                f"  hold={cue.hold_s:.3f}s"
            )
            self._pub_cmd(cue.cmd)

            if cue.hold_s > 0.0:
                self._wait_until(fire_t + cue.hold_s)
                self._pub_cmd("STOP")

        return audio_started

    # ── Main ─────────────────────────────────────────────────────────

    def run(self) -> None:
        self.get_logger().info("Waiting 2 s for followers to connect…")
        self._sleep(2.0)

        self._pub_cmd(f"SPEED:{self.speed}")
        self._sleep(0.3)

        if self.timeline_path:
            selection = load_timeline_or_beats(self.timeline_path)

            self.get_logger().info("══════════════════════════════════════")
            self.get_logger().info(f"  File     : {selection.timeline_file}")
            self.get_logger().info(f"  Type     : {selection.source_type}")
            self.get_logger().info(f"  Cues     : {len(selection.cues)}")
            if selection.bpm is not None:
                self.get_logger().info(f"  BPM      : {selection.bpm:.2f}")
            if selection.duration_s is not None:
                self.get_logger().info(
                    f"  Duration : {selection.duration_s:.1f}s "
                    f"({selection.duration_s / 60:.1f} min)"
                )
            if selection.audio_path:
                self.get_logger().info(f"  Audio    : {selection.audio_path}")
            self.get_logger().info("══════════════════════════════════════")

            selected_audio = self._resolve_audio(selection)
            if selected_audio:
                self.get_logger().info(f"Autoplay  : {selected_audio}")
            else:
                self.get_logger().warning(
                    "No audio file found.  "
                    "Pass  --audio-file /path/to/song.mp3  or  --audio-name TRACKNAME"
                )

            audio_started = self._run_timeline(selection.cues, selected_audio)
            self._wait_audio_done()

            # If audio was not played, still honour the track duration
            if not audio_started and selection.duration_s is not None:
                cue_end   = _compute_cues_end_s(selection.cues)
                remaining = selection.duration_s - cue_end
                if remaining > 0.1:
                    self.get_logger().info(
                        f"Waiting remaining track time: {remaining:.2f}s"
                    )
                    self._sleep(remaining)

            self._pub_cmd("RESET")
            self._sleep(0.5)
            self._pub_cmd("DONE")
            self.get_logger().info("✔  Timeline choreography complete.")
            self._stop_audio()
            return

        # ── Static loop mode ──────────────────────────────────────
        steps = choreography(self.speed, self.step_width)
        total = len(steps)
        for loop_idx in range(self.loops):
            self.get_logger().info(f"=== Loop {loop_idx + 1}/{self.loops} ===")
            for i, step in enumerate(steps, start=1):
                self.get_logger().info(f"Step {i:02d}/{total}: {step.name}")
                self._pub_cmd(step.cmd)
                self._sleep(step.pause_s * self.beat)
                self._pub_cmd("STOP")
                self._sleep(0.15)

        self._pub_cmd("RESET")
        self._sleep(0.5)
        self._pub_cmd("DONE")
        self.get_logger().info("✔  Choreography complete.")


# ═══════════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════════

def parse_args() -> argparse.Namespace:
    import rclpy.utilities
    p = argparse.ArgumentParser(description="MUTO-RS Dance Leader (ROS2)")
    p.add_argument("--loops",        type=int,   default=1,
                   help="Full loop repetitions (static mode only)")
    p.add_argument("--beat",         type=float, default=1.0,
                   help="Tempo multiplier (static mode: <1=faster)")
    p.add_argument("--speed",        type=int,   default=2,
                   help="Speed sent to followers 1–5")
    p.add_argument("--step-width",   type=int,   default=16,
                   help="Locomotion step width 10–25")
    p.add_argument("--timeline",     type=str,   default="",
                   help="Path to beat-JSON (decodeur output) or cue-list JSON")
    p.add_argument("--song-delay",   type=float, default=5.0,
                   help="Countdown before music starts (seconds)")
    p.add_argument("--audio-file",   type=str,   default="",
                   help="Explicit path to audio file")
    p.add_argument("--audio-name",   type=str,   default="",
                   help="Filename in --audio-dir (extension optional)")
    p.add_argument("--audio-dir",    type=str,   default="assets/audio",
                   help="Directory where audio files are stored")
    p.add_argument("--audio-player", type=str,   default="auto",
                   help="Player binary: auto | ffplay | mpg123 | cvlc | paplay")
    p.add_argument("--play-audio",   type=str,   default="true",
                   choices=["true", "false"],
                   help="Enable automatic audio playback in timeline mode")
    return p.parse_args(rclpy.utilities.remove_ros_args(sys.argv)[1:])


def main() -> int:
    args = parse_args()
    rclpy.init()
    node = DanceLeader(
        loops         = max(1, args.loops),
        beat          = max(0.2, args.beat),
        speed         = max(1, min(5, args.speed)),
        step_width    = max(10, min(25, args.step_width)),
        timeline_path = args.timeline if args.timeline else None,
        song_delay    = max(0.0, args.song_delay),
        audio_file    = args.audio_file  if args.audio_file  else None,
        audio_name    = args.audio_name  if args.audio_name  else None,
        audio_dir     = args.audio_dir,
        audio_player  = args.audio_player,
        play_audio    = (args.play_audio.lower() == "true"),
    )
    try:
        node.run()
    except KeyboardInterrupt:
        node.get_logger().info("Interrupted — STOP + RESET")
        node._pub_cmd("STOP")
        time.sleep(0.3)
        node._pub_cmd("RESET")
        node._stop_audio()
    finally:
        node._stop_audio()
        node.destroy_node()
        rclpy.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())