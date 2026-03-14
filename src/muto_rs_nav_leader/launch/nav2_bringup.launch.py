import os
from pathlib import Path

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, OpaqueFunction
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def _find_repo_root() -> Path:
    env_repo = os.getenv("MUTO_RS_REPO")
    if env_repo:
        candidate = Path(env_repo).expanduser().resolve()
        if (candidate / "src" / "muto_rs_nav_leader").exists():
            return candidate

    current_file = Path(__file__).resolve()
    for parent in current_file.parents:
        if (parent / "src" / "muto_rs_nav_leader").exists():
            return parent

    workspace_repo = Path("/workspace")
    if (workspace_repo / "src" / "muto_rs_nav_leader").exists():
        return workspace_repo

    raise RuntimeError(
        "Unable to locate repository root. Set MUTO_RS_REPO to your repo path."
    )


def _create_bringup_action(context):
    map_path = LaunchConfiguration("map").perform(context)
    params_path = Path(LaunchConfiguration("params_file").perform(context)).expanduser().resolve()

    if not params_path.exists():
        raise RuntimeError(f"params_file not found: {params_path}")

    return [
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                "/opt/ros/humble/share/nav2_bringup/launch/bringup_launch.py"
            ),
            launch_arguments={
                "map": map_path,
                "params_file": params_path.as_posix(),
                "use_sim_time": "False",
                "autostart": "True",
            }.items(),
        )
    ]


def generate_launch_description():
    repo_root = _find_repo_root()
    default_map = repo_root / "src" / "muto_rs_nav_leader" / "maps" / "electronics_room.yaml"
    default_params = repo_root / "src" / "muto_rs_nav_leader" / "params" / "nav2_params.yaml"

    map_arg = DeclareLaunchArgument(
        "map",
        default_value=default_map.as_posix(),
        description="Path to map yaml",
    )
    params_arg = DeclareLaunchArgument(
        "params_file",
        default_value=default_params.as_posix(),
        description="Path to Nav2 params yaml",
    )
    fake_odom_tf_arg = DeclareLaunchArgument(
        "use_fake_odom_tf",
        default_value="True",
        description=(
            "Publish a fallback static transform odom->base_footprint when no odom TF is available"
        ),
    )
    fake_map_tf_arg = DeclareLaunchArgument(
        "use_fake_map_tf",
        default_value="True",
        description=(
            "Publish a fallback static transform map->odom when no map TF is available"
        ),
    )

    fake_odom_tf = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        name="fake_odom_tf",
        arguments=["0", "0", "0", "0", "0", "0", "odom", "base_footprint"],
        condition=IfCondition(LaunchConfiguration("use_fake_odom_tf")),
    )
    fake_map_tf = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        name="fake_map_tf",
        arguments=["0", "0", "0", "0", "0", "0", "map", "odom"],
        condition=IfCondition(LaunchConfiguration("use_fake_map_tf")),
    )

    bringup = OpaqueFunction(function=_create_bringup_action)

    return LaunchDescription([
        map_arg,
        params_arg,
        fake_odom_tf_arg,
        fake_map_tf_arg,
        fake_odom_tf,
        fake_map_tf,
        bringup,
    ])
