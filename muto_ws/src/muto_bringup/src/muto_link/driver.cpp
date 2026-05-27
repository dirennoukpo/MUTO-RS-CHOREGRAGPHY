#include "muto_link/driver.hpp"

#include <cmath>
#include <stdexcept>

#include "muto_link/errors.hpp"

namespace muto_link {

Driver::Driver(std::unique_ptr<Transport> transport)
    : Driver(std::move(transport), DriverOptions {}) {}

Driver::Driver(std::unique_ptr<Transport> transport, DriverOptions options)
    : transport_(std::move(transport)), options_(options) {
    if (!transport_) {
        throw std::invalid_argument("Driver requires a non-null transport");
    }
    if (options_.read_timeout_sec <= 0.0) {
        throw std::invalid_argument("DriverOptions.read_timeout_sec must be > 0");
    }
    if (options_.retry_count < 1) {
        options_.retry_count = 1;
    }
}

void Driver::open() {
    transport_->open();
}

void Driver::close() {
    transport_->close();
}

void Driver::torqueOn() {
    const auto frame = Protocol::buildFrame(
        static_cast<uint8_t>(Instruction::Write), kRegTorque, {0x00});
    transport_->write(frame);
}

void Driver::torqueOff() {
    const auto frame = Protocol::buildFrame(
        static_cast<uint8_t>(Instruction::Write), kRegTorqueOff, {0x00});
    transport_->write(frame);
}

void Driver::servoMove(uint8_t servo_id, int16_t angle_deg, uint16_t speed) {
    if (servo_id < 1 || servo_id > 18) {
        throw std::invalid_argument("Driver::servoMove servo_id must be between 1 and 18");
    }

    int16_t clamped_angle = angle_deg;
    if (clamped_angle < -90) { clamped_angle = -90; }
    else if (clamped_angle > 90) { clamped_angle = 90; }

    int16_t protocol_angle = 0;
    if (clamped_angle < 0) {
        protocol_angle = static_cast<int16_t>((clamped_angle * 128 + (clamped_angle > 0 ? 45 : -45)) / 90);
    } else {
        protocol_angle = static_cast<int16_t>((clamped_angle * 127 + 45) / 90);
    }

    const uint8_t angle_byte = static_cast<uint8_t>(protocol_angle & 0xFF);

    std::vector<uint8_t> data;
    data.push_back(servo_id);
    const auto speed_bytes = Protocol::packUint16BE(speed);
    data.push_back(angle_byte);
    data.insert(data.end(), speed_bytes.begin(), speed_bytes.end());

    const auto frame = Protocol::buildFrame(
        static_cast<uint8_t>(Instruction::Write), kRegServoPosition, data);
    transport_->write(frame);
}

std::vector<uint8_t> Driver::readServoAngle(uint8_t servo_id) {
    if (servo_id < 1 || servo_id > 18) {
        throw std::invalid_argument("Driver::readServoAngle servo_id must be between 1 and 18");
    }
    return read(kRegServoAngleRead, {servo_id});
}

int16_t Driver::readServoAngleDeg(uint8_t servo_id) {
    const auto raw = readServoAngle(servo_id);
    if (raw.size() < 18) {
        throw ProtocolError("Driver::readServoAngleDeg expected 18 servo bytes in response");
    }

    const uint8_t angle_byte = raw[static_cast<std::size_t>(servo_id - 1)];
    const int16_t protocol_angle = (angle_byte > 127)
        ? static_cast<int16_t>(angle_byte) - 256
        : static_cast<int16_t>(angle_byte);

    const double angle_deg = (protocol_angle < 0)
        ? (static_cast<double>(protocol_angle) / 128.0) * 90.0
        : (static_cast<double>(protocol_angle) / 127.0) * 90.0;
    return static_cast<int16_t>(std::lround(angle_deg));
}

ServoState Driver::readServoState(uint8_t servo_id) {
    return ServoState {
        servo_id,
        readServoAngleDeg(servo_id),
        0,
        true,
    };
}

void Driver::write(uint8_t address, const std::vector<uint8_t>& data) {
    const auto frame = Protocol::buildFrame(static_cast<uint8_t>(Instruction::Write), address, data);
    int retries_left = options_.retry_count;
    while (true) {
        try {
            transport_->write(frame);
            return;
        } catch (const std::runtime_error& e) {
            if (--retries_left <= 0) {
                throw TransportError(std::string("Driver::write failed after retries: ") + e.what());
            }
        }
    }
}

// ─── writeRaw ─────────────────────────────────────────────────────────────────
//
// FIX PRINCIPAL: écriture batch sans reconstruction de trame.
//
// Transmet les octets directement au transport (1 write système + 1 tcdrain).
// Utilisé par MutoHexapodHardware::write() pour envoyer les 18 trames servoMove
// en un seul appel, éliminant 17 tcdrain() superflus (~17ms gagnés à 115200 bauds
// sur adaptateur USB-serial CH340/FTDI).
//
// Aucun retry: les données brutes ne sont pas re-encapsulables.
// En cas d'erreur, l'appelant gère la reprise au cycle suivant.
//
void Driver::writeRaw(const std::vector<uint8_t>& raw_bytes) {
    if (raw_bytes.empty()) { return; }
    try {
        transport_->write(raw_bytes);
    } catch (const std::runtime_error& e) {
        throw TransportError(std::string("Driver::writeRaw failed: ") + e.what());
    }
}

std::vector<uint8_t> Driver::read(uint8_t address, const std::vector<uint8_t>& data) {
    int retries_left = options_.retry_count;
    while (true) {
        try {
            const auto frame = Protocol::buildFrame(static_cast<uint8_t>(Instruction::Read), address, data);
            transport_->write(frame);

            const auto header = transport_->read(3, options_.read_timeout_sec);
            if (header.size() != 3) {
                throw TimeoutError("Driver::read failed to read response header");
            }
            if (header[0] != Protocol::kHeader1 || header[1] != Protocol::kHeader2) {
                throw ValidationError("Driver::read invalid response header");
            }

            const uint8_t frame_len = header[2];
            if (frame_len < 5) {
                throw ValidationError("Driver::read invalid frame length");
            }

            const auto remaining = transport_->read(static_cast<std::size_t>(frame_len) - 3, options_.read_timeout_sec);
            if (remaining.size() < static_cast<std::size_t>(frame_len) - 3) {
                throw TimeoutError("Driver::read incomplete response frame");
            }

            std::vector<uint8_t> full_frame;
            full_frame.reserve(header.size() + remaining.size());
            full_frame.insert(full_frame.end(), header.begin(), header.end());
            full_frame.insert(full_frame.end(), remaining.begin(), remaining.end());

            if (full_frame.size() != frame_len) {
                throw ValidationError("Driver::read response size does not match LEN");
            }

            if (full_frame[full_frame.size() - 2] != Protocol::kTail1 ||
                full_frame[full_frame.size() - 1] != Protocol::kTail2) {
                throw ValidationError("Driver::read invalid response tail");
            }

            if (options_.validate_checksum && !Protocol::validateFrame(full_frame)) {
                throw ProtocolError("Driver::read invalid response checksum");
            }

            return std::vector<uint8_t>(full_frame.begin() + 5, full_frame.end() - 3);
        } catch (const Error&) {
            if (--retries_left <= 0) { throw; }
        } catch (const std::runtime_error& e) {
            if (--retries_left <= 0) {
                throw TransportError(std::string("Driver::read transport failure: ") + e.what());
            }
        }
    }
}

} // namespace muto_link