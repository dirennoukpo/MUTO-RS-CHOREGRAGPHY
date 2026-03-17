#!/usr/bin/env python3
"""ROS2 dance follower node for MUTO-RS multi-robot synchronization.

This node runs ON EACH ROBOT (has MutoLib + ROS2 in the factory Docker image).
It subscribes to /dance_cmd and executes each command via MutoLib.

Protocol (same as dance_leader.py):
  SPEED:<int>         → set speed level
  ACTION:<int>        → execute built-in action (1-8)
  MOVE:<direction>    → forward | back | left | right | turnleft | turnright
  STOP                → stop movement
  RESET               → reset to neutral stance
  DONE                → shutdown this node cleanly

Usage on robot (inside Muto Docker container):
  # 1. Make sure ROS_DOMAIN_ID matches the leader machine:
  export ROS_DOMAIN_ID=42
  # 2. Run:
  python3 dance_follower.py
  python3 dance_follower.py --step-width 16 --dry-run
"""

from __future__ import annotations

import argparse
import sys

import rclpy
from rclpy.node import Node
from std_msgs.msg import String


DANCE_TOPIC = "/dance_cmd"


class RobotController:
    """Wrap MutoLib calls with optional dry-run."""

    def __init__(self, dry_run: bool, step_width: int) -> None:
        self.dry_run = dry_run
        self.step_width = max(10, min(25, step_width))
        self._bot = None
        self._speed_level = 2

        if not self.dry_run:
            try:
                from MutoLib import Muto  # type: ignore
                self._bot = Muto()
                print("[follower] MutoLib connected OK")
            except Exception as exc:
                print(f"[WARN] MutoLib unavailable, switching to dry-run. {exc}")
                self.dry_run = True

    def _log(self, msg: str) -> None:
        print(f"[follower] {msg}")

    def speed(self, level: int) -> None:
        self._speed_level = max(1, min(5, level))
        self._log(f"speed({self._speed_level})")
        if not self.dry_run:
            self._bot.speed(self._speed_level)

    def stop(self) -> None:
        self._log("stop()")
        if not self.dry_run:
            self._bot.stop()

    def reset(self) -> None:
        self._log("reset()")
        if not self.dry_run:
            self._bot.reset()

    def action(self, action_id: int) -> None:
        self._log(f"action({action_id})")
        if not self.dry_run:
            self._bot.action(action_id)

    def move(self, direction: str) -> None:
        self._log(f"{direction}({self.step_width})")
        if self.dry_run:
            return
        fn_map = {
            "forward":   self._bot.forward,
            "back":      self._bot.back,
            "left":      self._bot.left,
            "right":     self._bot.right,
            "turnleft":  self._bot.turnleft,
            "turnright": self._bot.turnright,
        }
        fn = fn_map.get(direction)
        if fn:
            fn(self.step_width)
        else:
            print(f"[WARN] Unknown direction: {direction}")


class DanceFollower(Node):
    def __init__(self, ctrl: RobotController) -> None:
        super().__init__("dance_follower")
        self._ctrl = ctrl
        self._done = False

        self._sub = self.create_subscription(
            String,
            DANCE_TOPIC,
            self._on_cmd,
            qos_profile=10,
        )
        self.get_logger().info(
            f"Dance follower ready — listening on '{DANCE_TOPIC}' "
            f"(dry_run={ctrl.dry_run}, step_width={ctrl.step_width})"
        )

    def _on_cmd(self, msg: String) -> None:
        cmd = msg.data.strip()
        self.get_logger().debug(f"Received: {cmd}")

        if cmd == "STOP":
            self._ctrl.stop()

        elif cmd == "RESET":
            self._ctrl.reset()

        elif cmd == "DONE":
            self.get_logger().info("DONE received — choreography finished.")
            self._done = True

        elif cmd.startswith("SPEED:"):
            try:
                level = int(cmd.split(":", 1)[1])
                self._ctrl.speed(level)
            except ValueError:
                self.get_logger().warn(f"Bad SPEED command: {cmd}")

        elif cmd.startswith("ACTION:"):
            try:
                action_id = int(cmd.split(":", 1)[1])
                self._ctrl.action(action_id)
            except ValueError:
                self.get_logger().warn(f"Bad ACTION command: {cmd}")

        elif cmd.startswith("MOVE:"):
            direction = cmd.split(":", 1)[1].lower()
            self._ctrl.move(direction)

        else:
            self.get_logger().warn(f"Unknown command ignored: {cmd}")

    @property
    def done(self) -> bool:
        return self._done


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="MUTO-RS dance follower (ROS2)")
    parser.add_argument(
        "--step-width", type=int, default=16,
        help="Step width for locomotion commands (10-25)"
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Print commands without sending to robot hardware"
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    ctrl = RobotController(dry_run=args.dry_run, step_width=args.step_width)

    rclpy.init()
    node = DanceFollower(ctrl)

    try:
        while rclpy.ok() and not node.done:
            rclpy.spin_once(node, timeout_sec=0.1)
    except KeyboardInterrupt:
        node.get_logger().info("Interrupted — stopping robot.")
        ctrl.stop()
        ctrl.reset()
    finally:
        node.destroy_node()
        rclpy.shutdown()

    return 0


if __name__ == "__main__":
    sys.exit(main())
