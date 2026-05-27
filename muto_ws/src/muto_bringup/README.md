# muto_bringup

## Description
`muto_bringup` is the runtime package for the MUTO-RS robot. It ties together the low-level communication layer (`muto_link`), the `ros2_control` hardware plugin (`muto_hardware`), and the launch sequence that starts the robot description, controller manager, controller spawners, and the policy inference node.

The package is focused on three responsibilities: serial communication over USB/UART, control of the 18 servos, and IMU publication from the hardware plugin. It is the main integration layer between the physical robot, the controller stack, and the policy node.

## Key Files
- [CMakeLists.txt](CMakeLists.txt): builds the shared `muto_link` library and the `muto_hardware` plugin.
- [package.xml](package.xml): declares the ROS 2 dependencies for the package.
- [muto_hardware_plugin.xml](muto_hardware_plugin.xml): registers `muto_hardware/MutoHexapodHardware` as a `hardware_interface::SystemInterface` plugin.
- [launch/bringup.launch.py](launch/bringup.launch.py): starts the full robot bringup sequence.
- [src/muto_hardware.cpp](src/muto_hardware.cpp): implements the `ros2_control` hardware interface and IMU publication.
- [src/muto_link/*.cpp](src/muto_link): low-level protocol, serial transport, driver, sensor wrapper, and C API.
- [include/muto_link/*.hpp](include/muto_link): public headers for the low-level communication layer.

## Usage / Examples
Build the package from the workspace root:

```bash
colcon build --packages-select muto_bringup
source install/setup.bash
```

Start the full stack:

```bash
ros2 launch muto_bringup bringup.launch.py
```

Start the stack with a specific ONNX policy file:

```bash
ros2 launch muto_bringup bringup.launch.py policy_path:=muto_walk_policy.onnx
```

By default, the hardware plugin uses `/dev/ttyUSB0` at 115200 baud. If your serial adapter or device path differs, update the hardware parameters in `muto_description/urdf/Muto_complet.urdf.xacro`.

## Technical Notes
- `muto_hardware/MutoHexapodHardware` controls 18 servos and publishes `/muto/imu`, `/muto/imu/raw`, and `/muto/imu/mag`.
- `muto_link` implements the custom MUTO serial protocol and the POSIX serial backend (`termios`, `select`, `tcdrain`).
- The default configuration assumes a stable USB serial adapter and response times compatible with the launch delays.
- `update_state_from_hardware` is intentionally left `false` for production use so the real-time control loop does not block on serial reads.
