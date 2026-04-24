#!/usr/bin/env python3
"""Simple ROS2 dance follower: runs a preprogrammed routine on START."""

from __future__ import annotations

import argparse
import os
import signal
import threading
import time

import rclpy
from rclpy.node import Node
from std_msgs.msg import String


DANCE_TOPIC = "/dance_cmd"


class RobotController:
    def __init__(self, dry_run: bool, step_width: int, serial_port: str) -> None:
        self.dry_run = dry_run
        self.step_width = max(10, min(25, step_width))
        self.serial_port = serial_port
        self._bot = None

        if self.dry_run:
            print("[follower] dry-run enabled")
            return

        try:
            from MutoLib import Muto  # type: ignore
            self._bot = Muto(port=self.serial_port)
            fw = self._bot.read_version()
            batt = self._bot.read_battery(True)
            if fw is None and batt == 0.0:
                print(
                    "[WARN] serial opened but robot did not reply "
                    f"(port={self.serial_port}, fw=None, battery=0.0). Switching to dry-run."
                )
                self.dry_run = True
            else:
                print(f"[follower] MutoLib connected on {self.serial_port} (fw={fw}, battery={batt})")
        except Exception as exc:
            print(f"[WARN] MutoLib init failed on {self.serial_port}: {exc}. Switching to dry-run.")
            self.dry_run = True

    def _log(self, msg: str) -> None:
        print(f"[follower] {msg}")

    def speed(self, level: int) -> None:
        level = max(1, min(5, level))
        self._log(f"speed({level})")
        if not self.dry_run and self._bot is not None:
            self._bot.speed(level)

    def stop(self) -> None:
        self._log("stop()")
        if not self.dry_run and self._bot is not None:
            self._bot.stop()

    def reset(self) -> None:
        self._log("reset()")
        if not self.dry_run and self._bot is not None:
            self._bot.reset()

    def move(self, direction: str) -> None:
        self._log(f"{direction}({self.step_width})")
        if self.dry_run or self._bot is None:
            return
        fn_map = {
            "forward": self._bot.forward,
            "back": self._bot.back,
            "left": self._bot.left,
            "right": self._bot.right,
        }
        fn = fn_map.get(direction)
        if fn is not None:
            fn(self.step_width)

    def safe_shutdown(self) -> None:
        try:
            self.stop()
        except Exception as exc:
            print(f"[WARN] stop() failed during shutdown: {exc}")
        try:
            self.reset()
        except Exception as exc:
            print(f"[WARN] reset() failed during shutdown: {exc}")


class DanceFollower(Node):
    def __init__(self, ctrl: RobotController, loops: int, move_time_s: float, pause_s: float, speed: int) -> None:
        super().__init__("dance_follower")
        self._ctrl = ctrl
        self._loops = max(1, loops)
        self._move_time_s = max(0.05, move_time_s)
        self._pause_s = max(0.0, pause_s)
        self._speed = max(1, min(5, speed))
        self._running = False
        self._stop_event = threading.Event()
        self._worker: threading.Thread | None = None

        self.create_subscription(String, DANCE_TOPIC, self._on_cmd, 10)
        self.get_logger().info(
            f"Dance follower ready — listening on '{DANCE_TOPIC}' "
            f"(dry_run={ctrl.dry_run}, step_width={ctrl.step_width}, loops={self._loops})"
        )

    def _on_cmd(self, msg: String) -> None:
        cmd = msg.data.strip().upper()

        if cmd == "START":
            if self._running:
                self.get_logger().info("START ignored: choreography already running")
                return
            self.get_logger().info("START received — running preprogrammed choreography")
            self._stop_event.clear()
            self._worker = threading.Thread(target=self._run_choreography, daemon=True)
            self._worker.start()
            return

        if cmd == "STOP":
            self.get_logger().info("STOP received")
            self._stop_event.set()
            self._ctrl.safe_shutdown()
            return

        if cmd == "RESET":
            self.get_logger().info("RESET received")
            self._ctrl.reset()

    def _run_choreography(self) -> None:
        self._running = True
        sequence = ["forward", "back", "left", "right"]
        try:
            self._ctrl.speed(self._speed)
            for _ in range(self._loops):
                for move in sequence:
                    if self._stop_event.is_set():
                        return
                    self._ctrl.move(move)
                    time.sleep(self._move_time_s)
                    self._ctrl.stop()
                    if self._pause_s > 0:
                        time.sleep(self._pause_s)
            self.get_logger().info("Preprogrammed choreography completed")
        finally:
            self._ctrl.safe_shutdown()
            self._running = False

    def shutdown(self) -> None:
        self._stop_event.set()
        self._ctrl.safe_shutdown()
        if self._worker is not None and self._worker.is_alive():
            self._worker.join(timeout=1.0)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="MUTO-RS simple dance follower")
    parser.add_argument("--step-width", type=int, default=16, help="Step width (10-25)")
    parser.add_argument("--loops", type=int, default=2, help="Number of choreography loops")
    parser.add_argument("--move-time", type=float, default=0.8, help="Time a move is held (s)")
    parser.add_argument("--pause", type=float, default=0.2, help="Pause between moves (s)")
    parser.add_argument("--speed", type=int, default=2, help="Robot speed (1-5)")
    parser.add_argument("--dry-run", action="store_true", help="Do not send hardware commands")
    parser.add_argument(
        "--serial-port",
        default=os.getenv("MUTO_SERIAL_PORT", "/dev/ttyUSB0"),
        help="Serial port for MutoLib",
    )
    return parser.parse_args(rclpy.utilities.remove_ros_args()[1:])


def main() -> int:
    args = parse_args()
    ctrl = RobotController(args.dry_run, args.step_width, args.serial_port)

    rclpy.init()
    node = DanceFollower(ctrl, args.loops, args.move_time, args.pause, args.speed)

    stop_requested = {"value": False}

    def _handle_signal(signum, _frame) -> None:
        stop_requested["value"] = True
        node.get_logger().info(f"Signal {signum} received — stopping robot")
        node.shutdown()

    signal.signal(signal.SIGTERM, _handle_signal)
    signal.signal(signal.SIGINT, _handle_signal)

    try:
        while rclpy.ok() and not stop_requested["value"]:
            rclpy.spin_once(node, timeout_sec=0.1)
    finally:
        node.shutdown()
        node.destroy_node()
        rclpy.shutdown()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
