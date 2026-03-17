#!/usr/bin/env python3
"""Simple dance demo for MUTO-RS.

This script plays a short choreography by chaining built-in actions and
basic locomotion commands from MutoLib.

Examples:
  python3 dance_demo.py --dry-run
  python3 dance_demo.py --loops 2
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from typing import Callable, List, Optional


@dataclass
class Step:
    name: str
    fn: Callable[[], None]
    pause_s: float


class RobotController:
    """Wrap MutoLib calls and provide a dry-run mode."""

    def __init__(self, dry_run: bool, speed: int, step_width: int) -> None:
        self.dry_run = dry_run
        self.speed_level = max(1, min(5, speed))
        self.step_width = max(10, min(25, step_width))
        self._bot = None

        if not self.dry_run:
            try:
                from MutoLib import Muto  # type: ignore

                self._bot = Muto()
            except Exception as exc:
                print(
                    "[WARN] MutoLib unavailable or robot not reachable. "
                    f"Switching to dry-run mode. Details: {exc}"
                )
                self.dry_run = True

    def _log(self, msg: str) -> None:
        print(f"[dance] {msg}")

    def _call(self, label: str, fn: Optional[Callable[[], None]] = None) -> None:
        self._log(label)
        if not self.dry_run and fn is not None:
            fn()

    def speed(self) -> None:
        self._call(f"speed({self.speed_level})", lambda: self._bot.speed(self.speed_level))

    def stop(self) -> None:
        self._call("stop()", lambda: self._bot.stop())

    def reset(self) -> None:
        self._call("reset()", lambda: self._bot.reset())

    def action(self, action_id: int) -> None:
        self._call(f"action({action_id})", lambda: self._bot.action(action_id))

    def forward(self) -> None:
        self._call(
            f"forward({self.step_width})",
            lambda: self._bot.forward(self.step_width),
        )

    def back(self) -> None:
        self._call(
            f"back({self.step_width})",
            lambda: self._bot.back(self.step_width),
        )

    def left(self) -> None:
        self._call(
            f"left({self.step_width})",
            lambda: self._bot.left(self.step_width),
        )

    def right(self) -> None:
        self._call(
            f"right({self.step_width})",
            lambda: self._bot.right(self.step_width),
        )

    def turn_left(self) -> None:
        self._call(
            f"turnleft({self.step_width})",
            lambda: self._bot.turnleft(self.step_width),
        )

    def turn_right(self) -> None:
        self._call(
            f"turnright({self.step_width})",
            lambda: self._bot.turnright(self.step_width),
        )


def choreography(ctrl: RobotController) -> List[Step]:
    # Built-in action IDs:
    # 1 Stretch, 2 Greeting, 3 Retreat, 4 Warm_up, 5 Turn_around,
    # 6 Say_no, 7 Crouching, 8 Stride.
    return [
        Step("Warm up", lambda: ctrl.action(4), 2.4),
        Step("Say hello", lambda: ctrl.action(2), 1.6),
        Step("Slide left", ctrl.left, 1.0),
        Step("Slide right", ctrl.right, 1.0),
        Step("Turn left", ctrl.turn_left, 0.9),
        Step("Turn right", ctrl.turn_right, 0.9),
        Step("Forward hit", ctrl.forward, 0.8),
        Step("Back hit", ctrl.back, 0.8),
        Step("Pose stretch", lambda: ctrl.action(1), 1.8),
        Step("Final crouch", lambda: ctrl.action(7), 1.8),
    ]


def run_show(ctrl: RobotController, loops: int, beat_s: float) -> None:
    ctrl.speed()
    steps = choreography(ctrl)
    print(f"[dance] Starting choreography with {len(steps)} steps x {loops} loop(s)")

    for loop_idx in range(loops):
        print(f"[dance] Loop {loop_idx + 1}/{loops}")
        for idx, step in enumerate(steps, start=1):
            print(f"[dance] Step {idx:02d}: {step.name}")
            step.fn()
            time.sleep(max(0.05, step.pause_s * beat_s))
            ctrl.stop()
            time.sleep(0.08)

    ctrl.reset()
    print("[dance] Done")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="MUTO-RS dance demo")
    parser.add_argument("--loops", type=int, default=1, help="Number of full dance loops")
    parser.add_argument(
        "--speed",
        type=int,
        default=2,
        help="Robot speed level (1..5)",
    )
    parser.add_argument(
        "--step-width",
        type=int,
        default=16,
        help="Step width for locomotion (10..25)",
    )
    parser.add_argument(
        "--beat",
        type=float,
        default=1.0,
        help="Tempo multiplier. <1.0 faster, >1.0 slower",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print commands without sending them to robot",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    loops = max(1, args.loops)
    ctrl = RobotController(dry_run=args.dry_run, speed=args.speed, step_width=args.step_width)

    try:
        run_show(ctrl=ctrl, loops=loops, beat_s=max(0.2, args.beat))
    except KeyboardInterrupt:
        print("\n[dance] Interrupted, stopping robot...")
        try:
            ctrl.stop()
            ctrl.reset()
        except Exception:
            pass
    except Exception as exc:
        print(f"[ERROR] Dance failed: {exc}")
        try:
            ctrl.stop()
            ctrl.reset()
        except Exception:
            pass
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
