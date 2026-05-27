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

    # ─── NŒUD D'INFÉRENCE DE LA POLITIQUE IA (C++) ───
    # Ce nœud charge le modèle ONNX et pilote le leg_controller à 50Hz.
    # Vous pouvez écraser le nom de la politique au lancement via les arguments ROS si besoin.
    muto_policy_node = Node(
        package="muto_bringup",
        executable="muto_policy_node",
        name="muto_policy_inference_node",
        parameters=[
            {"policy_path": "muto_walk_policy.onnx"} # Cherchera dans muto_description/config/
        ],
        output="screen",
    )

    return LaunchDescription([
        robot_state_publisher,
        controller_manager,

        # Délai fixe : laisse le temps au controller_manager de charger le URDF
        # et d'initialiser le hardware (/dev/ttyUSB0) avant de spawner.
        TimerAction(period=2.0, actions=[joint_state_broadcaster_spawner]),
        TimerAction(period=3.0, actions=[leg_controller_spawner]),

        # Sécurité : On attend que le leg_controller soit pleinement fonctionnel (à T=3s) 
        # avant d'activer le flux de commandes de la politique d'apprentissage (à T=4s).
        TimerAction(period=4.0, actions=[muto_policy_node]),
    ])
