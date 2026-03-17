# dg3f_m_driver ROS 2 Package 🚀

## 📌 Overview

The `dg3f_m_driver` ROS 2 package provides a hardware interface leveraging [ros2_control](https://control.ros.org/) for the DG-3F-M grippers (12 DOF, 3 fingers × 4 joints), enabling direct robotic control operations.

## 📦 Dependency Installation

### Navigate to Workspace
```bash
cd ~/your_ws
```

### Update rosdep
```bash
apt update
rosdep update
```

### Install Specific Dependencies
```bash
rosdep install --from-paths src/DELTO_M_ROS2/dg3f_m_driver --ignore-src -r -y
```

### Verify Installation by Building
```bash
colcon build --packages-select dg3f_m_driver delto_hardware
```

---

## 🚀 Launch Files

| Launch File | Description | Controller Type |
|-------------|-------------|-----------------|
| `dg3f_m_driver.launch.py` | DG3F-M - JointTrajectoryController | Position (Trajectory) |
| `dg3f_m_pid_controller.launch.py` | DG3F-M - PID Controller | PID (Position→Effort) |
| `dg3f_m_effort_controller.launch.py` | DG3F-M - Direct Effort Control | Effort (Direct) |

---

## 🎛️ Controlling Delto Gripper-3F-M

### 1. Loading DG3F-M controller (JointTrajectory)

Launch the Delto Gripper-3F-M controller with:
```bash
ros2 launch dg3f_m_driver dg3f_m_driver.launch.py delto_ip:=169.254.186.72 delto_port:=502
```

### 2. Loading DG3F-M PID controller

For PID position control (position input → effort output):
```bash
ros2 launch dg3f_m_driver dg3f_m_pid_controller.launch.py delto_ip:=169.254.186.72
```

### 3. Loading DG3F-M Effort controller

For direct effort control:
```bash
ros2 launch dg3f_m_driver dg3f_m_effort_controller.launch.py delto_ip:=169.254.186.72
```

### 4. Test scripts:

| Script | Controller Type | Description |
|--------|-----------------|-------------|
| `dg3f_m_jtc_test.py` | JTC | JointTrajectory based test |
| `dg3f_m_operator_test.py` | Operator | Operator mode test |
| `dg3f_m_effort_test.py` | Effort | Direct effort control test |
| `dg3f_m_effort_simple_test.py` | Effort | Simple effort control test |

**Python Example:**
```bash
ros2 run dg3f_m_driver dg3f_m_jtc_test.py
```

**C++ Example:**
```bash
ros2 run dg3f_m_driver dg3f_m_test_cpp
```

---

## 🔧 Controller Types

### 1. JointTrajectoryController (Default)
- **Purpose**: Smooth trajectory interpolation for position control
- **Joints**: 12 joints (j_dg_1_1~1_4, j_dg_2_1~2_4, j_dg_3_1~3_4)
- **Topic**: `/dg3f_m/delto_controller/joint_trajectory`

### 2. PID Controller (Position → Effort)
- **Purpose**: Position control with effort output using PID feedback
- **Input**: Single position reference value
- **Topic**: `/dg3f_m/pid_controller/reference`

### 3. Effort Controller (Direct)
- **Purpose**: Direct effort control without position feedback
- **Use Case**: Direct force control, impedance control
- **Topic**: `/dg3f_m/effort_controller/commands`

---

## 🌐 Namespace

All DG3F-M drivers use the `/dg3f_m/` namespace to avoid topic conflicts with other grippers.

---

## 🤝 Contributing
Contributions are encouraged:

1. Fork repository
2. Create branch (`git checkout -b feature/my-feature`)
3. Commit changes (`git commit -am 'Add my feature'`)
4. Push (`git push origin feature/my-feature`)
5. Open pull request

## 📄 License
BSD-3-Clause

## 📧 Contact
[TESOLLO SUPPORT](mailto:support@tesollo.com)

