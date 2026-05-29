"""
bringup.launch.py — Muto RS

SÉQUENCE DE DÉMARRAGE :
  t=0   : robot_state_publisher + controller_manager
  t+1 s : spawner joint_state_broadcaster  (HW interface bien initialisé)
  t+3 s : spawner leg_controller           (broadcaster actif)
  t+6 s : muto_policy_node                 (leg_controller actif)

  Augmentez ces délais si vous observez des échecs sur Pi 5 chargé.

TOPICS (hardcodés dans muto_policy_inference.cpp — aucun remap nécessaire) :
  Subscriptions : /joint_states, /muto/imu
  Publication   : /leg_controller/commands
"""

from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    RegisterEventHandler,
    TimerAction,
    LogInfo,
)
from launch.event_handlers import OnProcessStart
from launch.substitutions import (
    Command,
    FindExecutable,
    LaunchConfiguration,
    PathJoinSubstitution,
)
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():

    # ── Arguments CLI ─────────────────────────────────────────────────────────
    declare_policy_path = DeclareLaunchArgument(
        "policy_path",
        default_value="muto_walk_policy.onnx",
        description="Nom du fichier ONNX (relatif à muto_description/config/) "
                    "ou chemin absolu.",
    )
    policy_path = LaunchConfiguration("policy_path")

    # ── Description URDF ──────────────────────────────────────────────────────
    robot_description_content = Command([
        FindExecutable(name="xacro"),
        " ",
        PathJoinSubstitution([
            FindPackageShare("muto_description"), "urdf", "Muto_complet.urdf.xacro",
        ]),
    ])
    robot_description = {
        "robot_description": ParameterValue(robot_description_content, value_type=str)
    }

    # ── Nœuds ─────────────────────────────────────────────────────────────────
    robot_state_publisher = Node(
        package="robot_state_publisher",
        executable="robot_state_publisher",
        name="robot_state_publisher",
        parameters=[robot_description],
        output="both",
        emulate_tty=True,
    )

    controller_manager = Node(
        package="controller_manager",
        executable="ros2_control_node",
        name="controller_manager",
        parameters=[
            robot_description,
            PathJoinSubstitution([
                FindPackageShare("muto_description"), "config", "controllers.yaml",
            ]),
        ],
        output="both",
        emulate_tty=True,
    )

    joint_state_broadcaster_spawner = Node(
        package="controller_manager",
        executable="spawner",
        name="joint_state_broadcaster_spawner",
        arguments=[
            "joint_state_broadcaster",
            "--controller-manager", "/controller_manager",
        ],
        output="both",
    )

    leg_controller_spawner = Node(
        package="controller_manager",
        executable="spawner",
        name="leg_controller_spawner",
        arguments=[
            "leg_controller",
            "--controller-manager", "/controller_manager",
        ],
        output="both",
    )

    muto_policy_node = Node(
        package="muto_policy",
        executable="muto_policy_node",
        name="muto_policy_inference_node",
        parameters=[{"policy_path": policy_path}],
        output="both",
        emulate_tty=True,
    )

    # ── Séquence de démarrage ─────────────────────────────────────────────────
    delayed_startup = RegisterEventHandler(
        event_handler=OnProcessStart(
            target_action=controller_manager,
            on_start=[
                LogInfo(msg="[bringup] controller_manager démarré."),

                TimerAction(
                    period=1.0,
                    actions=[
                        LogInfo(msg="[bringup] Spawning joint_state_broadcaster…"),
                        joint_state_broadcaster_spawner,
                    ],
                ),

                TimerAction(
                    period=3.0,
                    actions=[
                        LogInfo(msg="[bringup] Spawning leg_controller…"),
                        leg_controller_spawner,
                    ],
                ),

                TimerAction(
                    period=6.0,
                    actions=[
                        LogInfo(msg="[bringup] Démarrage du nœud d'inférence IA…"),
                        muto_policy_node,
                    ],
                ),
            ],
        )
    )

    return LaunchDescription([
        declare_policy_path,
        robot_state_publisher,
        controller_manager,
        delayed_startup,
    ])
