# muto_bringup/src

## Description
This directory contains the C++ implementation of the low-level robot stack. It combines the `ros2_control` hardware plugin, the serial transport layer, and the custom binary MUTO protocol.

The code is split by responsibility: `muto_hardware.cpp` handles ROS 2 and `hardware_interface` integration, while `muto_link/*` implements the USB/UART communication primitives and reusable abstractions.

## Key Files
- [muto_hardware.cpp](muto_hardware.cpp): robot `SystemInterface`, servo control, IMU reading, and ROS 2 publication.
- [muto_link/driver.cpp](muto_link/driver.cpp): high-level command and read logic for servos and sensors.
- [muto_link/protocol.cpp](muto_link/protocol.cpp): frame format, checksum, and basic encoding helpers.
- [muto_link/transport.cpp](muto_link/transport.cpp): POSIX serial implementation (`UsbSerial`).
- [muto_link/sensor.cpp](muto_link/sensor.cpp): IMU access and unit conversions.
- [muto_link/c_api.cpp](muto_link/c_api.cpp): C wrapper for external integration.

## Usage / Examples
This directory is not a standalone executable package; it is compiled as part of `muto_bringup`.

Typical validation workflow after building:

```bash
colcon build --packages-select muto_bringup
ros2 launch muto_bringup bringup.launch.py
```

## Technical Notes
- `muto_hardware.cpp` assumes a maximum of 18 joints and a single serial port shared between servos and IMU traffic.
- The serial code is intentionally sequential: only one physical operation is allowed on the bus at a time.
- The transport depends on Linux/POSIX APIs (`fcntl`, `termios`, `select`), so it is not directly portable without adaptation.
