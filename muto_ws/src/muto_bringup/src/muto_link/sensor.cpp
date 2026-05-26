#include "muto_link/sensor.hpp"

#include <stdexcept>
#include <vector>

namespace muto_link {

/**
 * @brief Convertit une valeur d'angle brute (LSB) en degrés.
 * 
 * Facteur d'échelle : 1° = 100 LSB
 * La valeur est interprétée comme un entier signé.
 * 
 * @param raw Valeur brute (LSB)
 * @return Angle en degrés (positif ou négatif)
 */
float Sensor::toDegrees(uint16_t raw) {
    return static_cast<int16_t>(raw) / kAngleScale;
}

/**
 * @brief Convertit une valeur d'accélération brute en G.
 * 
 * Facteur d'échelle : 1G = 8192 LSB (plage full-scale ±4G)
 * La valeur est interprétée comme un entier signé.
 * 
 * @param raw Valeur brute (LSB)
 * @return Accélération en unités G
 */
float Sensor::toG(uint16_t raw) {
    return static_cast<int16_t>(raw) / kAccelScale;
}

/**
 * @brief Convertit une valeur d'accélération brute en m/s².
 * 
 * Combine la conversion en G avec l'accélération gravitationnelle standard.
 * a(m/s²) = a(G) × 9.80665
 * 
 * @param raw Valeur brute (LSB)
 * @return Accélération en m/s²
 */
float Sensor::toMs2(uint16_t raw) {
    return toG(raw) * kGravityMs2;
}

/**
 * @brief Convertit une valeur de gyroscope brute en degrés par seconde.
 * 
 * Facteur d'échelle : 1°/s = 16.4 LSB (plage full-scale ±2000°/s)
 * La valeur est interprétée comme un entier signé.
 * 
 * @param raw Valeur brute (LSB)
 * @return Vitesse angulaire en °/s
 */
float Sensor::toDegPerSec(uint16_t raw) {
    return static_cast<int16_t>(raw) / kGyroScale;
}

/**
 * @brief Récupère les angles d'Euler fusionnés et la température.
 * 
 * Lit depuis le registre kRegImuAngle (0x60) la trame de réponse contenant :
 * [ROLL_H][ROLL_L][PITCH_H][PITCH_L][YAW_H][YAW_L][TEMP]
 * 
 * Soit 7 octets au total. Les angles sont en big-endian 16-bit.
 * 
 * @return Structure IMUAngleData avec angles en LSB et température en °C
 * @throws std::runtime_error si la réponse n'a pas la bonne taille
 */
IMUAngleData Sensor::getImuAngle() {
    // Commande : lire 7 octets de données (0x07)
    const auto response = read(kRegImuAngle, {0x07});
    
    // Vérification que la réponse a la bonne taille
    if (response.size() != 7) {
        throw std::runtime_error("Sensor::getImuAngle response too short");
    }

    // Décodage des angles (big-endian 16-bit) et température
    return {
        Protocol::unpackUint16BE({response[0], response[1]}),  // Roll
        Protocol::unpackUint16BE({response[2], response[3]}),  // Pitch
        Protocol::unpackUint16BE({response[4], response[5]}),  // Yaw
        response[6]                                             // Temperature
    };
}

EulerAnglesDeg Sensor::getImuAngleDegrees() {
    const auto raw = getImuAngle();
    return EulerAnglesDeg {
        toDegrees(raw.roll),
        toDegrees(raw.pitch),
        toDegrees(raw.yaw),
        raw.temperature,
    };
}

/**
 * @brief Récupère toutes les données brutes de l'IMU 9-axes.
 * 
 * Lit depuis le registre kRegImuRaw (0x61) la trame de réponse contenant :
 * [ACCEL_X_H][ACCEL_X_L][ACCEL_Y_H][ACCEL_Y_L][ACCEL_Z_H][ACCEL_Z_L]
 * [GYRO_X_H][GYRO_X_L][GYRO_Y_H][GYRO_Y_L][GYRO_Z_H][GYRO_Z_L]
 * [MAG_X_H][MAG_X_L][MAG_Y_H][MAG_Y_L][MAG_Z_H][MAG_Z_L]
 * 
 * Soit 18 octets au total. Tous les champs sont en big-endian 16-bit.
 * 
 * @return Structure RawIMUData avec les 9 axes en LSB
 * @throws std::runtime_error si la réponse n'a pas la bonne taille
 */
RawIMUData Sensor::getRawImuData() {
    // Commande : lire 18 octets de données (0x12 = 18 en hexadécimal)
    const auto response = read(kRegImuRaw, {0x12});
    
    // Vérification que la réponse a la bonne taille
    if (response.size() != 18) {
        throw std::runtime_error("Sensor::getRawImuData response too short");
    }

    // Décodage de tous les 9 axes (big-endian 16-bit)
    return {
        Protocol::unpackUint16BE({response[0], response[1]}),   // Accel X
        Protocol::unpackUint16BE({response[2], response[3]}),   // Accel Y
        Protocol::unpackUint16BE({response[4], response[5]}),   // Accel Z
        Protocol::unpackUint16BE({response[6], response[7]}),   // Gyro X
        Protocol::unpackUint16BE({response[8], response[9]}),   // Gyro Y
        Protocol::unpackUint16BE({response[10], response[11]}), // Gyro Z
        Protocol::unpackUint16BE({response[12], response[13]}), // Mag X
        Protocol::unpackUint16BE({response[14], response[15]}), // Mag Y
        Protocol::unpackUint16BE({response[16], response[17]})  // Mag Z
    };
}

Imu9AxesPhysical Sensor::getImuPhysical() {
    const auto raw = getRawImuData();
    return Imu9AxesPhysical {
        toMs2(raw.accel_x),
        toMs2(raw.accel_y),
        toMs2(raw.accel_z),
        toDegPerSec(raw.gyro_x),
        toDegPerSec(raw.gyro_y),
        toDegPerSec(raw.gyro_z),
        static_cast<float>(static_cast<int16_t>(raw.mag_x)),
        static_cast<float>(static_cast<int16_t>(raw.mag_y)),
        static_cast<float>(static_cast<int16_t>(raw.mag_z)),
    };
}

} // namespace muto_link
