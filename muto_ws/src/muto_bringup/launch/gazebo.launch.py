import os
from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription, TimerAction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch.substitutions import Command, FindExecutable, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare
import os
from ament_index_python.packages import get_package_share_directory

# Permet à Gazebo Harmonic de localiser les fichiers STL du package muto_description
try:
    pkg_description_share = get_package_share_directory('muto_description')
    # On remonte au dossier parent (install/muto_description/share)
    parent_dir = os.path.dirname(pkg_description_share)
    
    if 'GZ_SIM_RESOURCE_PATH' in os.environ:
        os.environ['GZ_SIM_RESOURCE_PATH'] += os.pathsep + parent_dir
    else:
        os.environ['GZ_SIM_RESOURCE_PATH'] = parent_dir
except Exception:
    pass

def generate_launch_description():
    # 1. Générer le robot_description via Xacro avec votre fichier Gazebo
    robot_description_content = Command([
        FindExecutable(name="xacro"), " ",
        PathJoinSubstitution([FindPackageShare("muto_description"), "urdf", "Muto_gazebo.urdf.xacro"])
    ])
    robot_description = {"robot_description": ParameterValue(robot_description_content, value_type=str)}

    robot_state_publisher = Node(
        package="robot_state_publisher",
        executable="robot_state_publisher",
        parameters=[robot_description],
        output="screen",
    )

    # 2. Démarrer le monde Gazebo vide
    gazebo_sim = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            PathJoinSubstitution([FindPackageShare("ros_gz_sim"), "launch", "gz_sim.launch.py"])
        ]),
        launch_arguments={"gz_args": "-r empty.sdf"}.items(),
    )

    # 3. Faire apparaître (spawn) le robot
    # Modifier uniquement l'argument -name ou ajouter le point de départ si nécessaire
    spawn_robot = Node(
        package="ros_gz_sim",
        executable="create",
        arguments=["-topic", "robot_description", "-name", "muto_rs", "-z", "0.2"],
        output="screen",
    )

    # 4. PONT DE SÉCURITÉ (GZ BRIDGE) : Traduit le sujet IMU de Gazebo vers ROS 2
    gz_ros_bridge = Node(
        package="ros_gz_bridge",
        executable="parameter_bridge",
        arguments=[
            '/gazebo/imu@sensor_msgs/msg/Imu[gz.msgs.IMU'
        ],
        remappings=[
            ('/gazebo/imu', '/imu') # Renomme le sujet pour correspondre à votre nœud C++
        ],
        output="screen"
    )

    # 5. Spawners des contrôleurs
    joint_state_broadcaster_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["joint_state_broadcaster"],
    )

    leg_controller_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["leg_controller"],
    )

    return LaunchDescription([
        robot_state_publisher,
        gazebo_sim,
        spawn_robot,
        gz_ros_bridge, # Lance le pont IMU en même temps que la simulation

        TimerAction(period=3.0, actions=[joint_state_broadcaster_spawner]),
        TimerAction(period=4.0, actions=[leg_controller_spawner]),
    ])
