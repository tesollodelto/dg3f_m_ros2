# Copyright 2025 TESOLLO
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
#    * Neither the name of the TESOLLO nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

import os
import yaml
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition, UnlessCondition
from launch.substitutions import (
    Command, FindExecutable, LaunchConfiguration, PathJoinSubstitution
)
from launch_ros.actions import Node
from launch_ros.descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare
from ament_index_python.packages import get_package_share_directory


def load_yaml(package_name, file_path):
    package_path = get_package_share_directory(package_name)
    absolute_file_path = os.path.join(package_path, file_path)
    with open(absolute_file_path, "r") as file:
        return yaml.safe_load(file)


def generate_launch_description():
    declared_arguments = []

    declared_arguments.append(
        DeclareLaunchArgument(
            "use_mock",
            default_value="true",
            description="Use mock hardware (true) or real hardware (false)"
        )
    )
    declared_arguments.append(
        DeclareLaunchArgument(
            "delto_ip",
            default_value="169.254.186.72",
            description="IP address for gripper (real hardware only)"
        )
    )
    declared_arguments.append(
        DeclareLaunchArgument(
            "delto_port",
            default_value="502",
            description="Port for gripper (real hardware only)"
        )
    )

    use_mock = LaunchConfiguration("use_mock")
    delto_ip = LaunchConfiguration("delto_ip")
    delto_port = LaunchConfiguration("delto_port")

    ns = "dg3f_m"

    # Mock hardware robot description
    robot_description_mock = Command(
        [
            PathJoinSubstitution([FindExecutable(name="xacro")]),
            " ",
            PathJoinSubstitution(
                [FindPackageShare("dg3f_m_driver"), "urdf",
                 "dg3f_m_mock.xacro"]
            ),
        ]
    )

    # Real hardware robot description
    robot_description_real = Command(
        [
            PathJoinSubstitution([FindExecutable(name="xacro")]),
            " ",
            PathJoinSubstitution(
                [FindPackageShare("dg3f_m_driver"), "urdf",
                 "dg3f_m_ros2_control.xacro"]
            ),
            " ", "delto_ip:=", delto_ip,
            " ", "delto_port:=", delto_port,
            " ", "fingertip_sensor:=false",
            " ", "io:=false",
        ]
    )

    # Load MoveIt configs
    srdf_content = Command(["cat ", PathJoinSubstitution([FindPackageShare("dg3f_m_moveit_config"), "srdf", "dg3f_m.srdf"])])
    robot_description_semantic = {"robot_description_semantic": ParameterValue(srdf_content, value_type=str)}

    kinematics_yaml = load_yaml("dg3f_m_moveit_config", "config/kinematics.yaml")
    joint_limits_yaml = load_yaml("dg3f_m_moveit_config", "config/joint_limits.yaml")
    ompl_planning_yaml = load_yaml("dg3f_m_moveit_config", "config/ompl_planning.yaml")
    moveit_controllers_yaml = load_yaml("dg3f_m_moveit_config", "config/moveit_controllers.yaml")

    moveit_config = {
        "robot_description_kinematics": kinematics_yaml,
        "robot_description_planning": joint_limits_yaml,
        "planning_pipelines": ["ompl"],
        "ompl": ompl_planning_yaml,
    }

    # Mock hardware controller config
    mock_controllers = PathJoinSubstitution(
        [FindPackageShare("dg3f_m_driver"), "config",
         "dg3f_m_mock_controller.yaml"]
    )

    # Real hardware controller config
    real_controllers = PathJoinSubstitution(
        [FindPackageShare("dg3f_m_driver"), "config",
         "dg3f_m_controller.yaml"]
    )

    # --- Mock hardware nodes ---
    control_node_mock = Node(
        package="controller_manager",
        executable="ros2_control_node",
        namespace=ns,
        parameters=[mock_controllers],
        remappings=[
            ("~/robot_description", "/" + ns + "/robot_description"),
        ],
        output="screen",
        condition=IfCondition(use_mock),
    )

    robot_state_pub_mock = Node(
        package="robot_state_publisher",
        executable="robot_state_publisher",
        namespace=ns,
        output="screen",
        parameters=[{"robot_description": robot_description_mock}],
        condition=IfCondition(use_mock),
    )

    # --- Real hardware nodes ---
    control_node_real = Node(
        package="controller_manager",
        executable="ros2_control_node",
        namespace=ns,
        parameters=[real_controllers],
        remappings=[
            ("~/robot_description", "/" + ns + "/robot_description"),
        ],
        output="screen",
        condition=UnlessCondition(use_mock),
    )

    robot_state_pub_real = Node(
        package="robot_state_publisher",
        executable="robot_state_publisher",
        namespace=ns,
        output="screen",
        parameters=[{"robot_description": robot_description_real}],
        condition=UnlessCondition(use_mock),
    )

    # --- Common nodes ---
    joint_state_broadcaster_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["joint_state_broadcaster", "-c", "/" + ns + "/controller_manager"],
        output="screen",
    )

    controller_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["dg3f_m_controller", "-c", "/" + ns + "/controller_manager"],
        output="screen",
    )

    # MoveIt move_group node
    move_group_node = Node(
        package="moveit_ros_move_group",
        executable="move_group",
        namespace=ns,
        output="screen",
        parameters=[
            {"robot_description": robot_description_mock},
            robot_description_semantic,
            moveit_config,
            {"moveit_controller_manager": "moveit_simple_controller_manager/MoveItSimpleControllerManager"},
            moveit_controllers_yaml,
            {"use_sim_time": False},
        ],
    )

    # RViz
    rviz_config = PathJoinSubstitution([FindPackageShare("dg3f_m_moveit_config"), "config", "dg3f_m_moveit.rviz"])

    rviz_node = Node(
        package="rviz2",
        executable="rviz2",
        name="rviz2",
        namespace=ns,
        output="screen",
        arguments=["-d", rviz_config],
        parameters=[
            {"robot_description": robot_description_mock},
            robot_description_semantic,
            moveit_config,
        ],
    )

    nodes = [
        control_node_mock,
        robot_state_pub_mock,
        control_node_real,
        robot_state_pub_real,
        joint_state_broadcaster_spawner,
        controller_spawner,
        move_group_node,
        rviz_node,
    ]

    return LaunchDescription(declared_arguments + nodes)
