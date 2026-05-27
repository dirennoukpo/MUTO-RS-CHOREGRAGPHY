# muto_description/config

## Description
Shared runtime configuration for the MUTO-RS stack. This directory mainly contains the `ros2_control` controller definitions and the parameters required for robot operation.

## Key Files
- [controllers.yaml](controllers.yaml): declares `controller_manager`, `joint_state_broadcaster`, and `leg_controller`.

## Usage / Examples
The file is consumed automatically by `ros2_control_node` from the main launch file. It is not usually loaded manually.

Example of starting the full stack:

```bash
ros2 launch muto_bringup bringup.launch.py
```

## Technical Notes
- `update_rate` is set to 50 Hz in the current configuration.
- `leg_controller` is a `position_controllers/JointGroupPositionController` over all 18 robot joints.
- The file assumes the hardware plugin exposes position interfaces for every declared joint.
