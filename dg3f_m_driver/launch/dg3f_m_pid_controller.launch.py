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


from launch import LaunchDescription
from launch.substitutions import Command, FindExecutable, LaunchConfiguration, PathJoinSubstitution
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition, UnlessCondition
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    # Namespace for this driver
    ns = "dg3f_m"

    # Declare arguments
    declared_arguments = []

    declared_arguments.append(
        DeclareLaunchArgument(
            "delto_ip",
            default_value="169.254.186.72",
            description="IP address for gripper"
        )
    )
    delto_ip = LaunchConfiguration("delto_ip")

    declared_arguments.append(
        DeclareLaunchArgument(
            "delto_port",
            default_value="502",
            description="Port for gripper"
        )
    )

    delto_port = LaunchConfiguration("delto_port")

    declared_arguments.append(
        DeclareLaunchArgument(
            "fingertip_sensor",
            default_value="false",
            description="Enable fingertip force/torque sensors"
        )
    )
    fingertip_sensor = LaunchConfiguration("fingertip_sensor")

    declared_arguments.append(
        DeclareLaunchArgument(
            "io",
            default_value="false",
            description="Enable IO interface"
        )
    )
    io = LaunchConfiguration("io")

    # Get paths to config files
    robot_description_content = Command(
        [
            PathJoinSubstitution([FindExecutable(name="xacro")]),
            " ",
            PathJoinSubstitution(
                [FindPackageShare("dg3f_m_driver"), "urdf",
                 "dg3f_m_ros2_control.xacro"]
            ),
            " ",
            "delto_ip:=",
            delto_ip,
            " ",
            "delto_port:=",
            delto_port,
            " ",
            "fingertip_sensor:=",
            fingertip_sensor,
            " ",
            "io:=",
            io,
        ]
    )

    robot_description = {"robot_description": robot_description_content}

    robot_controllers = PathJoinSubstitution(
        [FindPackageShare("dg3f_m_driver"), "config",
         "dg3f_m_pid_controller.yaml"]
    )

    ft_broadcaster_config = PathJoinSubstitution(
        [FindPackageShare("dg3f_m_driver"), "config",
         "dg3f_m_ft_broadcaster.yaml"]
    )

    # ROS2 Control Node (FT 센서 없음)
    control_node = Node(
        namespace=ns,
        package="controller_manager",
        executable="ros2_control_node",
        parameters=[robot_controllers],
        remappings=[
            ("~/robot_description", "/" + ns + "/robot_description"),
        ],
        output="screen",
        condition=UnlessCondition(fingertip_sensor),
    )

    # ROS2 Control Node (FT 센서 활성화)
    control_node_with_ft = Node(
        namespace=ns,
        package="controller_manager",
        executable="ros2_control_node",
        parameters=[robot_controllers, ft_broadcaster_config],
        remappings=[
            ("~/robot_description", "/" + ns + "/robot_description"),
        ],
        output="screen",
        condition=IfCondition(fingertip_sensor),
    )

    # Robot State Publisher
    robot_state_pub_node = Node(
        namespace=ns,
        package="robot_state_publisher",
        executable="robot_state_publisher",
        output="screen",
        parameters=[robot_description],
    )

    # Joint State Broadcaster
    joint_state_broadcaster_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["joint_state_broadcaster", "-c", "/" + ns + "/controller_manager"],
        output="screen",
    )

    # PID Controller (위치값 입력 → effort 출력)
    pid_controller_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["pid_controller", "-c", "/" + ns + "/controller_manager"],
        output="screen",
    )

    # Fingertip F/T sensor broadcasters (조건부)
    fingertip_1_broadcaster_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["fingertip_1_broadcaster", "-c", "/" + ns + "/controller_manager"],
        output="screen",
        condition=IfCondition(fingertip_sensor),
    )

    fingertip_2_broadcaster_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["fingertip_2_broadcaster", "-c", "/" + ns + "/controller_manager"],
        output="screen",
        condition=IfCondition(fingertip_sensor),
    )

    fingertip_3_broadcaster_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["fingertip_3_broadcaster", "-c", "/" + ns + "/controller_manager"],
        output="screen",
        condition=IfCondition(fingertip_sensor),
    )

    # List all nodes to start
    nodes = [
        control_node,
        control_node_with_ft,
        robot_state_pub_node,
        joint_state_broadcaster_spawner,
        pid_controller_spawner,
        fingertip_1_broadcaster_spawner,
        fingertip_2_broadcaster_spawner,
        fingertip_3_broadcaster_spawner,
    ]

    return LaunchDescription(declared_arguments + nodes)
