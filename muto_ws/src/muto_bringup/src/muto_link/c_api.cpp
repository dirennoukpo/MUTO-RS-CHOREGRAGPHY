 #include <cstdint>
#include <memory>
#include <string>

#include "muto_link/export.hpp"
#include "muto_link/driver.hpp"
#include "muto_link/sensor.hpp"
#include "muto_link/transport.hpp"

namespace muto_link {
namespace {

struct CApiHandle {
    std::unique_ptr<Transport> transport;
    std::unique_ptr<Sensor> sensor;
    std::string last_error;
};

int fail(CApiHandle* handle, const std::exception& error) {
    if (handle) {
        handle->last_error = error.what();
    }
    return -1;
}

} // namespace
} // namespace muto_link

extern "C" {

typedef struct muto_handle {
    muto_link::CApiHandle impl;
} muto_handle;

typedef struct muto_imu_angles {
    float roll;
    float pitch;
    float yaw;
    uint8_t temperature_c;
} muto_imu_angles;

typedef struct muto_raw_imu_data {
    uint16_t accel_x;
    uint16_t accel_y;
    uint16_t accel_z;
    uint16_t gyro_x;
    uint16_t gyro_y;
    uint16_t gyro_z;
    uint16_t mag_x;
    uint16_t mag_y;
    uint16_t mag_z;
} muto_raw_imu_data;

MUTO_LINK_API muto_handle* muto_create_usb(const char* port, int baud) {
    try {
        auto* handle = new muto_handle;
        handle->impl.transport = std::make_unique<muto_link::UsbSerial>(port ? std::string(port) : std::string{}, baud);
        muto_link::DriverOptions options;
        options.read_timeout_sec = 0.015;  // 15ms timeout to accommodate protocol recovery at startup
        options.retry_count = 2;           // Retry once on failure for better resilience
        options.validate_checksum = true;
        handle->impl.sensor = std::make_unique<muto_link::Sensor>(std::move(handle->impl.transport), options);
        return handle;
    } catch (...) {
        return nullptr;
    }
}

MUTO_LINK_API int muto_open(muto_handle* handle) {
    if (!handle) {
        return -1;
    }
    try {
        if (handle->impl.sensor) {
            handle->impl.sensor->open();
            return 0;
        }
        return -1;
    } catch (const std::exception& error) {
        handle->impl.last_error = error.what();
        return -1;
    }
}

MUTO_LINK_API int muto_close(muto_handle* handle) {
    if (!handle) {
        return -1;
    }
    try {
        if (handle->impl.sensor) {
            handle->impl.sensor->close();
            return 0;
        }
        return -1;
    } catch (const std::exception& error) {
        handle->impl.last_error = error.what();
        return -1;
    }
}

MUTO_LINK_API int muto_torque_on(muto_handle* handle) {
    if (!handle || !handle->impl.sensor) {
        return -1;
    }
    try {
        handle->impl.sensor->torqueOn();
        return 0;
    } catch (const std::exception& error) {
        return fail(&handle->impl, error);
    }
}

MUTO_LINK_API int muto_torque_off(muto_handle* handle) {
    if (!handle || !handle->impl.sensor) {
        return -1;
    }
    try {
        handle->impl.sensor->torqueOff();
        return 0;
    } catch (const std::exception& error) {
        return fail(&handle->impl, error);
    }
}

MUTO_LINK_API int muto_servo_move(muto_handle* handle, uint8_t id, int16_t angle, uint16_t speed) {
    if (!handle || !handle->impl.sensor) {
        return -1;
    }
    try {
        handle->impl.sensor->servoMove(id, angle, speed);
        return 0;
    } catch (const std::exception& error) {
        return fail(&handle->impl, error);
    }
}

MUTO_LINK_API int muto_read_servo_angle_deg(muto_handle* handle, uint8_t id, int16_t* out_angle_deg) {
    if (!handle || !handle->impl.sensor || !out_angle_deg) {
        return -1;
    }
    try {
        *out_angle_deg = handle->impl.sensor->readServoAngleDeg(id);
        return 0;
    } catch (const std::exception& error) {
        return fail(&handle->impl, error);
    }
}

MUTO_LINK_API int muto_get_imu_angles(muto_handle* handle, muto_imu_angles* out) {
    if (!handle || !handle->impl.sensor || !out) {
        return -1;
    }
    try {
        const auto data = handle->impl.sensor->getImuAngleDegrees();
        out->roll = data.roll;
        out->pitch = data.pitch;
        out->yaw = data.yaw;
        out->temperature_c = data.temperature_c;
        return 0;
    } catch (const std::exception& error) {
        return fail(&handle->impl, error);
    }
}

MUTO_LINK_API int muto_get_raw_imu_data(muto_handle* handle, muto_raw_imu_data* out) {
    if (!handle || !handle->impl.sensor || !out) {
        return -1;
    }
    try {
        const auto data = handle->impl.sensor->getRawImuData();
        out->accel_x = data.accel_x;
        out->accel_y = data.accel_y;
        out->accel_z = data.accel_z;
        out->gyro_x = data.gyro_x;
        out->gyro_y = data.gyro_y;
        out->gyro_z = data.gyro_z;
        out->mag_x = data.mag_x;
        out->mag_y = data.mag_y;
        out->mag_z = data.mag_z;
        return 0;
    } catch (const std::exception& error) {
        return fail(&handle->impl, error);
    }
}

MUTO_LINK_API const char* muto_last_error(muto_handle* handle) {
    if (!handle) {
        return "null handle";
    }
    return handle->impl.last_error.c_str();
}

MUTO_LINK_API void muto_destroy(muto_handle* handle) {
    delete handle;
}

} // extern "C"
