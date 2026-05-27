# muto_description/meshes

## Description
STL mesh repository used by the MUTO-RS URDF. These files serve RViz visualization, collision geometry, and consistency with the mechanical description.

## Key Files
- `base_link.STL`: robot base mesh.
- `imu_Link.STL`, `camera_Link.STL`, `LD_Link.STL`: accessory parts.
- `zq*.STL`, `zz*.STL`, `zh*.STL`, `yq*.STL`, `yz*.STL`, `yh*.STL`: link meshes for the six legs.

## Usage / Examples
The meshes are referenced from the URDF using URIs such as:

```xml
package://muto_description/meshes/base_link.STL
```

They are not executed directly. To verify that the assets resolve correctly, launch the robot in RViz through the main bringup stack:

```bash
ros2 launch muto_bringup bringup.launch.py
```

## Technical Notes
- Any file renaming or path changes must be reflected in `urdf/Muto.urdf`.
- The files use uppercase `.STL`; keep the exact casing expected by the URDF.
