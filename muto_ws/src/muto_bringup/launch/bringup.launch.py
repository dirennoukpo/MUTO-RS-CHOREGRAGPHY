from launch import LaunchDescription
from launch.actions import TimerAction
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch.substitutions import Command, FindExecutable, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    robot_description_content = Command([
        FindExecutable(name="xacro"),
        " ",
        PathJoinSubstitution([
            FindPackageShare("muto_description"), "urdf", "Muto_complet.urdf.xacro"
        ]),
    ])
    robot_description = {
        "robot_description": ParameterValue(robot_description_content, value_type=str)
    }

    controller_manager = Node(
        package="controller_manager",
        executable="ros2_control_node",
        parameters=[
            robot_description,
            PathJoinSubstitution([
                FindPackageShare("muto_description"), "config", "controllers.yaml"
            ]),
        ],
        output="screen",
    )

    robot_state_publisher = Node(
        package="robot_state_publisher",
        executable="robot_state_publisher",
        parameters=[robot_description],
        output="screen",
    )

    # Attendre 2s que le controller_manager soit prêt (hardware initialisé,
    # service /controller_manager/list_controllers disponible) avant de spawner.
    # OnProcessStart déclenche dès le fork du process, pas dès que le node est prêt.
    joint_state_broadcaster_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=[
            "joint_state_broadcaster",
            "--controller-manager", "/controller_manager",
        ],
    )

    leg_controller_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=[
            "leg_controller",
            "--controller-manager", "/controller_manager",
        ],
    )

    return LaunchDescription([
        robot_state_publisher,
        controller_manager,

        # Délai fixe: laisse le temps au controller_manager de charger le URDF
        # et d'initialiser le hardware (/dev/ttyUSB0) avant de spawner.
        # 2s est largement suffisant sur RPi (mesuré ~0.2s dans les logs).
        TimerAction(period=2.0, actions=[joint_state_broadcaster_spawner]),
        TimerAction(period=3.0, actions=[leg_controller_spawner]),
    ])