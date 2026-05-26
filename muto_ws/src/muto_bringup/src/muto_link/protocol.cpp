#include "muto_link/protocol.hpp"

#include <stdexcept>

namespace muto_link {

/**
 * @brief Construit une trame complète et valide selon le protocole MUTO.
 * 
 * La trame a la structure suivante :
 * [0x55][0x00] | [LEN][INSTR][ADDR][DATA...] | [CHK] | [0x00][0xAA]
 *    EN-TÊTE    |        PAYLOAD             | CHK    |   QUEUE
 * 
 * @param instruction Type d'instruction (Write=0x01, Read=0x02, Reply=0x12)
 * @param address Adresse du registre cible
 * @param data Données à inclure (max 250 octets)
 * @return Vecteur d'octets formant une trame complète
 */
std::vector<uint8_t> Protocol::buildFrame(uint8_t instruction, uint8_t address, const std::vector<uint8_t>& data) {
    // Validation de l'instruction
    if (instruction != static_cast<uint8_t>(Instruction::Write) &&
        instruction != static_cast<uint8_t>(Instruction::Read) &&
        instruction != static_cast<uint8_t>(Instruction::Reply)) {
        throw std::invalid_argument("Protocol::buildFrame unsupported instruction");
    }

    // Vérification que les données ne dépassent pas la limite
    if (data.size() > kMaxData) {
        throw std::invalid_argument("Protocol::buildFrame data too large");
    }

    // Calcul de la longueur totale de la trame
    // LEN = 8 + taille des données
    // (1 pour LEN + 1 pour INSTR + 1 pour ADDR + données + 1 pour CHK + 2 pour queue)
    const uint8_t len = static_cast<uint8_t>(data.size() + 8);
    
    // Construction du payload (LEN + INSTR + ADDR + DATA)
    std::vector<uint8_t> payload;
    payload.reserve(3 + data.size());
    payload.push_back(len);
    payload.push_back(instruction);
    payload.push_back(address);
    payload.insert(payload.end(), data.begin(), data.end());

    // Calcul de la somme de contrôle
    const uint8_t chk = checksum(payload);

    // Construction de la trame complète
    std::vector<uint8_t> frame;
    frame.reserve(payload.size() + 5);
    frame.push_back(kHeader1);                       // 0x55
    frame.push_back(kHeader2);                       // 0x00
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(chk);
    frame.push_back(kTail1);                         // 0x00
    frame.push_back(kTail2);                         // 0xAA
    return frame;
}

/**
 * @brief Calcule la somme de contrôle selon l'algorithme MUTO.
 * 
 * Algorithme : CHK = 255 - (somme_totale % 256)
 * La somme porte sur tous les octets du payload.
 * 
 * @param payload Les octets du payload (LEN + INSTR + ADDR + DATA)
 * @return L'octet de checksum
 */
uint8_t Protocol::checksum(const std::vector<uint8_t>& payload) {
    uint16_t sum = 0;
    for (uint8_t b : payload) {
        sum += b;
    }
    return static_cast<uint8_t>(255 - (sum % 256));
}

/**
 * @brief Valide une trame reçue.
 * 
 * Vérifie :
 * - La présence des en-têtes et queue
 * - La cohérence de la longueur
 * - La validité de la somme de contrôle
 * 
 * @param frame La trame complète à valider
 * @return true si la trame est valide, false sinon
 */
bool Protocol::validateFrame(const std::vector<uint8_t>& frame) {
    // Vérification de la taille minimale (en-tête + LEN + INSTR + ADDR + CHK + queue)
    if (frame.size() < 7) {
        return false;
    }
    
    // Vérification des en-têtes
    if (frame[0] != kHeader1 || frame[1] != kHeader2) {
        return false;
    }
    
    // Vérification de la queue
    if (frame[frame.size() - 2] != kTail1 || frame[frame.size() - 1] != kTail2) {
        return false;
    }

    // Vérification de la longueur déclarée vs réelle
    const uint8_t len = frame[2];
    if (frame.size() != static_cast<std::size_t>(len)) {
        return false;
    }

    // Vérification de la somme de contrôle
    const uint8_t received_chk = frame[frame.size() - 3];
    std::vector<uint8_t> payload(frame.begin() + 2, frame.end() - 3);
    return checksum(payload) == received_chk;
}

/**
 * @brief Encode une valeur 16 bits en format big-endian (réseau).
 * 
 * @param value Valeur à encoder
 * @return Vecteur de 2 octets [MSB, LSB]
 */
std::vector<uint8_t> Protocol::packUint16BE(uint16_t value) {
    return {static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF)};
}

/**
 * @brief Décode une valeur 16 bits depuis le format big-endian.
 * 
 * @param bytes Vecteur d'au moins 2 octets en big-endian [MSB, LSB]
 * @return Valeur 16 bits décodée
 * @throws std::invalid_argument si bytes contient moins de 2 octets
 */
uint16_t Protocol::unpackUint16BE(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 2) {
        throw std::invalid_argument("Protocol::unpackUint16BE expects at least 2 bytes");
    }
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}

} // namespace muto_link
