#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "muto_link/export.hpp"
#include "muto_link/protocol.hpp"
#include "muto_link/transport.hpp"

namespace muto_link {

struct MUTO_LINK_API DriverOptions {
    double read_timeout_sec = 0.05;
    int retry_count = 1;
    bool validate_checksum = true;
};

struct MUTO_LINK_API ServoState {
    uint8_t id;
    int16_t angle_deg;
    uint16_t speed;
    bool torque_enabled;
};

class MUTO_LINK_API Driver {
public:
    explicit Driver(std::unique_ptr<Transport> transport);
    explicit Driver(std::unique_ptr<Transport> transport, DriverOptions options);
    virtual ~Driver() = default;

    void open();
    void close();
    void torqueOn();
    void torqueOff();
    void servoMove(uint8_t servo_id, int16_t angle_deg, uint16_t speed);
    std::vector<uint8_t> readServoAngle(uint8_t servo_id);
    int16_t readServoAngleDeg(uint8_t servo_id);
    ServoState readServoState(uint8_t servo_id);

    void write(uint8_t address, const std::vector<uint8_t>& data);
    std::vector<uint8_t> read(uint8_t address, const std::vector<uint8_t>& data);

    // ─── FIX: écriture batch ──────────────────────────────────────────────────
    //
    // Envoie des octets bruts directement via le transport, sans encapsulation
    // de trame supplémentaire. Destiné à envoyer un bloc de trames MUTO
    // pré-construites en un seul write() système + un seul tcdrain().
    //
    // Gain: élimine les N-1 appels tcdrain() superflus qui coûtent ~1ms chacun
    // sur un adaptateur USB-serial (CH340, FTDI, CP210x).
    //
    // ATTENTION: les octets doivent être des trames MUTO valides et complètes.
    // Aucune validation ni encapsulation n'est effectuée ici.
    //
    // Exemple d'utilisation:
    //   std::vector<uint8_t> batch;
    //   // ... append 18 trames servoMove complètes (12 bytes chacune) ...
    //   driver.writeRaw(batch);  // 1 write() + 1 tcdrain() au total
    //
    void writeRaw(const std::vector<uint8_t>& raw_bytes);

protected:
    std::unique_ptr<Transport> transport_;
    DriverOptions options_;

private:
    static constexpr uint8_t kRegTorque         = 0x26;
    static constexpr uint8_t kRegServoPosition   = 0x40;
    static constexpr uint8_t kRegTorqueOff       = 0x27;
    static constexpr uint8_t kRegServoAngleRead  = 0x50;
};

} // namespace muto_link