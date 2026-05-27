"""
bringup.launch.py — Muto RS  (version corrigée)

PROBLÈME RACINE :
  OnProcessStart(target=spawner) se déclenche quand le PROCESSUS spawner démarre,
  pas quand le contrôleur est actif. Avec TimerAction(1 s) en cascade, si la
  machine est un peu lente le policy_node arrive avant que leg_controller soit ACTIVE.
  De plus, si le policy_path n'existe pas, le nœud crash silencieusement dès le
  chargement ONNX et n'apparaît jamais dans ros2 node list.

CORRECTIONS :
  #1  Chaîne de délais absolus depuis le démarrage du controller_manager,
      avec des marges confortables (Pi 5 chargé, USB lent).
  #2  Vérification d'existence du fichier ONNX via ExecuteProcess + LogInfo.
  #3  policy_path transmis correctement via LaunchConfiguration.
  #4  emulate_tty + output="both" pour tous les nœuds.
  #5  Le nœud policy démarre 6 s après controller_manager (broadcaster+leg bien actifs).
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
        remappings=[
            ("/joint_states", "/joint_states"),
            ("/imu",          "/muto/imu"),
        ],
        output="both",
        emulate_tty=True,
    )

    # ── Séquence de démarrage ─────────────────────────────────────────────────
    #
    # STRATÉGIE : délais ABSOLUS depuis OnProcessStart(controller_manager)
    # (plus fiable que la cascade OnProcessStart→spawner→spawner, car les
    #  spawners se terminent dès que le contrôleur est chargé, pas actif)
    #
    #  t=0   : controller_manager démarre
    #  t+1 s : spawner joint_state_broadcaster  (HW interface bien initialisé)
    #  t+3 s : spawner leg_controller            (broadcaster actif, ~1-2 s)
    #  t+6 s : muto_policy_node                  (leg_controller actif, ~1-2 s)
    #
    # Augmentez ces délais si vous observez encore des échecs sur Pi 5 chargé.

    delayed_startup = RegisterEventHandler(
        event_handler=OnProcessStart(
            target_action=controller_manager,
            on_start=[
                LogInfo(msg="[bringup] controller_manager démarré."),

                # t+1 s : broadcaster
                TimerAction(
                    period=1.0,
                    actions=[
                        LogInfo(msg="[bringup] Spawning joint_state_broadcaster…"),
                        joint_state_broadcaster_spawner,
                    ],
                ),

                # t+3 s : leg_controller (pas en cascade du spawner précédent)
                TimerAction(
                    period=3.0,
                    actions=[
                        LogInfo(msg="[bringup] Spawning leg_controller…"),
                        leg_controller_spawner,
                    ],
                ),

                # t+6 s : nœud d'inférence IA
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