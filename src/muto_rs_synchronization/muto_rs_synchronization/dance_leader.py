#!/usr/bin/env python3
"""ROS2 dance leader node for MUTO-RS multi-robot synchronization.

This node runs on the PC / Docker container (no MutoLib needed).
It publishes dance step commands to /dance_cmd so all robots subscribed
on the same ROS_DOMAIN_ID execute the choreography in sync.

Protocol (std_msgs/String data field):
  SPEED:<int>         → set speed level (1-5)
  ACTION:<int>        → execute built-in action by id (1-8)
  MOVE:<direction>    → forward | back | left | right | turnleft | turnright
  STOP                → stop movement
  RESET               → reset to neutral stance
  DONE                → choreography finished, followers clean up

Usage:
  # Same ROS_DOMAIN_ID on PC and all robots (e.g. export ROS_DOMAIN_ID=42)
  python3 dance_leader.py
  python3 dance_leader.py --loops 2 --beat 0.8 --speed 2
"""

from __future__ import annotations

import argparse
import json
import os
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


class Step:
    def __init__(self, name: str, cmd: str, pause_s: float) -> None:
        self.name = name
        self.cmd = cmd          # command string to publish
        self.pause_s = pause_s  # seconds to wait after command before STOP


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
    ) -> None:
        self.cues = cues
        self.source_type = source_type
        self.timeline_file = timeline_file
        self.audio_path = audio_path
        self.bpm = bpm


def choreography(speed: int, step_width: int) -> list[Step]:
    """Return the ordered list of dance steps (mirrors dance_demo.py logic).

    Built-in action IDs:
      1 Stretch  2 Greeting  3 Retreat  4 Warm_up
      5 Turn_around  6 Say_no  7 Crouching  8 Stride
    """
    _ = step_width  # width is embedded in follower's RobotController config
    # return [

    #     #00:00

    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),
    #     Step("Forward hit",  "MOVE:forward",     0.6),

    #     # 0014
    
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide left",   "MOVE:left",        0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),
    #     Step("Slide right",  "MOVE:right",       0.5),




    #     # Step("Forward hit",  "MOVE:forward",     1.1),
    #     # Step("Warm up",      "ACTION:8",         2.4),
    #     # Step("Warm up",      "ACTION:8",         2.4),
    #     # Step("Warm up",      "ACTION:8",         2.4),
    #     # Step("Warm up",      "ACTION:8",         2.4),
    #     # Step("Say hello",    "ACTION:2",         1.6),
    #     # Step("Slide left",   "MOVE:left",        1.0),
    #     # Step("Slide right",  "MOVE:right",       1.0),
    #     # Step("Turn left",    "MOVE:turnleft",    0.9),
    #     # Step("Turn right",   "MOVE:turnright",   0.9),
    #     # Step("Back hit",     "MOVE:back",        0.8),
    #     # Step("Pose stretch", "ACTION:1",         1.8),
    #     # Step("Final crouch", "ACTION:7",         1.8),
    # ]
    base = [

        # ===== PHRASE 1 =====
        Step("Forward groove", "MOVE:forward", 0.9),
        Step("Forward groove", "MOVE:forward", 0.9),

        Step("Left hit",       "MOVE:left",    0.5),
        Step("Right hit",      "MOVE:right",   0.5),

        Step("Turn left",      "MOVE:turnleft", 0.7),
        Step("Turn right",     "MOVE:turnright",0.7),

        Step("Back groove",    "MOVE:back",    0.7),
        Step("Pose",           "ACTION:1",     1.2),

        # ===== PHRASE 2 =====
        Step("Slide left",     "MOVE:left",    0.5),
        Step("Slide right",    "MOVE:right",   0.5),
        Step("Slide left",     "MOVE:left",    0.5),
        Step("Slide right",    "MOVE:right",   0.5),

        Step("Forward hit",    "MOVE:forward", 0.6),
        Step("Forward hit",    "MOVE:forward", 0.6),

        Step("Spin",           "MOVE:turnleft",1.0),
        Step("Crouch",         "ACTION:7",     1.2),
    ]

    variation = [

        Step("Fast left",      "MOVE:left",    0.4),
        Step("Fast right",     "MOVE:right",   0.4),
        Step("Fast left",      "MOVE:left",    0.4),
        Step("Fast right",     "MOVE:right",   0.4),

        Step("Forward attack", "MOVE:forward", 0.6),
        Step("Back attack",    "MOVE:back",    0.6),

        Step("Turn combo",     "MOVE:turnright", 0.8),
        Step("Turn combo",     "MOVE:turnleft",  0.8),

        Step("Big pose",       "ACTION:8",     1.5),
    ]

    finale = [
        Step("Slow forward",   "MOVE:forward", 1.0),
        Step("Slow forward",   "MOVE:forward", 1.0),

        Step("Final spin",     "MOVE:turnleft", 1.2),
        Step("Final pose",     "ACTION:1",      2.0),
        Step("Final crouch",   "ACTION:7",      2.0),
    ]

    # ===== ASSEMBLAGE =====
    steps = []

    # Intro (lent)
    steps += base

    # Corps (répétition intelligente)
    for i in range(6):   # ajuste ici pour la durée (~5 min)
        steps += base
        steps += variation

    # Final
    steps += finale

    return steps

def load_timeline(path: str) -> list[TimelineCue]:
    """Load timeline cues from JSON.

    JSON format:
    [
      {"t": 0.00, "name": "Intro", "cmd": "ACTION:8", "hold": 1.8},
      {"t": 1.95, "name": "Left hit", "cmd": "MOVE:left", "hold": 0.35}
    ]
    - t: absolute time in seconds from song start
    - hold: optional hold duration before sending STOP
    """
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)

    if not isinstance(raw, list):
        raise ValueError("Timeline JSON must be a list of cue objects")

    cues: list[TimelineCue] = []
    for i, item in enumerate(raw):
        if not isinstance(item, dict):
            raise ValueError(f"Timeline item #{i} must be an object")
        t_s = float(item["t"])
        name = str(item.get("name", f"Cue {i + 1}"))
        cmd = str(item["cmd"])
        hold_s = float(item.get("hold", 0.0))
        if t_s < 0:
            raise ValueError(f"Timeline item #{i} has negative t={t_s}")
        if hold_s < 0:
            raise ValueError(f"Timeline item #{i} has negative hold={hold_s}")
        cues.append(TimelineCue(t_s=t_s, name=name, cmd=cmd, hold_s=hold_s))

    cues.sort(key=lambda c: c.t_s)
    return cues


def _aggressive_cmd_for_beat(idx: int, beat_pos: int | None, is_downbeat: bool) -> str:
    """Return a punchy move pattern based on beat index and bar position."""
    if is_downbeat:
        # Downbeats drive the strongest accents.
        strong_cycle = ["MOVE:forward", "MOVE:turnleft", "MOVE:turnright", "ACTION:8"]
        return strong_cycle[(idx // 4) % len(strong_cycle)]

    if beat_pos == 2:
        return "MOVE:left"
    if beat_pos == 3:
        return "MOVE:right"
    if beat_pos == 4:
        return "MOVE:back"

    # Fallback when beat positions are missing or irregular.
    alt = ["MOVE:left", "MOVE:right", "MOVE:turnleft", "MOVE:turnright"]
    return alt[idx % len(alt)]


def build_aggressive_timeline_from_beats(data: dict[str, Any]) -> list[TimelineCue]:
    """Build a choreography timeline from beat-analysis JSON data.

    Expected keys in the JSON (as produced by beat analyzers):
    - beats: list[float]
    - downbeats: optional list[float]
    - beat_positions: optional list[int] with values 1..4
    """
    beats = data.get("beats", [])
    downbeats = data.get("downbeats", [])
    beat_positions = data.get("beat_positions", [])

    if not isinstance(beats, list) or len(beats) == 0:
        raise ValueError("Beat JSON must contain a non-empty 'beats' list")

    beat_times = [float(x) for x in beats]
    downbeat_set = {round(float(x), 2) for x in downbeats} if isinstance(downbeats, list) else set()

    cues: list[TimelineCue] = []
    for i, t_s in enumerate(beat_times):
        beat_pos = None
        if isinstance(beat_positions, list) and i < len(beat_positions):
            try:
                beat_pos = int(beat_positions[i])
            except (TypeError, ValueError):
                beat_pos = None

        is_downbeat = round(t_s, 2) in downbeat_set or beat_pos == 1
        cmd = _aggressive_cmd_for_beat(i, beat_pos, is_downbeat)

        # Keep movement snappy: hold is slightly shorter than interval to next beat.
        if i + 1 < len(beat_times):
            dt = max(0.06, beat_times[i + 1] - t_s)
            hold_s = max(0.08, min(0.42, dt * 0.72))
        else:
            hold_s = 0.25

        name = f"Beat {i + 1}"
        cues.append(TimelineCue(t_s=t_s, name=name, cmd=cmd, hold_s=hold_s))

    # Add a final strong pose near the end of the track.
    end_t = beat_times[-1] + 0.12
    cues.append(TimelineCue(t_s=end_t, name="Final power pose", cmd="ACTION:7", hold_s=0.75))
    return cues


def load_timeline_or_beats(path: str) -> TimelineSelection:
    """Load either direct timeline JSON or beat-analysis JSON."""
    with open(path, "r", encoding="utf-8") as f:
        raw = json.load(f)

    if isinstance(raw, list):
        # Native cue timeline format.
        cues = []
        for i, item in enumerate(raw):
            if not isinstance(item, dict):
                raise ValueError(f"Timeline item #{i} must be an object")
            t_s = float(item["t"])
            name = str(item.get("name", f"Cue {i + 1}"))
            cmd = str(item["cmd"])
            hold_s = float(item.get("hold", 0.0))
            if t_s < 0:
                raise ValueError(f"Timeline item #{i} has negative t={t_s}")
            if hold_s < 0:
                raise ValueError(f"Timeline item #{i} has negative hold={hold_s}")
            cues.append(TimelineCue(t_s=t_s, name=name, cmd=cmd, hold_s=hold_s))
        cues.sort(key=lambda c: c.t_s)
        return TimelineSelection(
            cues=cues,
            source_type="timeline",
            timeline_file=path,
        )

    if isinstance(raw, dict) and "beats" in raw:
        cues = build_aggressive_timeline_from_beats(raw)
        audio_path = str(raw.get("path")) if raw.get("path") else None
        bpm = None
        if raw.get("bpm") is not None:
            bpm = float(raw.get("bpm"))
        return TimelineSelection(
            cues=cues,
            source_type="beats",
            timeline_file=path,
            audio_path=audio_path,
            bpm=bpm,
        )

    # Fallback to strict timeline parser for clear error messages.
    cues = load_timeline(path)
    return TimelineSelection(
        cues=cues,
        source_type="timeline",
        timeline_file=path,
    )


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

        # Give subscribers time to connect before the first message
        self.get_logger().info(f"Dance leader ready — publishing on '{DANCE_TOPIC}'")
        self.get_logger().info(
            f"Config: loops={loops}, beat={beat}, speed={speed}, step_width={step_width}"
        )
        if timeline_path:
            self.get_logger().info(
                f"Timeline mode enabled: file={timeline_path}, song_delay={song_delay}s"
            )

    def _pick_audio_player(self) -> list[str] | None:
        """Return the command prefix to run an audio player, or None if unavailable."""
        if self.audio_player != "auto":
            if shutil.which(self.audio_player) is None:
                self.get_logger().warning(
                    f"Requested audio player '{self.audio_player}' not found in PATH"
                )
                return None
            # Generic invocation: <player> <file>
            return [self.audio_player]

        # Auto-detect common CLI players in priority order.
        if shutil.which("ffplay"):
            return ["ffplay", "-nodisp", "-autoexit", "-loglevel", "error"]
        if shutil.which("mpg123"):
            return ["mpg123", "-q"]
        if shutil.which("cvlc"):
            return ["cvlc", "--play-and-exit", "--intf", "dummy"]
        if shutil.which("paplay"):
            return ["paplay"]
        return None

    def _start_audio(self, audio_path: str) -> None:
        """Start audio playback in background."""
        player_prefix = self._pick_audio_player()
        if player_prefix is None:
            self.get_logger().warning(
                "No supported audio player found (ffplay/mpg123/cvlc/paplay). "
                "Install one or pass --audio-player."
            )
            return

        cmd = [*player_prefix, audio_path]
        try:
            self._audio_proc = subprocess.Popen(cmd)
            self.get_logger().info(f"Audio playback started with: {' '.join(cmd[:-1])}")
            self.get_logger().info(f"Audio file playing: {audio_path}")

            # If player exits immediately, report likely container audio misconfiguration.
            time.sleep(0.25)
            rc = self._audio_proc.poll()
            if rc is not None:
                self.get_logger().warning(
                    f"Audio player exited immediately with code {rc}. "
                    "If running in Docker, expose /dev/snd to the container."
                )
                self._audio_proc = None
        except Exception as exc:
            self.get_logger().warning(f"Failed to start audio playback: {exc}")
            self._audio_proc = None

    def _resolve_selected_audio(self, selection: TimelineSelection) -> str | None:
        """Resolve audio path priority: explicit file > named track > timeline metadata."""
        if self.audio_file:
            candidate = Path(self.audio_file).expanduser()
            return str(candidate) if candidate.exists() else None

        if self.audio_name:
            base_dir = Path(self.audio_dir).expanduser()
            named = base_dir / self.audio_name
            if named.exists():
                return str(named)

            # Convenience: allow passing a name without extension.
            for ext in (".mp3", ".wav", ".ogg"):
                with_ext = base_dir / f"{self.audio_name}{ext}"
                if with_ext.exists():
                    return str(with_ext)
            return None

        if selection.audio_path:
            candidate = Path(selection.audio_path)
            if candidate.exists():
                return str(candidate)

        return None

    def _stop_audio_if_running(self) -> None:
        if self._audio_proc is None:
            return
        if self._audio_proc.poll() is not None:
            self._audio_proc = None
            return

        self.get_logger().info("Stopping audio playback process...")
        self._audio_proc.terminate()
        try:
            self._audio_proc.wait(timeout=1.5)
        except Exception:
            self._audio_proc.kill()
        self._audio_proc = None

    def _pub_cmd(self, cmd: str) -> None:
        msg = String()
        msg.data = cmd
        self._pub.publish(msg)
        self.get_logger().info(f">> {cmd}")

    def _sleep(self, seconds: float) -> None:
        """rclpy-safe sleep (spins during sleep to process callbacks)."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            rclpy.spin_once(self, timeout_sec=0.05)

    def _wait_until_monotonic(self, target_t: float) -> None:
        """Wait until target monotonic timestamp while keeping ROS callbacks alive."""
        while True:
            now = time.monotonic()
            if now >= target_t:
                return
            # Sleep in short chunks to reduce jitter while still spinning callbacks.
            remaining = target_t - now
            rclpy.spin_once(self, timeout_sec=min(0.02, max(0.001, remaining)))

    def _run_timeline(self, cues: list[TimelineCue]) -> None:
        if not cues:
            self.get_logger().warning("Timeline is empty. Nothing to run.")
            return

        self.get_logger().info(f"Starting timeline with {len(cues)} cues")
        self.get_logger().info(
            f"Start your music now. First cue will fire in {self.song_delay:.2f} s"
        )

        start_t = time.monotonic() + self.song_delay
        for i, cue in enumerate(cues, start=1):
            fire_t = start_t + cue.t_s
            self._wait_until_monotonic(fire_t)
            self.get_logger().info(
                f"Cue {i:03d}/{len(cues)} @ {cue.t_s:.2f}s: {cue.name}"
            )
            self._pub_cmd(cue.cmd)
            if cue.hold_s > 0:
                self._sleep(cue.hold_s)
                self._pub_cmd("STOP")

    def run(self) -> None:
        # Allow followers to fully subscribe before we start
        self.get_logger().info("Waiting 2 s for followers to connect...")
        self._sleep(2.0)

        # Push speed config to followers
        self._pub_cmd(f"SPEED:{self.speed}")
        self._sleep(0.3)

        if self.timeline_path:
            selection = load_timeline_or_beats(self.timeline_path)
            self.get_logger().info("================ SONG SELECTION ================")
            self.get_logger().info(
                f"Timeline file: {selection.timeline_file}"
            )
            self.get_logger().info(
                f"Timeline type: {selection.source_type}"
            )
            if selection.audio_path:
                self.get_logger().info(
                    f"Audio reference: {selection.audio_path}"
                )
                self.get_logger().info(
                    f"Audio file name: {os.path.basename(selection.audio_path)}"
                )
            if selection.bpm is not None:
                self.get_logger().info(f"Detected BPM: {selection.bpm:.2f}")
            self.get_logger().info("================================================")

            selected_audio = self._resolve_selected_audio(selection)

            if selected_audio:
                self.get_logger().info(f"Selected audio for autoplay: {selected_audio}")
            else:
                self.get_logger().warning(
                    "No valid audio selected for autoplay. "
                    "Use --audio-file /path/to/song.mp3 or --audio-name TRACK"
                )

            if self.play_audio and selected_audio:
                # Start song now; cue timestamps are relative to this start.
                self._start_audio(selected_audio)

            self._run_timeline(selection.cues)
            self._pub_cmd("RESET")
            self._sleep(0.5)
            self._pub_cmd("DONE")
            self.get_logger().info("Timeline choreography complete.")
            self._stop_audio_if_running()
            return

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
        self.get_logger().info("Choreography complete.")


def parse_args() -> argparse.Namespace:
    import rclpy.utilities
    parser = argparse.ArgumentParser(description="MUTO-RS dance leader (ROS2)")
    parser.add_argument("--loops",      type=int,   default=1,   help="Number of full dance loops")
    parser.add_argument("--beat",       type=float, default=1.0, help="Tempo multiplier (<1 faster)")
    parser.add_argument("--speed",      type=int,   default=2,   help="Speed level sent to followers (1-5)")
    parser.add_argument("--step-width", type=int,   default=16,  help="Step width for locomotion (10-25)")
    parser.add_argument(
        "--timeline",
        type=str,
        default="",
        help="Path to JSON cue timeline for precise song sync",
    )
    parser.add_argument(
        "--song-delay",
        type=float,
        default=5.0,
        help="Delay before first cue in timeline mode (seconds)",
    )
    parser.add_argument(
        "--audio-file",
        type=str,
        default="",
        help="Path to song file to play when timeline starts (mp3/wav)",
    )
    parser.add_argument(
        "--audio-name",
        type=str,
        default="",
        help="Song name in audio directory (with or without extension)",
    )
    parser.add_argument(
        "--audio-dir",
        type=str,
        default="assets/audio",
        help="Directory where song files are stored",
    )
    parser.add_argument(
        "--audio-player",
        type=str,
        default="auto",
        help="Audio player binary (auto|ffplay|mpg123|cvlc|paplay)",
    )
    parser.add_argument(
        "--play-audio",
        type=str,
        default="true",
        choices=["true", "false"],
        help="Enable automatic song playback in timeline mode",
    )
    # Strip ROS-injected args (--ros-args, -r __node:=...) before argparse sees them
    return parser.parse_args(rclpy.utilities.remove_ros_args(sys.argv)[1:])


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
        node.get_logger().info("Interrupted, sending STOP + RESET to followers...")
        node._pub_cmd("STOP")
        time.sleep(0.3)
        node._pub_cmd("RESET")
        node._stop_audio_if_running()
    finally:
        node._stop_audio_if_running()
        node.destroy_node()
        rclpy.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
