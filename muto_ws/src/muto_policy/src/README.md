# muto_policy/src

## Description
C++ sources for the `muto_policy_node` executable. This node consumes robot state, loads the ONNX model, and publishes joint commands.

## Key Files
- [muto_policy_node.cpp](muto_policy_node.cpp): main ROS 2 node and inference logic.

## Usage / Examples
The file is compiled into the `muto_policy_node` executable:

```bash
ros2 run muto_policy muto_policy_node --ros-args -p policy_path:=muto_walk_policy.onnx
```

It relies on the following topics:
- input `JointState` on `/joint_states`
- input IMU on `/muto/imu`
- output `Float64MultiArray` on `/leg_controller/commands`

## Technical Notes
- The actual ONNX tensor names are resolved dynamically through ONNX Runtime; they are not hardcoded.
- The input buffer is assembled in the 18-joint order expected by the code.
- If the model is missing or incompatible, the node stops after an explicit log message before any inference is attempted.
