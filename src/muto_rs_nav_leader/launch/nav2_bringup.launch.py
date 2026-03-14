import os
from pathlib import Path

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, OpaqueFunction
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from nav2_common.launch import RewrittenYaml
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
    bt_xml_path = Path(LaunchConfiguration("bt_xml_file").perform(context)).expanduser().resolve()

    if not params_path.exists():
        raise RuntimeError(f"params_file not found: {params_path}")
    if not bt_xml_path.exists():
        raise RuntimeError(f"bt_xml_file not found: {bt_xml_path}")

    configured_params = RewrittenYaml(
        source_file=params_path.as_posix(),
        root_key="",
        param_rewrites={
            "yaml_filename": map_path,
            "default_nav_to_pose_bt_xml": bt_xml_path.as_posix(),
        },
        convert_types=True,
    )

    return [
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                "/opt/ros/humble/share/nav2_bringup/launch/bringup_launch.py"
            ),
            launch_arguments={
                "map": map_path,
                "params_file": configured_params,
                "use_sim_time": "False",
                "autostart": "True",
                "slam": "False",
                "use_localization": "True",
            }.items(),
        )
    ]


def generate_launch_description():
    repo_root = _find_repo_root()
    default_map = repo_root / "src" / "muto_rs_nav_leader" / "maps" / "electronics_room.yaml"
    default_params = repo_root / "src" / "muto_rs_nav_leader" / "params" / "nav2_params.yaml"
    default_bt_xml = repo_root / "src" / "muto_rs_nav_leader" / "behavior_trees" / "nav_leader.xml"

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
        "bt_xml_file",
        default_value=default_bt_xml.as_posix(),
        description="Path to Nav2 behavior tree xml",
    )
    leader_bridge_arg = DeclareLaunchArgument(
        "use_leader_bridge",
        default_value="True",
        description="Launch the bridge that republishes leader pose and voltage topics expected by the BT",
    )
    leader_pose_source_topic_arg = DeclareLaunchArgument(
        "leader_pose_source_topic",
        default_value="/amcl_pose",
        description="Source topic used to republish leader pose",
    )
    leader_pose_source_type_arg = DeclareLaunchArgument(
        "leader_pose_source_type",
        default_value="pose_with_covariance_stamped",
        description="Type of leader pose source topic: pose_stamped, pose_with_covariance_stamped, odometry",
    )
    leader_voltage_source_topic_arg = DeclareLaunchArgument(
        "leader_voltage_source_topic",
        default_value="/voltage",
        description="Source topic used to republish leader voltage",
    )
    leader_voltage_source_type_arg = DeclareLaunchArgument(
        "leader_voltage_source_type",
        default_value="float32",
        description="Type of leader voltage source topic: float32 or battery_state",
    )
    fake_odom_tf_arg = DeclareLaunchArgument(
        "use_fake_odom_tf",
        default_value="False",
        description=(
            "Publish a fallback static transform odom->base_footprint when no odom TF is available"
        ),
    )
    fake_map_tf_arg = DeclareLaunchArgument(
        "use_fake_map_tf",
        default_value="False",
        description=(
            "Publish a fallback static transform map->odom when no map TF is available"
        ),
    )
    fake_base_link_tf_arg = DeclareLaunchArgument(
        "use_fake_base_link_tf",
        default_value="False",
        description=(
            "Publish a fallback static transform base_footprint->base_link when base_link TF is unavailable"
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
    fake_base_link_tf = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        name="fake_base_link_tf",
        arguments=["0", "0", "0", "0", "0", "0", "base_footprint", "base_link"],
        condition=IfCondition(LaunchConfiguration("use_fake_base_link_tf")),
    )
    leader_bridge = Node(
        package="pkg_leader_bridge",
        executable="leader_bridge",
        name="leader_bridge",
        parameters=[{
            "robot_id": "1",
            "pose_source_topic": LaunchConfiguration("leader_pose_source_topic"),
            "pose_source_type": LaunchConfiguration("leader_pose_source_type"),
            "voltage_source_topic": LaunchConfiguration("leader_voltage_source_topic"),
            "voltage_source_type": LaunchConfiguration("leader_voltage_source_type"),
            "pose_output_topic": "/robot1/pose",
            "voltage_output_topic": "/robot1/voltage",
        }],
        condition=IfCondition(LaunchConfiguration("use_leader_bridge")),
    )

    bringup = OpaqueFunction(function=_create_bringup_action)

    return LaunchDescription([
        map_arg,
        params_arg,
        bt_xml_arg,
        leader_bridge_arg,
        leader_pose_source_topic_arg,
        leader_pose_source_type_arg,
        leader_voltage_source_topic_arg,
        leader_voltage_source_type_arg,
        fake_odom_tf_arg,
        fake_map_tf_arg,
        fake_base_link_tf_arg,
        leader_bridge,
        fake_odom_tf,
        fake_map_tf,
        fake_base_link_tf,
        bringup,
    ])
