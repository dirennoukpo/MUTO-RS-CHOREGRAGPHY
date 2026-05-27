# muto_link

## Description
Public interface for the low-level library shared by `muto_bringup`. It exposes the custom MUTO protocol, the serial transport, the servo/IMU driver, and a C wrapper for non-ROS integrations.

This directory does not contain application logic. It defines the reusable abstractions consumed by the hardware plugin and, optionally, by other C or C++ programs.

## Key Files
- [transport.hpp](transport.hpp): `Transport` interface and `UsbSerial` implementation.
- [protocol.hpp](protocol.hpp): frame format, checksum, and encoding helpers.
- [driver.hpp](driver.hpp): high-level API for servos and reads.
- [sensor.hpp](sensor.hpp): `Driver` extension for the 9-axis IMU.
- [errors.hpp](errors.hpp): `muto_link` exception types.
- [export.hpp](export.hpp): shared-library export macros.
- [c_api.h](c_api.h): C declaration of the external wrapper.

## Usage / Examples
Include the headers from another C++ target built with `muto_bringup`:

```cpp
#include "muto_link/sensor.hpp"
```

Create a serial transport:

```cpp
auto transport = std::make_unique<muto_link::UsbSerial>("/dev/ttyUSB0", 115200);
```

## Technical Notes
- The library assumes a Linux environment with access to POSIX serial APIs.
- `MUTO_LINK_API` handles symbol export/import depending on the platform and build type.
- The protocol uses a custom binary frame with header `0x55 0x00` and tail `0x00 0xAA`.
