#!/usr/bin/env python3
"""Minimal launch file for START-based leader/follower choreography."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def _build_nodes(context):
    mode = LaunchConfiguration("mode").perform(context)
    nodes = []

    if mode == "leader":
        nodes.append(
            Node(
                package="muto_rs_synchronization",
                namespace="dance",
                executable="dance_leader.py",
                name="leader",
                arguments=[
                    "--countdown", LaunchConfiguration("countdown").perform(context),
                    "--repeat-start", LaunchConfiguration("repeat_start").perform(context),
                    "--repeat-interval", LaunchConfiguration("repeat_interval").perform(context),
                ],
                output="screen",
            )
        )
    elif mode == "follower":
        args = [
            "--step-width", LaunchConfiguration("step_width").perform(context),
            "--loops", LaunchConfiguration("loops").perform(context),
            "--move-time", LaunchConfiguration("move_time").perform(context),
            "--pause", LaunchConfiguration("pause").perform(context),
            "--speed", LaunchConfiguration("speed").perform(context),
            "--serial-port", LaunchConfiguration("serial_port").perform(context),
        ]
        dry_run = LaunchConfiguration("dry_run").perform(context)
        if dry_run.lower() == "true":
            args.append("--dry-run")

        nodes.append(
            Node(
                package="muto_rs_synchronization",
                namespace="dance",
                executable="dance_follower.py",
                name="follower",
                arguments=args,
                output="screen",
            )
        )

    return nodes


def generate_launch_description() -> LaunchDescription:
    return LaunchDescription([
        DeclareLaunchArgument("mode", default_value="leader", description="leader or follower"),
        DeclareLaunchArgument("countdown", default_value="2.0", description="Leader countdown in seconds"),
        DeclareLaunchArgument("repeat_start", default_value="3", description="Leader START publish count"),
        DeclareLaunchArgument("repeat_interval", default_value="0.2", description="Delay between START publishes"),
        DeclareLaunchArgument("step_width", default_value="16", description="Follower step width"),
        DeclareLaunchArgument("loops", default_value="2", description="Follower choreography loops"),
        DeclareLaunchArgument("move_time", default_value="0.8", description="Follower move hold time (s)"),
        DeclareLaunchArgument("pause", default_value="0.2", description="Follower pause between moves (s)"),
        DeclareLaunchArgument("speed", default_value="2", description="Follower speed (1-5)"),
        DeclareLaunchArgument("serial_port", default_value="/dev/ttyUSB0", description="Follower serial port"),
        DeclareLaunchArgument("dry_run", default_value="false", description="Follower dry-run mode"),
        OpaqueFunction(function=_build_nodes),
    ])
