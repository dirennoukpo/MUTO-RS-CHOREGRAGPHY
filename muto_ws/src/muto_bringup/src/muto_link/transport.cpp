#include "muto_link/transport.hpp"

#include "muto_link/errors.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace muto_link {
namespace {

/**
 * @brief Convertit un débit en bauds (entier) en constante tpos ix.
 * 
 * @param baud Débit en bauds (9600, 19200, 38400, 57600, 115200, 230400, 460800)
 * @return Constante speed_t pour termios
 * @throws std::invalid_argument si le débit n'est pas supporté
 */
speed_t toPosixBaud(int baud) {
    switch (baud) {
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        case 230400:
            return B230400;
#ifdef B460800
        case 460800:
            return B460800;
#endif
        default:
            throw std::invalid_argument("UsbSerial unsupported baud rate");
    }
}

/**
 * @brief Crée une exception runtime_error avec le message d'erreur système.
 * 
 * @param context Description du contexte de l'erreur
 * @return Exception avec le message d'erreur POSIX concaténé
 */
TransportError sysError(const std::string& context) {
    return TransportError(context + ": " + std::strerror(errno));
}

} // namespace

/**
 * @brief Destructeur. Ferme automatiquement le port.
 * 
 * Ignore les exceptions pour éviter les problèmes à la destruction.
 */
UsbSerial::~UsbSerial() {
    try {
        close();
    } catch (...) {
    }
}

/**
 * @brief Ouvre le port série et configure les paramètres.
 * 
 * Processus :
 * 1. Ouverture du port en mode non-bloquant (O_NONBLOCK)
 * 2. Lecture de la configuration actuelle (tcgetattr)
 * 3. Configuration du mode brut (cfmakeraw)
 * 4. Configuration des paramètres (pas de parité, 8 bits, etc.)
 * 5. Configuration du débit
 * 6. Désactivation du contrôle de flux (RTS/CTS)
 * 7. Vidage des buffers après configuration (tcflush)
 * 
 * @throws std::runtime_error si une étape échoue
 */
void UsbSerial::open() {
    // Si déjà ouvert, retour immédiat
    if (fd_ >= 0) {
        return;
    }

    // Ouverture du port en lecture-écriture, mode non-bloquant
    fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        throw sysError("UsbSerial open failed");
    }

    // Lecture de la configuration actuelle du port
    termios tty {};
    if (::tcgetattr(fd_, &tty) != 0) {
        const int saved_errno = errno;
        ::close(fd_);
        fd_ = -1;
        errno = saved_errno;
        throw sysError("UsbSerial tcgetattr failed");
    }

    // Configuration du mode brut
    ::cfmakeraw(&tty);
    
    // Configuration des flags de contrôle (CLOCAL = ignorer les signaux modem, CREAD = activer lecture)
    tty.c_cflag |= (CLOCAL | CREAD);
    
    // Configuration de la parité : pas de parité (PARENB = parité activée)
    tty.c_cflag &= ~PARENB;
    
    // Configuration des bits de stop : 1 bit stop (CSTOPB = 2 bits stop)
    tty.c_cflag &= ~CSTOPB;
    
    // Configuration de la largeur des données : 8 bits (CSIZE = masque)
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    
    // Désactivation du contrôle de flux (RTS/CTS)
    tty.c_cflag &= ~CRTSCTS;

    // Configuration du débit (entrée et sortie)
    const speed_t speed = toPosixBaud(baud_);
    if (::cfsetispeed(&tty, speed) != 0 || ::cfsetospeed(&tty, speed) != 0) {
        const int saved_errno = errno;
        ::close(fd_);
        fd_ = -1;
        errno = saved_errno;
        throw sysError("UsbSerial baud configuration failed");
    }

    // Configuration du timeout : VMIN=0 (non-bloquant), VTIME=0 (pas d'attente)
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    // Application de la nouvelle configuration
    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
        const int saved_errno = errno;
        ::close(fd_);
        fd_ = -1;
        errno = saved_errno;
        throw sysError("UsbSerial tcsetattr failed");
    }

    // Vidage des buffers d'entrée et sortie
    if (::tcflush(fd_, TCIOFLUSH) != 0) {
        const int saved_errno = errno;
        ::close(fd_);
        fd_ = -1;
        errno = saved_errno;
        throw sysError("UsbSerial flush failed");
    }
}

/**
 * @brief Ferme le port série.
 * 
 * Safe : peut être appelé plusieurs fois sans risque.
 */
void UsbSerial::close() {
    if (fd_ >= 0) {
        const int local_fd = fd_;
        fd_ = -1;
        ::close(local_fd);
    }
}

/**
 * @brief Envoie des données complètement via le port.
 * 
 * Avec gestion des interruptions (EINTR). Attend que tous les octets
 * soient réellement envoyés via tcdrain() avant de retourner.
 * 
 * @param data Vecteur d'octets à envoyer
 * @return Nombre d'octets envoyés
 * @throws std::runtime_error si le port est fermé ou si l'écriture échoue
 */
std::size_t UsbSerial::write(const std::vector<uint8_t>& data) {
    if (fd_ < 0) {
        throw TransportError("UsbSerial write: port is closed");
    }

    // Boucle jusqu'à ce que tous les octets soient envoyés
    std::size_t total_written = 0;
    while (total_written < data.size()) {
        const ssize_t n = ::write(
            fd_,
            data.data() + total_written,
            data.size() - total_written
        );

        // Gestion des erreurs et interruptions
        if (n < 0) {
            if (errno == EINTR) {
                continue;  // Retry on interrupt
            }
            throw sysError("UsbSerial write failed");
        }

        total_written += static_cast<std::size_t>(n);
    }

    // Attendre que le buffer de sortie soit vidé (tcdrain = transmission drain)
    if (::tcdrain(fd_) != 0) {
        throw sysError("UsbSerial drain failed");
    }

    return total_written;
}

/**
 * @brief Reçoit des données via le port avec timeout.
 * 
 * Utilise select() pour l'attente efficace jusqu'au timeout.
 * Retourne moins d'octets que demandés en cas de timeout.
 * 
 * @param size Nombre d'octets à lire
 * @param timeout_sec Timeout en secondes (0 = utiliser timeout par défaut)
 * @return Vecteur d'octets reçus (peut être plus petit que size)
 * @throws std::runtime_error si le port est fermé ou si la lecture échoue
 */
std::vector<uint8_t> UsbSerial::read(std::size_t size, double timeout_sec) {
    if (fd_ < 0) {
        throw TransportError("UsbSerial read: port is closed");
    }

    if (size == 0) {
        return {};
    }

    // Utilisation du timeout passé en paramètre, ou du timeout par défaut
    const double effective_timeout = (timeout_sec > 0.0) ? timeout_sec : timeout_sec_;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(effective_timeout);

    std::vector<uint8_t> buffer;
    buffer.reserve(size);

    // Boucle de lecture jusqu'à la fin du timeout ou réception de tous les octets
    while (buffer.size() < size) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            break;  // Timeout atteint
        }

        // Calcul du temps restant et conversion en timeval pour select()
        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - now);
        timeval tv {};
        tv.tv_sec = static_cast<time_t>(remaining.count() / 1000000);
        tv.tv_usec = static_cast<suseconds_t>(remaining.count() % 1000000);

        // Configuration du descriptor set pour select()
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd_, &readfds);

        // Attendre que le port soit disponible à la lecture
        const int ready = ::select(fd_ + 1, &readfds, nullptr, nullptr, &tv);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;  // Retry on interrupt
            }
            throw sysError("UsbSerial select failed");
        }

        if (ready == 0) {
            break;  // Timeout sans données (select retourne 0)
        }

        // Lecture des données disponibles
        uint8_t temp[256];
        const std::size_t to_read = (size - buffer.size() < sizeof(temp)) ? (size - buffer.size()) : sizeof(temp);
        const ssize_t n = ::read(fd_, temp, to_read);

        if (n < 0) {
            if (errno == EINTR) {
                continue;  // Retry on interrupt
            }
            throw sysError("UsbSerial read failed");
        }

        if (n == 0) {
            break;  // EOF
        }

        buffer.insert(buffer.end(), temp, temp + n);
    }

    return buffer;
}

} // namespace muto_link
