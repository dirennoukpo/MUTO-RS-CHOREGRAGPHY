# muto_policy

## Description
C++ policy inference package for MUTO-RS. It loads an ONNX model, assembles observations from `JointState` and IMU data, and publishes leg commands on `/leg_controller/commands`.

The node is intended to run after the `ros2_control` stack is already active, and it depends heavily on the structure of the exported ONNX model.

## Key Files
- [src/muto_policy_node.cpp](src/muto_policy_node.cpp): main ROS 2 inference node.
- [CMakeLists.txt](CMakeLists.txt): ONNX Runtime discovery and executable build logic.
- [package.xml](package.xml): ROS 2 dependencies.

## Usage / Examples
Build the package:

```bash
colcon build --packages-select muto_policy
source install/setup.bash
```

Run the node directly:

```bash
ros2 run muto_policy muto_policy_node --ros-args -p policy_path:=muto_walk_policy.onnx
```

The `policy_path` parameter accepts either a relative file name inside `muto_description/config/` or an absolute path.

## Technical Notes
- The model must expose 24 inputs expected by the node: 18 joint values plus 6 IMU channels.
- The expected output size is 18 values, one per joint.
- ONNX Runtime is searched by CMake in `/usr/local` by default; adjust `ONNXRUNTIME_ROOT` if your installation is elsewhere.
