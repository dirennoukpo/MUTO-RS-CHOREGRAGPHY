#!/usr/bin/env python3
"""ROS2 dance leader node for MUTO-RS multi-robot synchronization.

Protocol (std_msgs/String data field):
  SPEED:<int>       -> set speed level (1-5)
  ACTION:<int>      -> built-in action 1-8
  MOVE:<direction>  -> forward|back|left|right|turnleft|turnright
  STOP              -> stop movement
  RESET             -> neutral stance
  DONE              -> choreography finished

Built-in action IDs:
  1=Stretch  2=Greeting  3=Retreat  4=Warm_up
  5=Turn_around  6=Say_no  7=Crouching  8=Stride

Usage:
  python3 dance_leader.py --timeline beats.json --audio-file song.mp3
  python3 dance_leader.py --loops 2 --beat 0.8 --speed 3
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

# Compensates for audio player open+buffer latency (seconds).
# Increase if robot moves BEFORE the beat; decrease if it moves AFTER.
AUDIO_PLAYER_LATENCY_S = 0.10

# ---------------------------------------------------------------------------
# SECTION AGGRESSION MAP
# Maps segment label -> aggression level 0-3
#   0 = calm (start, silence)
#   1 = low  (intro, outro)
#   2 = medium (verse, bridge)
#   3 = HIGH (chorus, drop, hook)
# ---------------------------------------------------------------------------
SECTION_AGGRESSION: dict[str, int] = {
    "start":      0,
    "silent":     0,
    "intro":      1,
    "outro":      1,
    "bridge":     2,
    "verse":      2,
    "pre-chorus": 2,
    "pre_chorus": 2,
    "chorus":     3,
    "drop":       3,
    "hook":       3,
    "refrain":    3,
}

# ---------------------------------------------------------------------------
# MOVEMENT PALETTES
#
# Each palette is keyed by beat position (1-4).
# Each value is a list of (command, hold_factor) tuples.
# hold_factor is applied to the beat interval to compute hold_seconds.
# The list is rotated by bar_index to provide variety without randomness.
#
# Design rules:
#   Beat 1 (downbeat): biggest accent, longest hold (0.70-0.78 x dt)
#   Beat 2: lateral snap, medium hold (0.45-0.54 x dt)
#   Beat 3 (backbeat): counter-move, medium-long hold (0.55-0.68 x dt)
#   Beat 4: pickup/anticipation, short hold (0.40-0.50 x dt)
# ---------------------------------------------------------------------------

# Level 0 — calm: very gentle, let the robot breathe
_PAL0: dict[int, list[tuple[str, float]]] = {
    1: [("ACTION:1", 0.70), ("ACTION:4", 0.68)],
    2: [("MOVE:left", 0.42), ("MOVE:right", 0.42)],
    3: [("ACTION:6", 0.52), ("MOVE:back", 0.44)],
    4: [("MOVE:right", 0.38), ("MOVE:left", 0.38)],
}

# Level 1 — intro/outro: smooth, expressive, deliberate
_PAL1: dict[int, list[tuple[str, float]]] = {
    1: [
        ("MOVE:forward", 0.72), ("ACTION:2", 0.72),   # Greeting
        ("ACTION:1", 0.70),     ("MOVE:forward", 0.72),
    ],
    2: [("MOVE:left", 0.50), ("MOVE:right", 0.50)],
    3: [
        ("MOVE:back", 0.60),    ("ACTION:6", 0.58),
        ("MOVE:turnright", 0.58), ("ACTION:3", 0.56),  # Retreat
    ],
    4: [("MOVE:right", 0.44), ("MOVE:left", 0.44)],
}

# Level 2 — verse: energetic, narrative, varied
_PAL2: dict[int, list[tuple[str, float]]] = {
    1: [
        ("MOVE:forward",  0.75), ("ACTION:8",  0.75),   # Stride
        ("MOVE:turnleft", 0.72), ("ACTION:1",  0.72),   # Stretch spin
        ("MOVE:forward",  0.75), ("ACTION:7",  0.73),   # Crouch punch
        ("MOVE:turnright",0.70), ("ACTION:4",  0.70),   # Warm-up spin
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

# Level 3 — chorus/drop: MAXIMUM aggression, explosive, relentless
_PAL3: dict[int, list[tuple[str, float]]] = {
    1: [
        ("MOVE:forward",  0.78), ("ACTION:8",  0.78),   # Stride — slam
        ("MOVE:turnleft", 0.76), ("ACTION:7",  0.76),   # Crouch spin
        ("ACTION:5",      0.78), ("MOVE:forward", 0.78),# Turn-around
        ("ACTION:8",      0.78), ("MOVE:turnright",0.76),
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
        ("MOVE:back",      0.68), ("ACTION:7",       0.70),  # Crouch backbeat
        ("MOVE:turnright", 0.68), ("ACTION:6",       0.65),
    ],
    4: [
        ("MOVE:right", 0.50), ("MOVE:left",  0.50),
        ("MOVE:right", 0.50), ("MOVE:left",  0.50),
        ("MOVE:right", 0.50), ("MOVE:left",  0.50),
        ("MOVE:right", 0.50), ("MOVE:left",  0.50),
    ],
}

_PALETTES = {0: _PAL0, 1: _PAL1, 2: _PAL2, 3: _PAL3}


# ===========================================================================
# DATA CLASSES
# ===========================================================================

class Step:
    def __init__(self, name: str, cmd: str, pause_s: float) -> None:
        self.name = name
        self.cmd = cmd
        self.pause_s = pause_s


class TimelineCue:
    def __init__(self, t_s: float, name: str, cmd: str, hold_s: float = 0.0) -> None:
        self.t_s = t_s
        self.name = name
        self.cmd = cmd
        self.hold_s = hold_s


class TimelineSelection:
    def __init__(
        self,
        cues: list[TimelineCue],
        source_type: str,
        timeline_file: str,
        audio_path: str | None = None,
        bpm: float | None = None,
        duration_s: float | None = None,
    ) -> None:
        self.cues = cues
        self.source_type = source_type
        self.timeline_file = timeline_file
        self.audio_path = audio_path
        self.bpm = bpm
        self.duration_s = duration_s


# ===========================================================================
# SEGMENT INDEXING
# ===========================================================================

def _build_seg_index(
    segments: list[dict[str, Any]]
) -> list[tuple[float, float, str]]:
    index: list[tuple[float, float, str]] = []
    for seg in segments:
        try:
            s = float(seg["start"])
            e = float(seg["end"])
            label = str(seg.get("label", "intro")).lower().strip()
            index.append((s, e, label))
        except (KeyError, TypeError, ValueError):
            continue
    index.sort(key=lambda x: x[0])
    return index


def _label_at(t: float, index: list[tuple[float, float, str]]) -> str:
    """Return segment label active at time t."""
    last = "intro"
    for s, e, lbl in index:
        if s <= t < e:
            return lbl
        if s > t:
            break
        last = lbl
    return last


def _aggression_at(t: float, index: list[tuple[float, float, str]]) -> int:
    return SECTION_AGGRESSION.get(_label_at(t, index), 1)


# ===========================================================================
# CHOREOGRAPHY ENGINE  —  the core of the whole system
# ===========================================================================

def _select_move(
    beat_pos: int,
    bar_idx: int,
    dt: float,
    aggression: int,
) -> tuple[str, float]:
    """Pick the command and compute hold_seconds for one beat.

    Parameters
    ----------
    beat_pos   : position inside the bar (1-4)
    bar_idx    : monotonically increasing bar counter (for palette rotation)
    dt         : seconds until next beat (beat interval)
    aggression : section aggression level 0-3

    Returns
    -------
    (cmd, hold_s) where STOP will be sent after hold_s seconds
    """
    palette = _PALETTES[max(0, min(3, aggression))]
    eff_pos = beat_pos if beat_pos in (1, 2, 3, 4) else 4

    moves = palette[eff_pos]
    cmd, factor = moves[bar_idx % len(moves)]

    # Clamp: STOP must arrive >= 120 ms before the next beat so the robot
    # finishes the command and has a brief neutral frame before the next hit.
    max_hold = max(0.05, dt - 0.12)
    hold_s = max(0.08, min(max_hold, dt * factor))

    return cmd, hold_s


def build_timeline_from_beats(data: dict[str, Any]) -> list[TimelineCue]:
    """Convert beat-analysis JSON into a precise choreography cue list.

    The engine reads every available field to make the best decision:
      - beat_positions (1-4) for phrasing
      - downbeats for bar-1 confirmation
      - segments for section-aware aggression
      - beat intervals for exact hold timing

    Every cue fires on the exact beat timestamp (t_s).
    STOP fires at t_s + hold_s, always before the next beat.
    """
    beats = data.get("beats", [])
    downbeats = data.get("downbeats", [])
    beat_positions = data.get("beat_positions", data.get("beatPositions", []))
    segments_raw = data.get("segments", [])

    if not isinstance(beats, list) or not beats:
        raise ValueError("Beat JSON must contain a non-empty 'beats' list")

    beat_times = [float(x) for x in beats]
    n = len(beat_times)

    # Downbeat lookup with 3 ms tolerance
    downbeat_set: set[int] = set()
    if isinstance(downbeats, list):
        for db in downbeats:
            try:
                db_f = float(db)
                # Find the beat index closest to this downbeat timestamp
                closest = min(range(n), key=lambda i: abs(beat_times[i] - db_f))
                if abs(beat_times[closest] - db_f) < 0.05:
                    downbeat_set.add(closest)
            except (TypeError, ValueError):
                pass

    # Segment index
    seg_index = _build_seg_index(segments_raw) if segments_raw else []

    # Average beat interval (fallback for last beat)
    if n > 1:
        avg_dt = (beat_times[-1] - beat_times[0]) / (n - 1)
    else:
        avg_dt = 60.0 / 95.0  # sensible default

    # Pre-compute bar index: increments every time we hit beat_pos==1
    bar_idx = 0
    bar_counter: list[int] = []
    for i in range(n):
        bp_raw = None
        if isinstance(beat_positions, list) and i < len(beat_positions):
            try:
                bp_raw = int(beat_positions[i])
            except (TypeError, ValueError):
                pass
        if bp_raw == 1 or i in downbeat_set:
            bar_idx += 1
        bar_counter.append(bar_idx)

    # Build cues
    cues: list[TimelineCue] = []
    for i, t_s in enumerate(beat_times):

        # Beat position (default to cycling 1-4 if missing)
        bp: int = ((i % 4) + 1)
        if isinstance(beat_positions, list) and i < len(beat_positions):
            try:
                bp_raw = int(beat_positions[i])
                if bp_raw in (1, 2, 3, 4):
                    bp = bp_raw
            except (TypeError, ValueError):
                pass

        is_downbeat = (i in downbeat_set) or (bp == 1)

        # Interval to next beat
        if i + 1 < n:
            dt = max(0.08, beat_times[i + 1] - t_s)
        else:
            dt = avg_dt

        # Section aggression
        aggression = _aggression_at(t_s, seg_index) if seg_index else 1

        # Select move
        cmd, hold_s = _select_move(
            beat_pos=bp,
            bar_idx=bar_counter[i],
            dt=dt,
            aggression=aggression,
        )

        # Build label for logging
        section = _label_at(t_s, seg_index) if seg_index else ""
        star = "★" if is_downbeat else " "
        label_parts = [
            f"{star}",
            f"t={t_s:.3f}s",
            f"bar={bar_counter[i]:02d}",
            f"pos={bp}",
            f"agg={aggression}",
        ]
        if section:
            label_parts.append(f"[{section}]")
        name = "  ".join(label_parts)

        cues.append(TimelineCue(t_s=t_s, name=name, cmd=cmd, hold_s=hold_s))

    # Final power pose — must start AFTER last beat STOP has fired
    last_cue = cues[-1]
    end_t = last_cue.t_s + last_cue.hold_s + 0.15
    cues.append(TimelineCue(
        t_s=end_t,
        name="★ FINAL POSE",
        cmd="ACTION:7",
        hold_s=min(0.85, avg_dt * 0.80),
    ))

    return cues


# ===========================================================================
# JSON LOADERS
# ===========================================================================

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
    """Extract and normalize beat payload from any known JSON layout."""
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
    src: dict[str, Any] | None = None
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
    bpm = raw.get("bpm") if raw.get("bpm") is not None else src.get("bpm")

    return {
        "beats":         beats,
        "downbeats":     downbeats,
        "beat_positions": beat_positions,
        "segments":      segments,
        "audio_path":    str(audio_path) if audio_path else None,
        "bpm":           _safe_float(bpm),
        "duration_s":    _extract_duration_s(raw),
    }


def _load_native_cues(path: str) -> list[TimelineCue]:
    """Load a native cue-list JSON: [{t, cmd, name?, hold?}, ...]"""
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)
    if not isinstance(raw, list):
        raise ValueError("Native timeline JSON must be a list")
    cues: list[TimelineCue] = []
    for i, item in enumerate(raw):
        if not isinstance(item, dict):
            raise ValueError(f"Item #{i} must be an object")
        t_s = float(item["t"])
        name = str(item.get("name", f"Cue {i + 1}"))
        cmd = str(item["cmd"])
        hold_s = float(item.get("hold", 0.0))
        if t_s < 0:
            raise ValueError(f"Item #{i}: negative t={t_s}")
        cues.append(TimelineCue(t_s=t_s, name=name, cmd=cmd, hold_s=hold_s))
    cues.sort(key=lambda c: c.t_s)
    return cues


def load_timeline_or_beats(path: str) -> TimelineSelection:
    """Universal loader: handles beat JSON and native cue lists."""
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)

    # Native cue list
    if isinstance(raw, list):
        cues = _load_native_cues(path)
        return TimelineSelection(
            cues=cues, source_type="timeline", timeline_file=path,
            duration_s=_compute_cues_end_s(cues),
        )

    # Beat-analysis dict
    if isinstance(raw, dict):
        payload = _normalize_beat_dict(raw)
        if payload and payload.get("beats"):
            cues = build_timeline_from_beats(payload)
            return TimelineSelection(
                cues=cues,
                source_type="beats",
                timeline_file=path,
                audio_path=payload["audio_path"],
                bpm=payload["bpm"],
                duration_s=payload["duration_s"],
            )

    # Fallback
    cues = _load_native_cues(path)
    return TimelineSelection(
        cues=cues, source_type="timeline", timeline_file=path,
        duration_s=_compute_cues_end_s(cues),
    )


# ===========================================================================
# STATIC CHOREOGRAPHY (no beat JSON)
# ===========================================================================

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


# ===========================================================================
# ROS2 NODE
# ===========================================================================

class DanceLeader(Node):
    def __init__(
        self,
        loops: int,
        beat: float,
        speed: int,
        step_width: int,
        timeline_path: str | None,
        song_delay: float,
        audio_file: str | None,
        audio_name: str | None,
        audio_dir: str,
        audio_player: str,
        play_audio: bool,
    ) -> None:
        super().__init__("dance_leader")
        self.loops = loops
        self.beat = beat
        self.speed = speed
        self.step_width = step_width
        self.timeline_path = timeline_path
        self.song_delay = song_delay
        self.audio_file = audio_file
        self.audio_name = audio_name
        self.audio_dir = audio_dir
        self.audio_player = audio_player
        self.play_audio = play_audio
        self._audio_proc: subprocess.Popen[str] | None = None

        self._pub = self.create_publisher(String, DANCE_TOPIC, qos_profile=10)
        self.get_logger().info(f"Dance leader ready — topic: '{DANCE_TOPIC}'")
        self.get_logger().info(
            f"Config: loops={loops} beat={beat} speed={speed} step_width={step_width}"
        )
        if timeline_path:
            self.get_logger().info(f"Timeline: {timeline_path}  delay={song_delay}s")

    # ── Audio ────────────────────────────────────────────────────────────

    def _pick_audio_player(self) -> list[str] | None:
        if self.audio_player != "auto":
            if shutil.which(self.audio_player) is None:
                self.get_logger().warning(
                    f"Audio player '{self.audio_player}' not in PATH"
                )
                return None
            return [self.audio_player]
        if shutil.which("ffplay"):
            return ["ffplay", "-nodisp", "-autoexit", "-loglevel", "error"]
        if shutil.which("mpg123"):
            return ["mpg123", "-q"]
        if shutil.which("cvlc"):
            return ["cvlc", "--play-and-exit", "--intf", "dummy"]
        if shutil.which("paplay"):
            return ["paplay"]
        return None

    def _spawn_audio(self, audio_path: str) -> subprocess.Popen[str] | None:
        """Spawn audio player without any blocking sleep."""
        prefix = self._pick_audio_player()
        if prefix is None:
            self.get_logger().warning(
                "No audio player found (ffplay/mpg123/cvlc/paplay). "
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
        """Priority: --audio-file > --audio-name > JSON path field."""
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
            rclpy.spin_once(self, timeout_sec=0.2)

    # ── ROS helpers ──────────────────────────────────────────────────────

    def _pub_cmd(self, cmd: str) -> None:
        msg = String()
        msg.data = cmd
        self._pub.publish(msg)
        self.get_logger().info(f">> {cmd}")

    def _sleep(self, seconds: float) -> None:
        deadline = time.time() + seconds
        while time.time() < deadline:
            rclpy.spin_once(self, timeout_sec=0.05)

    def _wait_until(self, target_t: float) -> None:
        """High-precision monotonic wait, spinning ROS callbacks at <=15ms."""
        while True:
            now = time.monotonic()
            if now >= target_t:
                return
            rem = target_t - now
            rclpy.spin_once(self, timeout_sec=min(0.015, max(0.001, rem)))

    # ── Timeline runner ──────────────────────────────────────────────────

    def _run_timeline(
        self, cues: list[TimelineCue], selected_audio: str | None
    ) -> bool:
        """Execute every cue in exact beat sync with the audio.

        Synchronization model
        ─────────────────────
        1. Countdown via song_delay (audience preparation).
        2. Spawn audio (non-blocking Popen, no sleep).
        3. Record:  start_t = monotonic() - AUDIO_PLAYER_LATENCY_S
           The subtraction pre-compensates for the player's startup time so
           cue timestamps land on the actual musical beat, not after it.
        4. Hot loop:
             fire_wall_clock = start_t + cue.t_s    → send command
             stop_wall_clock = fire_wall_clock + cue.hold_s → send STOP
           _wait_until() spins ROS callbacks at ≤15 ms granularity for
           sub-frame accuracy without blocking the ROS event loop.
        """
        if not cues:
            self.get_logger().warning("Timeline is empty.")
            return False

        n = len(cues)
        self.get_logger().info(f"Timeline loaded: {n} cues")

        # 1. Countdown
        if self.song_delay > 0:
            self.get_logger().info(
                f"Countdown: {self.song_delay:.1f}s before music…"
            )
            self._sleep(self.song_delay)

        # 2. Spawn audio
        audio_started = False
        if self.play_audio and selected_audio:
            self._audio_proc = self._spawn_audio(selected_audio)
            audio_started = self._audio_proc is not None

        # 3. Epoch — compensate player startup latency
        start_t = time.monotonic() - AUDIO_PLAYER_LATENCY_S

        self.get_logger().info(
            f"▶▶▶ CHOREOGRAPHY + MUSIC STARTED  "
            f"[latency_comp={AUDIO_PLAYER_LATENCY_S*1000:.0f}ms]"
        )

        # 4. Hot cue loop
        for i, cue in enumerate(cues, start=1):
            fire_t = start_t + cue.t_s
            self._wait_until(fire_t)

            self.get_logger().info(
                f"[{i:04d}/{n}]  {cue.name}  →  {cue.cmd}  (hold={cue.hold_s:.3f}s)"
            )
            self._pub_cmd(cue.cmd)

            if cue.hold_s > 0.0:
                self._wait_until(fire_t + cue.hold_s)
                self._pub_cmd("STOP")

        return audio_started

    # ── Main ─────────────────────────────────────────────────────────────

    def run(self) -> None:
        self.get_logger().info("Waiting 2 s for followers to connect…")
        self._sleep(2.0)

        self._pub_cmd(f"SPEED:{self.speed}")
        self._sleep(0.3)

        if self.timeline_path:
            selection = load_timeline_or_beats(self.timeline_path)

            self.get_logger().info("══════════════════════════════════")
            self.get_logger().info(f"  File    : {selection.timeline_file}")
            self.get_logger().info(f"  Type    : {selection.source_type}")
            self.get_logger().info(f"  Cues    : {len(selection.cues)}")
            if selection.bpm is not None:
                self.get_logger().info(f"  BPM     : {selection.bpm:.2f}")
            if selection.duration_s is not None:
                self.get_logger().info(
                    f"  Length  : {selection.duration_s:.1f}s "
                    f"({selection.duration_s / 60:.1f} min)"
                )
            if selection.audio_path:
                self.get_logger().info(f"  Audio   : {selection.audio_path}")
            self.get_logger().info("══════════════════════════════════")

            selected_audio = self._resolve_audio(selection)
            if selected_audio:
                self.get_logger().info(f"Autoplay: {selected_audio}")
            else:
                self.get_logger().warning(
                    "No audio found. Use --audio-file /path/to/song.mp3"
                )

            audio_started = self._run_timeline(selection.cues, selected_audio)
            self._wait_audio_done()

            # Respect remaining track duration even without audio autoplay
            if not audio_started and selection.duration_s is not None:
                cue_end = _compute_cues_end_s(selection.cues)
                remaining = selection.duration_s - cue_end
                if remaining > 0.1:
                    self.get_logger().info(
                        f"Waiting track remainder: {remaining:.2f}s"
                    )
                    self._sleep(remaining)

            self._pub_cmd("RESET")
            self._sleep(0.5)
            self._pub_cmd("DONE")
            self.get_logger().info("✔ Timeline choreography complete.")
            self._stop_audio()
            return

        # Static mode
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
        self.get_logger().info("✔ Choreography complete.")


# ===========================================================================
# CLI
# ===========================================================================

def parse_args() -> argparse.Namespace:
    import rclpy.utilities
    p = argparse.ArgumentParser(description="MUTO-RS dance leader (ROS2)")
    p.add_argument("--loops",        type=int,   default=1)
    p.add_argument("--beat",         type=float, default=1.0)
    p.add_argument("--speed",        type=int,   default=2)
    p.add_argument("--step-width",   type=int,   default=16)
    p.add_argument("--timeline",     type=str,   default="")
    p.add_argument("--song-delay",   type=float, default=5.0)
    p.add_argument("--audio-file",   type=str,   default="")
    p.add_argument("--audio-name",   type=str,   default="")
    p.add_argument("--audio-dir",    type=str,   default="assets/audio")
    p.add_argument("--audio-player", type=str,   default="auto")
    p.add_argument("--play-audio",   type=str,   default="true",
                   choices=["true", "false"])
    return p.parse_args(rclpy.utilities.remove_ros_args(sys.argv)[1:])


def main() -> int:
    args = parse_args()
    rclpy.init()
    node = DanceLeader(
        loops=max(1, args.loops),
        beat=max(0.2, args.beat),
        speed=max(1, min(5, args.speed)),
        step_width=max(10, min(25, args.step_width)),
        timeline_path=args.timeline if args.timeline else None,
        song_delay=max(0.0, args.song_delay),
        audio_file=args.audio_file if args.audio_file else None,
        audio_name=args.audio_name if args.audio_name else None,
        audio_dir=args.audio_dir,
        audio_player=args.audio_player,
        play_audio=(args.play_audio.lower() == "true"),
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