import os
import re
import tempfile
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
        if (candidate / "src" / "muto_rs_nav").exists():
            return candidate

    current_file = Path(__file__).resolve()
    for parent in current_file.parents:
        if (parent / "src" / "muto_rs_nav").exists():
            return parent

    workspace_repo = Path("/workspace")
    if (workspace_repo / "src" / "muto_rs_nav").exists():
        return workspace_repo

    raise RuntimeError(
        "Unable to locate repository root. Set MUTO_RS_REPO to your repo path."
    )


def _rewrite_bt_xml_path(params_path: Path, bt_xml_path: Path) -> str:
    content = params_path.read_text(encoding="utf-8")
    updated, count = re.subn(
        r"(^\s*default_nav_to_pose_bt_xml:\s*).*$",
        rf"\1{bt_xml_path.as_posix()}",
        content,
        flags=re.MULTILINE,
    )

    if count == 0:
        updated = (
            content.rstrip()
            + "\n\n"
            + "bt_navigator:\n"
            + "  ros__parameters:\n"
            + f"    default_nav_to_pose_bt_xml: {bt_xml_path.as_posix()}\n"
        )

    tmp = tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False)
    tmp.write(updated)
    tmp.flush()
    tmp.close()
    return tmp.name


def _create_bringup_action(context):
    map_path = LaunchConfiguration("map").perform(context)
    params_path = Path(LaunchConfiguration("params_file").perform(context)).expanduser().resolve()
    bt_xml_path = Path(LaunchConfiguration("bt_xml").perform(context)).expanduser().resolve()

    if not params_path.exists():
        raise RuntimeError(f"params_file not found: {params_path}")
    if not bt_xml_path.exists():
        raise RuntimeError(f"bt_xml not found: {bt_xml_path}")

    runtime_params = _rewrite_bt_xml_path(params_path, bt_xml_path)

    return [
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                "/opt/ros/humble/share/nav2_bringup/launch/bringup_launch.py"
            ),
            launch_arguments={
                "map": map_path,
                "params_file": runtime_params,
                "use_sim_time": "False",
                "autostart": "True",
            }.items(),
        )
    ]


def generate_launch_description():
    repo_root = _find_repo_root()
    default_map = repo_root / "src" / "muto_rs_nav" / "maps" / "electronics_room.yaml"
    default_params = repo_root / "src" / "muto_rs_nav" / "params" / "nav2_params.yaml"
    default_bt = repo_root / "src" / "muto_rs_nav" / "behavior_trees" / "nav_to_pose.xml"
    env_bt = os.getenv("MUTO_RS_BT_XML")
    if env_bt:
        env_bt_path = Path(env_bt).expanduser().resolve()
        if not env_bt_path.exists():
            raise RuntimeError(
                f"MUTO_RS_BT_XML points to a missing file: {env_bt_path}"
            )
        default_bt = env_bt_path

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
    bt_xml_arg = DeclareLaunchArgument(
        "bt_xml",
        default_value=default_bt.as_posix(),
        description="Path to BT XML file used by bt_navigator",
    )
    fake_odom_tf_arg = DeclareLaunchArgument(
        "use_fake_odom_tf",
        default_value="True",
        description=(
            "Publish a fallback static transform odom->base_link when no odom TF is available"
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
        arguments=["0", "0", "0", "0", "0", "0", "odom", "base_link"],
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
        bt_xml_arg,
        fake_odom_tf_arg,
        fake_map_tf_arg,
        fake_odom_tf,
        fake_map_tf,
        bringup,
    ])
