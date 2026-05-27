# muto_description

## Description
Robot description package for MUTO-RS. It contains the URDF/xacro model, the STL meshes, and the `ros2_control` configuration used by the bringup stack.

This package is mostly static: it does not provide an executable node, but it supplies the shared resources consumed by `robot_state_publisher`, `ros2_control_node`, and visualization tools.

## Key Files
- [urdf/Muto_complet.urdf.xacro](urdf/Muto_complet.urdf.xacro): complete robot model with `ros2_control` integration.
- [urdf/Muto.urdf](urdf/Muto.urdf): mechanical description exported from SolidWorks.
- [config/controllers.yaml](config/controllers.yaml): `joint_state_broadcaster` and `leg_controller` configuration.
- [meshes/](meshes): STL meshes for the robot links.

## Usage / Examples
The resources are loaded by the main bringup launch:

```bash
ros2 launch muto_bringup bringup.launch.py
```

To inspect the URDF manually:

```bash
ros2 run robot_state_publisher robot_state_publisher --ros-args -p robot_description:="$(xacro muto_ws/src/muto_description/urdf/Muto_complet.urdf.xacro)"
```

## Technical Notes
- Controller joint names must stay aligned with the URDF and the policy code (`zq*`, `zz*`, `zh*`, `yq*`, `yz*`, `yh*`).
- The joint limits are approximately `[-1.57, 1.57]` rad for all 18 axes.
- The hardware plugin is referenced as `muto_hardware/MutoHexapodHardware` in the `ros2_control` block.
