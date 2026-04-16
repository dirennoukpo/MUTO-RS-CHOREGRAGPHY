#!/usr/bin/env python3
"""
ROS 2 launch file for MUTO-RS dance choreography.

Supports two modes:
  1. Leader (PC/central orchestrator):
    ros2 launch muto_rs_synchronization dance_choreography.launch.py mode:=leader
  
  2. Follower (each robot):
      ros2 launch muto_rs_synchronization dance_choreography.launch.py mode:=follower

Examples:
  # Leader with 2 loops and 0.9x tempo
    ros2 launch muto_rs_synchronization dance_choreography.launch.py mode:=leader loops:=2 beat:=0.9

  # Follower with larger steps
    ros2 launch muto_rs_synchronization dance_choreography.launch.py mode:=follower step_width:=20

  # Follower in dry-run mode (no hardware control)
    ros2 launch muto_rs_synchronization dance_choreography.launch.py mode:=follower dry_run:=true
"""

import os
from pathlib import Path

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def _build_nodes(context):
    """Build leader or follower node depending on mode."""
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
                    "--loops", LaunchConfiguration("loops").perform(context),
                    "--beat", LaunchConfiguration("beat").perform(context),
                    "--speed", LaunchConfiguration("speed").perform(context),
                    "--step-width", LaunchConfiguration("step_width").perform(context),
                    "--timeline", LaunchConfiguration("timeline").perform(context),
                    "--song-delay", LaunchConfiguration("song_delay").perform(context),
                    "--audio-file", LaunchConfiguration("audio_file").perform(context),
                    "--audio-name", LaunchConfiguration("audio_name").perform(context),
                    "--audio-dir", LaunchConfiguration("audio_dir").perform(context),
                    "--audio-player", LaunchConfiguration("audio_player").perform(context),
                    "--play-audio", LaunchConfiguration("play_audio").perform(context),
                ],
                output="screen",
            )
        )
    elif mode == "follower":
        dry_run = LaunchConfiguration("dry_run").perform(context)
        step_width = LaunchConfiguration("step_width").perform(context)

        args = ["--step-width", step_width]
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
    """Generate launch description for dance choreography (leader or follower mode)."""

    return LaunchDescription([
        # ===== Mode selection =====
        DeclareLaunchArgument(
            "mode",
            default_value="leader",
            description="Dance mode: 'leader' (orchestrator) or 'follower' (robot)",
        ),

        # ===== Leader-mode parameters =====
        DeclareLaunchArgument(
            "loops",
            default_value="1",
            description="[Leader] Number of full dance loops (int, default=1)",
        ),
        DeclareLaunchArgument(
            "beat",
            default_value="1.0",
            description="[Leader] Tempo multiplier: <1.0 = faster, >1.0 = slower (float, default=1.0)",
        ),
        DeclareLaunchArgument(
            "speed",
            default_value="2",
            description="[Leader] Speed level sent to followers (1-5, default=2)",
        ),
        DeclareLaunchArgument(
            "timeline",
            default_value="",
            description="[Leader] Path to timeline/beat JSON file",
        ),
        DeclareLaunchArgument(
            "song_delay",
            default_value="5.0",
            description="[Leader] Delay before first cue in timeline mode (seconds)",
        ),
        DeclareLaunchArgument(
            "audio_file",
            default_value="",
            description="[Leader] Song file path to autoplay at choreography start",
        ),
        DeclareLaunchArgument(
            "audio_name",
            default_value="",
            description="[Leader] Song name from audio directory (with or without extension)",
        ),
        DeclareLaunchArgument(
            "audio_dir",
            default_value="assets/audio",
            description="[Leader] Directory containing song files",
        ),
        DeclareLaunchArgument(
            "audio_player",
            default_value="auto",
            description="[Leader] Audio player binary (auto|ffplay|mpg123|cvlc|paplay)",
        ),
        DeclareLaunchArgument(
            "play_audio",
            default_value="true",
            description="[Leader] Enable automatic song playback (true|false)",
        ),

        # ===== Shared parameters =====
        DeclareLaunchArgument(
            "step_width",
            default_value="16",
            description="[Both] Step width for movement commands (10-25, default=16)",
        ),

        # ===== Follower-mode parameters =====
        DeclareLaunchArgument(
            "dry_run",
            default_value="false",
            description="[Follower] Dry-run mode: log commands without hardware control",
        ),

        # ===== Build and launch the appropriate node =====
        OpaqueFunction(function=_build_nodes),
    ])

