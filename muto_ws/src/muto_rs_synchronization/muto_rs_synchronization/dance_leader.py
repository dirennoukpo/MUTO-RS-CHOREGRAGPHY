#!/usr/bin/env python3
"""Simple ROS2 dance leader: only publishes the START signal."""

from __future__ import annotations

import argparse
import time

import rclpy
from rclpy.node import Node
from std_msgs.msg import String


DANCE_TOPIC = "/dance_cmd"


class DanceLeader(Node):
    def __init__(self) -> None:
        super().__init__("dance_leader")
        self._pub = self.create_publisher(String, DANCE_TOPIC, 10)

    def send_start(self, repeat: int, interval_s: float) -> None:
        msg = String()
        msg.data = "START"
        repeat = max(1, repeat)
        for i in range(repeat):
            self._pub.publish(msg)
            self.get_logger().info(f"START published ({i + 1}/{repeat})")
            if i < repeat - 1:
                time.sleep(max(0.0, interval_s))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="MUTO-RS simple dance leader")
    parser.add_argument(
        "--countdown",
        type=float,
        default=2.0,
        help="Seconds to wait before publishing START",
    )
    parser.add_argument(
        "--repeat-start",
        type=int,
        default=3,
        help="Number of START messages published for reliability",
    )
    parser.add_argument(
        "--repeat-interval",
        type=float,
        default=0.2,
        help="Delay between repeated START messages (seconds)",
    )
    return parser.parse_args(rclpy.utilities.remove_ros_args()[1:])


def main() -> int:
    args = parse_args()

    rclpy.init()
    node = DanceLeader()

    try:
        countdown = max(0.0, args.countdown)
        if countdown > 0:
            node.get_logger().info(f"Countdown before START: {countdown:.1f}s")
            time.sleep(countdown)

        node.send_start(args.repeat_start, args.repeat_interval)
        node.get_logger().info("Leader done.")
    finally:
        node.destroy_node()
        rclpy.shutdown()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
