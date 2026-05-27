# muto_bringup/launch

## Description
ROS 2 launch files used to initialize the MUTO-RS stack. The main launch brings up the robot description, the controller manager, the controller spawners, and the ONNX policy node.

## Key Files
- [bringup.launch.py](bringup.launch.py): main launch file for the full stack.

## Usage / Examples
Standard launch:

```bash
ros2 launch muto_bringup bringup.launch.py
```

Launch with a specific ONNX model:

```bash
ros2 launch muto_bringup bringup.launch.py policy_path:=muto_walk_policy.onnx
```

The `policy_path` argument accepts either a relative file name resolved inside `muto_description/config/` or an absolute path.

## Technical Notes
- Startup is sequenced to give `controller_manager` and the controller spawners enough time to stabilize before `muto_policy` starts.
- The policy node expects an ONNX model with 24 inputs and 18 outputs.
- `robot_state_publisher` loads `Muto_complet.urdf.xacro` through `xacro`.
