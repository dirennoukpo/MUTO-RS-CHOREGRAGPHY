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

    // ── Torque ───────────────────────────────────────────────────────────────
    //
    // Leçon apprise par reverse-engineering du firmware MUTO (STM32F103) :
    //
    // Le broadcast (0xFE) est ignoré par le firmware pour les commandes torque.
    // Il faut envoyer une trame par servo avec ~5ms de délai entre chaque.
    // torqueOn()  / torqueOff() bouclent sur les 18 servos (IDs 1–18).
    // torqueOnServo() / torqueOffServo() agissent sur un seul servo.
    //
    // Registres :
    //   0x26 = TORQUE_ON  (data = servo_id)
    //   0x27 = TORQUE_OFF (data = servo_id)
    //
    void torqueOn();
    void torqueOff();
    void torqueOnServo(uint8_t servo_id);
    void torqueOffServo(uint8_t servo_id);
    // Variantes RT (1ms inter-servo) pour usage dans le cycle temps-réel.
    void torqueOnRt();
    void torqueOffRt();

    void servoMove(uint8_t servo_id, int16_t angle_deg, uint16_t speed);
    std::vector<uint8_t> readServoAngle(uint8_t servo_id);
    int16_t readServoAngleDeg(uint8_t servo_id);
    ServoState readServoState(uint8_t servo_id);

    void write(uint8_t address, const std::vector<uint8_t>& data);
    std::vector<uint8_t> read(uint8_t address, const std::vector<uint8_t>& data);

    // ─── Écriture batch ───────────────────────────────────────────────────────
    // Envoie des octets bruts directement via le transport (1 write + 1 tcdrain).
    // Utilisé par MutoHexapodHardware::write() pour les 18 trames servoMove.
    void writeRaw(const std::vector<uint8_t>& raw_bytes);

protected:
    std::unique_ptr<Transport> transport_;
    DriverOptions options_;

private:
    static constexpr uint8_t kRegTorqueOn        = 0x26;  // data = servo_id
    static constexpr uint8_t kRegTorqueOff       = 0x27;  // data = servo_id
    static constexpr uint8_t kRegServoPosition   = 0x40;
    static constexpr uint8_t kRegServoAngleRead  = 0x50;

    static constexpr uint8_t kServoIdMin         = 1;
    static constexpr uint8_t kServoIdMax         = 18;
    // Délai inter-servo pour torque on/off (activation unique au démarrage).
    // Firmware MUTO ignore les trames trop rapprochées sur le bus half-duplex.
    static constexpr int kTorqueDelayMs   = 5;
    // Délai réduit pour le cycle RT (torqueOn→write→torqueOff à chaque cycle).
    // 1ms comme le code Python officiel (load_leg/unload_leg).
    static constexpr int kTorqueDelayRtMs = 1;
};

} // namespace muto_link