# muto_description/urdf

## Description
Contains the robot mechanical description in URDF and xacro form. These files define the link tree, joints, joint limits, and the `ros2_control` integration required by the runtime stack.

## Key Files
- [Muto_complet.urdf.xacro](Muto_complet.urdf.xacro): full robot model with serial parameters, the hardware plugin, and controller configuration.
- [Muto.urdf](Muto.urdf): mechanical model exported from SolidWorks.

## Usage / Examples
Generate the expanded URDF from xacro:

```bash
xacro muto_ws/src/muto_description/urdf/Muto_complet.urdf.xacro > /tmp/muto.urdf
```

Then load the model through `robot_state_publisher` or the main launch file:

```bash
ros2 launch muto_bringup bringup.launch.py
```

## Technical Notes
- `Muto_complet.urdf.xacro` includes `Muto.urdf` and adds the `ros2_control` block.
- The package relies on the meshes in `meshes/` using URIs of the form `package://muto_description/...`.
- Any joint name change must remain consistent with `controllers.yaml` and `muto_policy_node.cpp`.
