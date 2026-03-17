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
import sys
import time

import rclpy
from rclpy.node import Node
from std_msgs.msg import String


DANCE_TOPIC = "/dance_cmd"


class Step:
    def __init__(self, name: str, cmd: str, pause_s: float) -> None:
        self.name = name
        self.cmd = cmd          # command string to publish
        self.pause_s = pause_s  # seconds to wait after command before STOP


def choreography(speed: int, step_width: int) -> list[Step]:
    """Return the ordered list of dance steps (mirrors dance_demo.py logic).

    Built-in action IDs:
      1 Stretch  2 Greeting  3 Retreat  4 Warm_up
      5 Turn_around  6 Say_no  7 Crouching  8 Stride
    """
    _ = step_width  # width is embedded in follower's RobotController config
    return [
        Step("Warm up",      "ACTION:4",         2.4),
        Step("Say hello",    "ACTION:2",         1.6),
        Step("Slide left",   "MOVE:left",        1.0),
        Step("Slide right",  "MOVE:right",       1.0),
        Step("Turn left",    "MOVE:turnleft",    0.9),
        Step("Turn right",   "MOVE:turnright",   0.9),
        Step("Forward hit",  "MOVE:forward",     0.8),
        Step("Back hit",     "MOVE:back",        0.8),
        Step("Pose stretch", "ACTION:1",         1.8),
        Step("Final crouch", "ACTION:7",         1.8),
    ]


class DanceLeader(Node):
    def __init__(self, loops: int, beat: float, speed: int, step_width: int) -> None:
        super().__init__("dance_leader")
        self.loops = loops
        self.beat = beat
        self.speed = speed
        self.step_width = step_width

        self._pub = self.create_publisher(String, DANCE_TOPIC, qos_profile=10)

        # Give subscribers time to connect before the first message
        self.get_logger().info(f"Dance leader ready — publishing on '{DANCE_TOPIC}'")
        self.get_logger().info(
            f"Config: loops={loops}, beat={beat}, speed={speed}, step_width={step_width}"
        )

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

    def run(self) -> None:
        # Allow followers to fully subscribe before we start
        self.get_logger().info("Waiting 2 s for followers to connect...")
        self._sleep(2.0)

        # Push speed config to followers
        self._pub_cmd(f"SPEED:{self.speed}")
        self._sleep(0.3)

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
    )
    try:
        node.run()
    except KeyboardInterrupt:
        node.get_logger().info("Interrupted, sending STOP + RESET to followers...")
        node._pub_cmd("STOP")
        time.sleep(0.3)
        node._pub_cmd("RESET")
    finally:
        node.destroy_node()
        rclpy.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
