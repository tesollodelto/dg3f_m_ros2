# DG3F-M ROS 2

[![CI](https://github.com/tesollodelto/dg3f_m_ros2/actions/workflows/ci.yml/badge.svg)](https://github.com/tesollodelto/dg3f_m_ros2/actions/workflows/ci.yml)
![ROS 2 Humble](https://img.shields.io/badge/ROS_2-Humble-blue?logo=ros)
![ROS 2 Jazzy](https://img.shields.io/badge/ROS_2-Jazzy-blue?logo=ros)
![ROS 2 Lyrical](https://img.shields.io/badge/ROS_2-Lyrical-orange?logo=ros)

ROS 2 packages for the **Delto Gripper DG3F-M** (3-finger, medium size).

## Packages

| Package | Description |
|---|---|
| `dg3f_m_description` | URDF/xacro model, meshes, and RViz display launch |
| `dg3f_m_driver` | ros2_control hardware driver and controller launch files |
| `dg3f_m_gz` | Gazebo simulation |
| `dg3f_m_moveit_config` | MoveIt 2 configuration (SRDF, planners, mock hardware) |

## Dependencies

This repository needs two shared packages. Note that the **package** names differ
from the **repository** names:

| Package (colcon) | Repository (git) | Description |
|---|---|---|
| `delto_hardware` | [`dg_hardware`](https://github.com/tesollodelto/dg_hardware) | Unified ros2_control hardware interface |
| `delto_tcp_comm` | [`dg_tcp_comm`](https://github.com/tesollodelto/dg_tcp_comm) | TCP communication library |

Where those packages live depends on how you cloned, so pick **one** of the two
layouts below. Do not mix them: two copies of `delto_hardware` in one workspace
makes `colcon` fail with a duplicate package name.

### Layout A — via the `tesollo_ros2` umbrella repo (recommended)

The shared packages are already included as submodules under `dg_common/`, so
**do not clone them separately**.

```bash
mkdir -p ~/ros2_ws/src && cd ~/ros2_ws/src
git clone https://github.com/tesollodelto/tesollo_ros2.git
cd tesollo_ros2
git submodule update --init dg3f_m_ros2 dg_common/dg_tcp_comm dg_common/dg_hardware
```

```text
~/ros2_ws/src/
└── tesollo_ros2/
    ├── dg3f_m_ros2/
    └── dg_common/
        ├── dg_tcp_comm/     # package delto_tcp_comm
        └── dg_hardware/     # package delto_hardware
```

### Layout B — this repository standalone

```bash
mkdir -p ~/ros2_ws/src && cd ~/ros2_ws/src
git clone https://github.com/tesollodelto/dg3f_m_ros2.git
git clone https://github.com/tesollodelto/dg_hardware.git
git clone https://github.com/tesollodelto/dg_tcp_comm.git
```

```text
~/ros2_ws/src/
├── dg3f_m_ros2/
├── dg_hardware/            # package delto_hardware
└── dg_tcp_comm/            # package delto_tcp_comm
```

## Build

Identical for both layouts — `--packages-up-to` resolves `delto_tcp_comm` and
`delto_hardware` from wherever they are in `src/`:

```bash
cd ~/ros2_ws
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install --packages-up-to dg3f_m_description dg3f_m_driver dg3f_m_gz dg3f_m_moveit_config
source install/setup.bash
```

> **Do not** use `--packages-select dg3f_m_description dg3f_m_driver dg3f_m_gz dg3f_m_moveit_config`. It skips `delto_hardware` and
> `delto_tcp_comm`, and the build then fails at `find_package(delto_hardware)`.

## Launch

```bash
# RViz display
ros2 launch dg3f_m_description dg3f_m_display.launch.py

# Hardware driver
ros2 launch dg3f_m_driver dg3f_m_driver.launch.py

# Effort controller
ros2 launch dg3f_m_driver dg3f_m_effort_controller.launch.py

# PID controller
ros2 launch dg3f_m_driver dg3f_m_pid_controller.launch.py

# Gazebo simulation
ros2 launch dg3f_m_gz dg3f_m_gz.launch.py

# Mock hardware (no device required)
ros2 launch dg3f_m_driver dg3f_m_mock.launch.py

# MoveIt (mock hardware, default)
ros2 launch dg3f_m_moveit_config dg3f_m_moveit.launch.py

# MoveIt (real hardware)
ros2 launch dg3f_m_moveit_config dg3f_m_moveit.launch.py use_mock:=false delto_ip:=169.254.186.72
```
