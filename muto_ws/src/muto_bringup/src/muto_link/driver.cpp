#include "muto_link/driver.hpp"

#include <cmath>
#include <stdexcept>

#include "muto_link/errors.hpp"

namespace muto_link {

/**
 * @brief Initialise le pilote avec un transport donné.
 * 
 * @param transport Pointeur unique au transport. Ne doit pas être nullptr.
 * @throws std::invalid_argument si transport est nullptr.
 */
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

/**
 * @brief Ouvre la connexion de transport.
 * 
 * Délègue l'ouverture au transport sous-jacent.
 */
void Driver::open() {
    transport_->open();
}

/**
 * @brief Ferme la connexion de transport.
 * 
 * Délègue la fermeture au transport sous-jacent.
 */
void Driver::close() {
    transport_->close();
}

/**
 * @brief Active le couple sur tous les servos.
 * 
 * Envoie une trame d'écriture au registre de contrôle du couple
 * avec données nulles (signifie "activation").
 */
void Driver::torqueOn() {
    const auto frame = Protocol::buildFrame(
        static_cast<uint8_t>(Instruction::Write), kRegTorque, {0x00});
    transport_->write(frame);
}

/**
 * @brief Désactive le couple sur tous les servos.
 * 
 * Envoie une trame d'écriture au registre de désactivation du couple.
 */
void Driver::torqueOff() {
    const auto frame = Protocol::buildFrame(
        static_cast<uint8_t>(Instruction::Write), kRegTorqueOff, {0x00});
    transport_->write(frame);
}

/**
 * @brief Déplace un servo vers un angle cible.
 * 
 * Conversion d'angle :
 * - Plage utilisateur : [-90°, +90°]
 * - Plage protocole : [-128, +127] (entier signé sur 8 bits)
 * - Formule : protocol_angle = (user_angle * 127 ou 128) / 90
 * 
 * Données de commande :
 * [SERVO_ID] [ANGLE_BYTE] [SPEED_HIGH] [SPEED_LOW]
 * 
 * @param servo_id ID du servo (1-18)
 * @param angle_deg Angle cible (auto-limité à ±90°)
 * @param speed Vitesse de mouvement (0-65535)
 * @throws std::invalid_argument si servo_id n'est pas entre 1 et 18
 */
void Driver::servoMove(uint8_t servo_id, int16_t angle_deg, uint16_t speed) {
    // Validation de l'ID du servo
    if (servo_id < 1 || servo_id > 18) {
        throw std::invalid_argument("Driver::servoMove servo_id must be between 1 and 18");
    }

    // Limitation de l'angle à la plage [-90°, +90°]
    int16_t clamped_angle = angle_deg;
    if (clamped_angle < -90) {
        clamped_angle = -90;
    } else if (clamped_angle > 90) {
        clamped_angle = 90;
    }

    // Conversion de l'angle en format protocole (entier signé 8 bits)
    // Mathématiquement : protocole = (angle * 127 ou 128) / 90
    int16_t protocol_angle = 0;
    if (clamped_angle < 0) {
        protocol_angle = static_cast<int16_t>((clamped_angle * 128 + (clamped_angle > 0 ? 45 : -45)) / 90);
    } else {
        protocol_angle = static_cast<int16_t>((clamped_angle * 127 + 45) / 90);
    }

    // Extraction de l'octet bas de l'angle signé
    const uint8_t angle_byte = static_cast<uint8_t>(protocol_angle & 0xFF);

    // Construction des données de la commande
    std::vector<uint8_t> data;
    data.push_back(servo_id);
    const auto speed_bytes = Protocol::packUint16BE(speed);
    data.push_back(angle_byte);
    data.insert(data.end(), speed_bytes.begin(), speed_bytes.end());

    // Envoi de la trame
    const auto frame = Protocol::buildFrame(
        static_cast<uint8_t>(Instruction::Write), kRegServoPosition, data);
    transport_->write(frame);
}

/**
 * @brief Lit l'angle actuel d'un servo.
 * 
 * @param servo_id ID du servo (1-18)
 * @return Données brutes d'angle
 * @throws std::invalid_argument si servo_id n'est pas entre 1 et 18
 */
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

/**
 * @brief Envoie une commande d'écriture bas-niveau.
 * 
 * @param address Adresse du registre
 * @param data Données à écrire
 */
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

/**
 * @brief Envoie une commande de lecture bas-niveau.
 * 
 * Processus :
 * 1. Envoie la commande de lecture
 * 2. Reçoit l'en-tête de la réponse (3 octets)
 * 3. Extrait la longueur de la trame
 * 4. Reçoit le reste de la trame
 * 5. Valide la structure
 * 6. Retourne les données (sans en-tête, tail, instruction, address, checksum)
 * 
 * @param address Adresse du registre
 * @param data Commande spécifiant quoi lire
 * @return Données de la réponse
 * @throws std::runtime_error en cas d'erreur de communication
 */
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
            if (--retries_left <= 0) {
                throw;
            }
        } catch (const std::runtime_error& e) {
            if (--retries_left <= 0) {
                throw TransportError(std::string("Driver::read transport failure: ") + e.what());
            }
        }
    }
}

} // namespace muto_link
