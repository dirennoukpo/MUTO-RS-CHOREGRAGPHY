// ═══════════════════════════════════════════════════════════════════════════════
// muto_hardware.cpp — ros2_control SystemInterface pour hexapode MUTO
// ═══════════════════════════════════════════════════════════════════════════════
//
// ARCHITECTURE IMU (ajout v2):
//
//   Le port série est partagé entre les servos et l'IMU.
//   Contrainte: une seule opération série à la fois (half-duplex).
//
//   Solution: thread IMU dédié + mutex série.
//
//   ┌─────────────────────────────────────────────────────────────┐
//   │  Cycle RT 50Hz (thread controller_manager)                  │
//   │    write() → batch servo 18 trames → driver_->writeRaw()    │
//   │    read()  → state = command (0ms)                          │
//   │    Les deux acquièrent serial_mutex_ avant tout accès série  │
//   ├─────────────────────────────────────────────────────────────┤
//   │  Thread IMU (~10Hz, indépendant du cycle RT)                 │
//   │    Attend serial_mutex_ (non-bloquant si cycle RT actif)     │
//   │    sensor_->getImuAngleDegrees()   ~26ms                    │
//   │    sensor_->getImuPhysical()       ~26ms                    │
//   │    Copie dans imu_cache_ (protégé par imu_cache_mutex_)     │
//   │    Relâche serial_mutex_                                     │
//   ├─────────────────────────────────────────────────────────────┤
//   │  Node interne ROS2 (même processus)                         │
//   │    Publication depuis thread IMU (publishers thread-safe)    │
//   │    Topics: /muto/imu, /muto/imu/temp, /muto/imu/mag        │
//   └─────────────────────────────────────────────────────────────┘
//
// Paramètres xacro nouveaux:
//   imu_publish_rate  (défaut: 10.0) Hz de publication IMU (max 50)
//   imu_frame_id      (défaut: "imu_link") frame TF de l'IMU
//
// CHECKSUM (protocole MUTO):
//   check = 255 - (length + W/R + addr + data...) % 256
//   Vérifié sur les exemples du protocole:
//     Buzzer:  255-(0x09+0x01+0x18+0xFF)%256 = 0xDE ✓
//     Restore: 255-(0x09+0x01+0x06+0x00)%256 = 0xEF ✓
//
// LIMITATION sim_time:
//   Les timestamps IMU utilisent RCL_SYSTEM_TIME.
//   Avec use_sim_time=true, les topics ROS2 auront des timestamps wall-clock,
//   non synchronisés avec le temps de simulation. Acceptable pour du hardware réel.
//
// ═══════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "hardware_interface/handle.hpp"
#include "hardware_interface/system_interface.hpp"
#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "pluginlib/class_list_macros.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/state.hpp"

// Messages IMU
#include "sensor_msgs/msg/imu.hpp"
#include "sensor_msgs/msg/magnetic_field.hpp"
#include "sensor_msgs/msg/temperature.hpp"

#include "muto_link/sensor.hpp"   // Sensor hérite de Driver: toutes les méthodes servo disponibles
#include "muto_link/errors.hpp"
#include "muto_link/transport.hpp"

namespace muto_hardware {
namespace {

constexpr double   kPi                = 3.14159265358979323846;
constexpr double   kRadToDeg          = 180.0 / kPi;
constexpr double   kDegToRad          = kPi / 180.0;
constexpr uint16_t kDefaultServoSpeed = 300;
// Trame single servo (addr=0x40): 0x55 0x00 len instr addr id angle spd_h spd_l chk 0x00 0xAA
// = 12 octets. Vérifié contre protocole MUTO §"Control a single steering gear angle".
constexpr std::size_t kServoFrameSize = 12;
constexpr uint8_t  kRegServoPosition  = 0x40;

// ─── Quaternion RPY sans dépendance tf2 ──────────────────────────────────────
struct Quat { double x, y, z, w; };
Quat rpy_to_quat(double roll_deg, double pitch_deg, double yaw_deg) {
  const double r = roll_deg  * kDegToRad;
  const double p = pitch_deg * kDegToRad;
  const double y = yaw_deg   * kDegToRad;
  const double cr = std::cos(r * 0.5), sr = std::sin(r * 0.5);
  const double cp = std::cos(p * 0.5), sp = std::sin(p * 0.5);
  const double cy = std::cos(y * 0.5), sy = std::sin(y * 0.5);
  return {
    sr*cp*cy - cr*sp*sy,
    cr*sp*cy + sr*cp*sy,
    cr*cp*sy - sr*sp*cy,
    cr*cp*cy + sr*sp*sy
  };
}

// ─── Helpers parsing ──────────────────────────────────────────────────────────
bool parse_bool(const std::string & value, bool default_value) {
  if (value.empty()) return default_value;
  std::string s = value;
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
  if (s == "true"  || s == "1" || s == "yes" || s == "on")  return true;
  if (s == "false" || s == "0" || s == "no"  || s == "off") return false;
  return default_value;
}

double protocol_to_degrees(uint8_t angle_byte) {
  const int16_t p = (angle_byte > 127)
    ? static_cast<int16_t>(angle_byte) - 256
    : static_cast<int16_t>(angle_byte);
  return (p < 0) ? (p / 128.0) * 90.0 : (p / 127.0) * 90.0;
}

uint8_t degrees_to_protocol_byte(int16_t clamped) {
  if (clamped < -90) clamped = -90;
  if (clamped >  90) clamped =  90;
  int16_t p = (clamped < 0)
    ? static_cast<int16_t>((clamped * 128 - 45) / 90)
    : static_cast<int16_t>((clamped * 127 + 45) / 90);
  return static_cast<uint8_t>(p & 0xFF);
}

bool parse_bool_token(const std::string & t, bool & out) {
  if (t.empty()) return false;
  std::string s = t;
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
  if (s == "true" || s == "1" || s == "yes" || s == "on")  { out = true;  return true; }
  if (s == "false"|| s == "0" || s == "no"  || s == "off") { out = false; return true; }
  return false;
}

bool parse_double_token(const std::string & t, double & out) {
  if (t.empty()) return false;
  try { std::size_t i; out = std::stod(t, &i); return i == t.size(); }
  catch (...) { return false; }
}

} // namespace

using CallbackReturn = hardware_interface::CallbackReturn;

// ─── Cache IMU (données protégées par imu_cache_mutex_) ──────────────────────
struct ImuCache {
  // Angles fusionnés
  float roll_deg   = 0.0f;
  float pitch_deg  = 0.0f;
  float yaw_deg    = 0.0f;
  uint8_t temp_c   = 0;
  // Données physiques 9 axes
  float accel_x_ms2 = 0.0f, accel_y_ms2 = 0.0f, accel_z_ms2 = 0.0f;
  float gyro_x_dps  = 0.0f, gyro_y_dps  = 0.0f, gyro_z_dps  = 0.0f;
  float mag_x_raw   = 0.0f, mag_y_raw   = 0.0f, mag_z_raw   = 0.0f;
  bool valid = false;
};

// ─── Classe principale ────────────────────────────────────────────────────────

class MutoHexapodHardware : public hardware_interface::SystemInterface {
public:
  CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & params) override;

  std::vector<hardware_interface::StateInterface>   export_state_interfaces()   override;
  std::vector<hardware_interface::CommandInterface> export_command_interfaces() override;

  CallbackReturn on_activate  (const rclcpp_lifecycle::State &) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State &) noexcept override;

  hardware_interface::return_type read (const rclcpp::Time &, const rclcpp::Duration &) override;
  hardware_interface::return_type write(const rclcpp::Time &, const rclcpp::Duration &) override;

private:
  // ── Méthodes internes ─────────────────────────────────────────────────────
  bool validate_joint_interfaces(const hardware_interface::HardwareInfo &) const;
  bool parse_servo_ids   (const std::string &, std::vector<uint8_t> &) const;
  bool parse_bool_list   (const std::string &, std::vector<bool>    &) const;
  bool parse_double_list (const std::string &, std::vector<double>  &) const;

  void release_driver() noexcept;
  // PRÉCONDITION: serial_mutex_ doit être détenu par l'appelant,
  //               OU imu_running_ doit être false (appel depuis on_activate avant start_imu_thread).
  bool read_state_from_hardware();

  // ── Thread IMU ────────────────────────────────────────────────────────────
  void imu_thread_fn();
  void start_imu_thread();
  void stop_imu_thread() noexcept;
  void publish_imu();

  // ── Logger ───────────────────────────────────────────────────────────────
  rclcpp::Logger logger_{rclcpp::get_logger("muto_hardware.MutoHexapodHardware")};

  // ── Horloge RT partagée — évite les allocations dans la boucle temps-réel ─
  // Utilisée pour RCLCPP_*_THROTTLE dans read() et write().
  // RCL_STEADY_TIME: monotone, pas de saut, idéal pour throttling.
  rclcpp::Clock rt_clock_{RCL_STEADY_TIME};

  // ── Driver (Sensor hérite de Driver: servos + IMU sur le même port) ──────
  // Sensor hérite de Driver → writeRaw(), torqueOn(), torqueOff() disponibles.
  std::unique_ptr<muto_link::Sensor> sensor_;

  // ── Paramètres ───────────────────────────────────────────────────────────
  std::string  port_{"/dev/ttyUSB0"};
  int          baud_{115200};
  double       read_timeout_sec_{0.050};
  int          retry_count_{1};
  bool         validate_checksum_{true};
  uint16_t     servo_speed_{kDefaultServoSpeed};
  bool         update_state_from_hardware_{false};
  bool         torque_enabled_{true};   // false → servos libres (calibration)
  double       imu_publish_rate_{10.0};
  std::string  imu_frame_id_{"imu_link"};

  // ── Joints ───────────────────────────────────────────────────────────────
  std::vector<uint8_t> servo_ids_;
  std::vector<bool>    servo_inversions_;
  std::vector<double>  servo_offsets_deg_;
  std::vector<double>  command_positions_;
  std::vector<double>  state_positions_;

  // ── Mutex série ───────────────────────────────────────────────────────────
  // Partagé entre le cycle RT (write/read) et le thread IMU.
  // Le port MUTO est half-duplex: une seule opération série à la fois.
  std::mutex serial_mutex_;

  // ── Thread IMU ────────────────────────────────────────────────────────────
  std::thread         imu_thread_;
  std::atomic<bool>   imu_running_{false};
  std::mutex          imu_cache_mutex_;
  ImuCache            imu_cache_;

  // ── Compteur de cycles RT manqués (mutex série occupé en mode passif) ────
  int read_missed_cycles_{0};

  // ── Node interne ROS2 pour publication IMU ────────────────────────────────
  // Note: timestamps via rclcpp::Clock(RCL_SYSTEM_TIME) — pas synchronisés
  // avec use_sim_time=true (acceptable sur hardware réel).
  rclcpp::Node::SharedPtr imu_node_;
  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr          pub_imu_;
  rclcpp::Publisher<sensor_msgs::msg::MagneticField>::SharedPtr pub_mag_;
  rclcpp::Publisher<sensor_msgs::msg::Temperature>::SharedPtr   pub_temp_;
};

// ─── on_init ──────────────────────────────────────────────────────────────────

CallbackReturn MutoHexapodHardware::on_init(
  const hardware_interface::HardwareComponentInterfaceParams & params)
{
  if (hardware_interface::SystemInterface::on_init(params) != CallbackReturn::SUCCESS)
    return CallbackReturn::ERROR;

  if (info_.joints.empty()) {
    RCLCPP_ERROR(logger_, "No joints configured"); return CallbackReturn::ERROR;
  }
  if (info_.joints.size() > 18) {
    RCLCPP_ERROR(logger_, "Max 18 servos, got %zu", info_.joints.size()); return CallbackReturn::ERROR;
  }
  if (!validate_joint_interfaces(info_)) return CallbackReturn::ERROR;

  const auto & pm = info_.hardware_parameters;
  auto get = [&](const char* k, const std::string & def) -> const std::string& {
    auto it = pm.find(k); return (it != pm.end() && !it->second.empty()) ? it->second : def;
  };
  static const std::string empty;

  const auto & pv = get("port", empty); if (!pv.empty()) port_ = pv;

  const auto & bv = get("baud", empty);
  if (!bv.empty()) { try { baud_ = std::stoi(bv); } catch(...) {} }
  if (baud_ <= 0) baud_ = 115200;

  const auto & tv = get("read_timeout_sec", empty);
  if (!tv.empty()) { try { read_timeout_sec_ = std::stod(tv); } catch(...) {} }
  if (read_timeout_sec_ <= 0.0) read_timeout_sec_ = 0.050;

  const auto & rv = get("retry_count", empty);
  if (!rv.empty()) { try { retry_count_ = std::stoi(rv); } catch(...) {} }
  if (retry_count_ < 1) retry_count_ = 1;

  {auto it = pm.find("validate_checksum"); if(it!=pm.end()) validate_checksum_ = parse_bool(it->second, true);}

  const auto & sv = get("servo_speed", empty);
  if (!sv.empty()) {
    try {
      const auto raw_speed = std::stoul(sv);
      servo_speed_ = static_cast<uint16_t>(std::min(raw_speed, (unsigned long)65535));
    } catch(...) {}
  }
  // FIX: servo_speed=0 → comportement indéfini firmware (servo bloqué ou ignoré)
  if (servo_speed_ == 0) {
    RCLCPP_WARN(logger_, "servo_speed=0 invalide (firmware MUTO), forcé à %u", kDefaultServoSpeed);
    servo_speed_ = kDefaultServoSpeed;
  }

  {auto it = pm.find("update_state_from_hardware"); if(it!=pm.end()) update_state_from_hardware_ = parse_bool(it->second, false);}

  // Torque
  {auto it = pm.find("torque_enabled"); if(it!=pm.end()) torque_enabled_ = parse_bool(it->second, true);}

  // Paramètres IMU
  const auto & ir = get("imu_publish_rate", empty);
  if (!ir.empty()) { try { imu_publish_rate_ = std::stod(ir); } catch(...) {} }
  // FIX: clamp explicite avec warning au lieu de reset silencieux à 10Hz
  if (imu_publish_rate_ <= 0.0) {
    RCLCPP_WARN(logger_, "imu_publish_rate=%.1f invalide, forcé à 10Hz", imu_publish_rate_);
    imu_publish_rate_ = 10.0;
  } else if (imu_publish_rate_ > 50.0) {
    RCLCPP_WARN(logger_, "imu_publish_rate=%.1f > 50Hz (limite lecture IMU ~26ms), clampé à 50Hz", imu_publish_rate_);
    imu_publish_rate_ = 50.0;
  }

  const auto & iframe = get("imu_frame_id", empty);
  if (!iframe.empty()) imu_frame_id_ = iframe;

  // servo_ids
  servo_ids_.clear();
  const auto & iv = get("servo_ids", empty);
  if (!iv.empty() && !parse_servo_ids(iv, servo_ids_)) servo_ids_.clear();
  if (servo_ids_.empty()) {
    for (std::size_t i = 0; i < info_.joints.size(); ++i)
      servo_ids_.push_back(static_cast<uint8_t>(i + 1));
  }
  if (servo_ids_.size() != info_.joints.size()) {
    RCLCPP_ERROR(logger_, "servo_ids size (%zu) != joints (%zu)", servo_ids_.size(), info_.joints.size());
    return CallbackReturn::ERROR;
  }
  {
    auto sorted = servo_ids_; std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
      RCLCPP_ERROR(logger_, "Duplicate servo_id"); return CallbackReturn::ERROR;
    }
  }

  // servo_inversions
  servo_inversions_.assign(info_.joints.size(), false);
  bool inv_set = false;
  {
    auto it = pm.find("servo_inversions");
    if (it != pm.end() && !it->second.empty()) {
      std::vector<bool> parsed;
      if (parse_bool_list(it->second, parsed)) {
        if      (parsed.size() == 1)                   servo_inversions_.assign(info_.joints.size(), parsed[0]);
        else if (parsed.size() == info_.joints.size())  servo_inversions_ = parsed;
        inv_set = true;
      }
    }
  }
  {
    auto it = pm.find("servo_invert_ids");
    if (it != pm.end() && !it->second.empty()) {
      std::vector<uint8_t> ids;
      if (parse_servo_ids(it->second, ids)) {
        if (inv_set) RCLCPP_WARN(logger_, "servo_invert_ids OR'ed avec servo_inversions existantes");
        for (std::size_t i = 0; i < servo_ids_.size(); ++i)
          if (std::find(ids.begin(), ids.end(), servo_ids_[i]) != ids.end())
            servo_inversions_[i] = true;
      }
    }
  }

  // servo_offsets
  servo_offsets_deg_.assign(info_.joints.size(), 0.0);
  {
    auto it = pm.find("servo_offsets");
    if (it != pm.end() && !it->second.empty()) {
      std::vector<double> parsed;
      if (parse_double_list(it->second, parsed)) {
        if      (parsed.size() == 1)                   servo_offsets_deg_.assign(info_.joints.size(), parsed[0]);
        else if (parsed.size() == info_.joints.size())  servo_offsets_deg_ = parsed;
      }
    }
  }

  command_positions_.assign(info_.joints.size(), 0.0);
  state_positions_.assign(info_.joints.size(), 0.0);

  RCLCPP_INFO(logger_,
    "Initialized: port=%s baud=%d timeout=%.3fs retries=%d speed=%u joints=%zu RT_state=%s torque=%s imu=%.0fHz frame=%s",
    port_.c_str(), baud_, read_timeout_sec_, retry_count_, servo_speed_, info_.joints.size(),
    update_state_from_hardware_ ? "hardware (≤20Hz!)" : "command (50Hz OK)",
    torque_enabled_ ? "ON" : "OFF (passif — update_rate ≤ 25Hz recommandé)",
    imu_publish_rate_, imu_frame_id_.c_str());

  if (!torque_enabled_) {
    RCLCPP_WARN(logger_,
      "torque_enabled=false: mode calibration/debug actif.\n"
      "  - write(): bloqué — aucune consigne envoyée aux servos\n"
      "  - read():  lecture hardware forcée — /joint_states reflète les angles réels\n"
      "  - Rate:    10Hz max (lecture ~26ms/cycle)\n"
      "  Déplacer les pattes à la main librement. Passer à true pour déployer la politique.");
  }

  return CallbackReturn::SUCCESS;
}

// ─── export interfaces ────────────────────────────────────────────────────────

std::vector<hardware_interface::StateInterface> MutoHexapodHardware::export_state_interfaces() {
  std::vector<hardware_interface::StateInterface> v;
  v.reserve(info_.joints.size());
  for (std::size_t i = 0; i < info_.joints.size(); ++i)
    v.emplace_back(info_.joints[i].name, hardware_interface::HW_IF_POSITION, &state_positions_[i]);
  return v;
}

std::vector<hardware_interface::CommandInterface> MutoHexapodHardware::export_command_interfaces() {
  std::vector<hardware_interface::CommandInterface> v;
  v.reserve(info_.joints.size());
  for (std::size_t i = 0; i < info_.joints.size(); ++i)
    v.emplace_back(info_.joints[i].name, hardware_interface::HW_IF_POSITION, &command_positions_[i]);
  return v;
}

// ─── release_driver ──────────────────────────────────────────────────────────

void MutoHexapodHardware::release_driver() noexcept {
  if (!sensor_) return;
  if (torque_enabled_) {
    try { sensor_->torqueOff(); } catch (const std::exception & e) {
      RCLCPP_WARN(logger_, "torqueOff: %s", e.what()); }
  }
  try { sensor_->close(); } catch (const std::exception & e) {
    RCLCPP_WARN(logger_, "close: %s", e.what()); }
  sensor_.reset();
}

// ─── read_state_from_hardware ─────────────────────────────────────────────────
//
// PRÉCONDITION: Le appelant doit soit:
//   (a) détenir serial_mutex_ (cycle RT en mode passif/debug), OU
//   (b) garantir que imu_running_==false (appel depuis on_activate avant start_imu_thread).
// Cette fonction ne prend PAS le mutex elle-même pour permettre les deux cas d'usage.
//
bool MutoHexapodHardware::read_state_from_hardware() {
  try {
    const auto raw = sensor_->readServoAngle(1);
    const uint8_t max_id = *std::max_element(servo_ids_.begin(), servo_ids_.end());
    if (raw.size() < static_cast<std::size_t>(max_id)) {
      RCLCPP_WARN(logger_, "Response too short (%zu < %u)", raw.size(), max_id);
      return false;
    }
    for (std::size_t i = 0; i < servo_ids_.size(); ++i) {
      double deg = protocol_to_degrees(raw[servo_ids_[i] - 1]) - servo_offsets_deg_[i];
      if (servo_inversions_[i]) deg = -deg;
      state_positions_[i] = deg * kDegToRad;
    }
    return true;
  } catch (const std::exception & e) {
    RCLCPP_WARN(logger_, "read_state_from_hardware: %s", e.what());
    return false;
  }
}

// ─── Thread IMU ───────────────────────────────────────────────────────────────
//
// Tourne à imu_publish_rate_ Hz (défaut 10Hz = période 100ms).
//
// Synchronisation série:
//   Acquiert serial_mutex_ (bloquant) avant chaque lecture IMU.
//   Le cycle RT (write) utilise try_to_lock → il passe son tour si l'IMU lit.
//   La lecture IMU (~52ms total: ~26ms angles + ~26ms physique) s'intercale
//   dans les intervalles libres entre cycles servo (50Hz = 20ms/cycle).
//
//   En pratique à 10Hz IMU / 50Hz RT:
//     - Le thread IMU attend jusqu'à ~20ms max pour obtenir serial_mutex_.
//     - Pendant les ~52ms de lecture IMU, le RT peut manquer 2-3 cycles write.
//     - Ces cycles manqués sont silencieux (try_to_lock retourne OK sans envoyer).
//     - Acceptable: les servos maintiennent leur dernière position.
//
void MutoHexapodHardware::imu_thread_fn() {
  const auto period_ns = std::chrono::nanoseconds(
    static_cast<long>(1e9 / imu_publish_rate_));

  RCLCPP_INFO(logger_, "IMU thread started (%.0f Hz)", imu_publish_rate_);

  while (imu_running_.load(std::memory_order_relaxed)) {
    const auto t_start = std::chrono::steady_clock::now();

    if (sensor_) {
      try {
        // Acquérir le mutex série (bloquant) — le thread IMU peut attendre
        // qu'un cycle write() libère le mutex (~2ms max).
        std::lock_guard<std::mutex> lock(serial_mutex_);

        const auto angles = sensor_->getImuAngleDegrees();   // ~26ms
        const auto phys   = sensor_->getImuPhysical();        // ~26ms

        // Mettre à jour le cache sous imu_cache_mutex_
        {
          std::lock_guard<std::mutex> cache_lock(imu_cache_mutex_);
          imu_cache_.roll_deg    = angles.roll;
          imu_cache_.pitch_deg   = angles.pitch;
          imu_cache_.yaw_deg     = angles.yaw;
          imu_cache_.temp_c      = angles.temperature_c;
          imu_cache_.accel_x_ms2 = phys.accel_x_ms2;
          imu_cache_.accel_y_ms2 = phys.accel_y_ms2;
          imu_cache_.accel_z_ms2 = phys.accel_z_ms2;
          imu_cache_.gyro_x_dps  = phys.gyro_x_dps;
          imu_cache_.gyro_y_dps  = phys.gyro_y_dps;
          imu_cache_.gyro_z_dps  = phys.gyro_z_dps;
          imu_cache_.mag_x_raw   = phys.mag_x_raw;
          imu_cache_.mag_y_raw   = phys.mag_y_raw;
          imu_cache_.mag_z_raw   = phys.mag_z_raw;
          imu_cache_.valid       = true;
        }

        // Publier depuis le thread IMU (rclcpp::Publisher::publish est thread-safe)
        publish_imu();

      } catch (const std::exception & e) {
        // FIX: utiliser rt_clock_ (membre) au lieu de Clock::make_shared() (allocation)
        RCLCPP_WARN_THROTTLE(logger_, rt_clock_, 5000,
          "IMU read error: %s", e.what());
      }
    }

    // Dormir jusqu'au prochain cycle IMU (en tenant compte du temps écoulé)
    const auto elapsed = std::chrono::steady_clock::now() - t_start;
    if (elapsed < period_ns) {
      std::this_thread::sleep_for(period_ns - elapsed);
    }
  }

  RCLCPP_INFO(logger_, "IMU thread stopped");
}

void MutoHexapodHardware::publish_imu() {
  if (!imu_node_) return;

  ImuCache cache;
  {
    std::lock_guard<std::mutex> lock(imu_cache_mutex_);
    if (!imu_cache_.valid) return;
    cache = imu_cache_;
  }

  // FIX: utiliser RCL_SYSTEM_TIME explicitement pour éviter t=0 avec use_sim_time=true.
  // Sur hardware réel, wall-clock est la valeur correcte pour les données capteurs.
  const rclcpp::Clock wall_clock(RCL_SYSTEM_TIME);
  const auto stamp = wall_clock.now();

  // ── sensor_msgs/Imu ───────────────────────────────────────────────────────
  {
    sensor_msgs::msg::Imu msg;
    msg.header.stamp    = stamp;
    msg.header.frame_id = imu_frame_id_;

    const auto q = rpy_to_quat(cache.roll_deg, cache.pitch_deg, cache.yaw_deg);
    msg.orientation.x = q.x;
    msg.orientation.y = q.y;
    msg.orientation.z = q.z;
    msg.orientation.w = q.w;
    // Covariance orientation: ~0.1° std → (0.1 * π/180)² ≈ 3e-6 rad² (REP-145)
    msg.orientation_covariance = {3e-6, 0, 0,  0, 3e-6, 0,  0, 0, 3e-6};

    // Accélération (m/s²) — déjà en m/s² depuis getImuPhysical()
    msg.linear_acceleration.x = static_cast<double>(cache.accel_x_ms2);
    msg.linear_acceleration.y = static_cast<double>(cache.accel_y_ms2);
    msg.linear_acceleration.z = static_cast<double>(cache.accel_z_ms2);
    msg.linear_acceleration_covariance = {1e-2, 0, 0,  0, 1e-2, 0,  0, 0, 1e-2};

    // Gyroscope: conversion °/s → rad/s
    msg.angular_velocity.x = static_cast<double>(cache.gyro_x_dps) * kDegToRad;
    msg.angular_velocity.y = static_cast<double>(cache.gyro_y_dps) * kDegToRad;
    msg.angular_velocity.z = static_cast<double>(cache.gyro_z_dps) * kDegToRad;
    msg.angular_velocity_covariance = {1e-4, 0, 0,  0, 1e-4, 0,  0, 0, 1e-4};

    pub_imu_->publish(msg);
  }

  // ── sensor_msgs/MagneticField ─────────────────────────────────────────────
  {
    sensor_msgs::msg::MagneticField msg;
    msg.header.stamp    = stamp;
    msg.header.frame_id = imu_frame_id_;
    // Valeurs brutes (unité capteur, facteur de conversion non documenté)
    msg.magnetic_field.x = static_cast<double>(cache.mag_x_raw);
    msg.magnetic_field.y = static_cast<double>(cache.mag_y_raw);
    msg.magnetic_field.z = static_cast<double>(cache.mag_z_raw);
    // REP-145: -1 sur la diagonale = covariance inconnue
    msg.magnetic_field_covariance = {-1, 0, 0,  0, -1, 0,  0, 0, -1};
    pub_mag_->publish(msg);
  }

  // ── sensor_msgs/Temperature ───────────────────────────────────────────────
  {
    sensor_msgs::msg::Temperature msg;
    msg.header.stamp    = stamp;
    msg.header.frame_id = imu_frame_id_;
    msg.temperature     = static_cast<double>(cache.temp_c);
    msg.variance        = 0.0;  // inconnu (REP-145: 0 = inconnu pour Temperature)
    pub_temp_->publish(msg);
  }
}

void MutoHexapodHardware::start_imu_thread() {
  // Créer le node interne ROS2 pour la publication
  rclcpp::NodeOptions opts;
  opts.automatically_declare_parameters_from_overrides(true);
  imu_node_ = rclcpp::Node::make_shared("muto_imu_publisher", opts);

  const auto qos = rclcpp::SensorDataQoS();
  pub_imu_  = imu_node_->create_publisher<sensor_msgs::msg::Imu>         ("muto/imu",      qos);
  pub_mag_  = imu_node_->create_publisher<sensor_msgs::msg::MagneticField>("muto/imu/mag",  qos);
  pub_temp_ = imu_node_->create_publisher<sensor_msgs::msg::Temperature>  ("muto/imu/temp", 10);

  RCLCPP_INFO(logger_, "IMU publishers créés: /muto/imu, /muto/imu/mag, /muto/imu/temp");

  imu_running_.store(true);
  imu_thread_ = std::thread(&MutoHexapodHardware::imu_thread_fn, this);
}

void MutoHexapodHardware::stop_imu_thread() noexcept {
  imu_running_.store(false);
  if (imu_thread_.joinable()) {
    imu_thread_.join();
  }
  pub_imu_.reset();
  pub_mag_.reset();
  pub_temp_.reset();
  imu_node_.reset();
}

// ─── on_activate ─────────────────────────────────────────────────────────────

CallbackReturn MutoHexapodHardware::on_activate(const rclcpp_lifecycle::State &) {
  // Invariant: imu_running_ doit être false ici (appelé avant start_imu_thread).
  // read_state_from_hardware() est appelé sans serial_mutex_ — safe car le
  // thread IMU n'est pas encore démarré.
  assert(!imu_running_.load() && "on_activate: thread IMU déjà actif, incohérence d'état");

  try {
    auto transport = std::make_unique<muto_link::UsbSerial>(port_, baud_, read_timeout_sec_);
    muto_link::DriverOptions opts;
    opts.read_timeout_sec  = read_timeout_sec_;
    opts.retry_count       = retry_count_;
    opts.validate_checksum = validate_checksum_;

    // Sensor hérite de Driver → toutes les méthodes servo + méthodes IMU
    sensor_ = std::make_unique<muto_link::Sensor>(std::move(transport), opts);
    sensor_->open();
    if (torque_enabled_) {
      sensor_->torqueOn();
    } else {
      // torqueOff() boucle sur IDs 1–18 avec délai inter-trame (firmware MUTO
      // exige des trames individuelles — le broadcast 0xFE est ignoré).
      sensor_->torqueOff();
      RCLCPP_WARN(logger_, "torque_enabled=false: torqueOff() envoyé servo par servo, servos libres");
    }

    // Lecture initiale HORS cycle RT — imu_running_==false ici (assertion ci-dessus)
    if (!read_state_from_hardware())
      std::fill(state_positions_.begin(), state_positions_.end(), 0.0);
    command_positions_ = state_positions_;

  } catch (const std::exception & e) {
    RCLCPP_ERROR(logger_, "Activation failed: %s", e.what());
    release_driver();
    return CallbackReturn::ERROR;
  }

  // Démarrer le thread IMU après la lecture initiale (ordre critique)
  read_missed_cycles_ = 0;
  start_imu_thread();

  RCLCPP_INFO(logger_, "Hardware activé sur %s (torque=%s, IMU @ %.0f Hz)",
    port_.c_str(), torque_enabled_ ? "ON" : "OFF", imu_publish_rate_);
  return CallbackReturn::SUCCESS;
}

// ─── on_deactivate ────────────────────────────────────────────────────────────

CallbackReturn MutoHexapodHardware::on_deactivate(const rclcpp_lifecycle::State &) noexcept {
  stop_imu_thread();
  release_driver();
  RCLCPP_INFO(logger_, "Hardware désactivé");
  return CallbackReturn::SUCCESS;
}

// ─── read (cycle RT) ──────────────────────────────────────────────────────────
//
// update_state_from_hardware=false + torque_enabled=true  (PRODUCTION 50Hz):
//   state = command — 0ms, pas d'accès série.
//
// update_state_from_hardware=false + torque_enabled=false (PASSIF ≤25Hz):
//   Lecture hardware avec try_to_lock. Si le mutex est pris par le thread IMU,
//   le cycle est sauté et les dernières valeurs sont conservées.
//   Un warning est émis tous les 10 cycles manqués consécutifs.
//
// update_state_from_hardware=true (DEBUG ≤20Hz):
//   Lecture hardware systématique avec try_to_lock.
//
hardware_interface::return_type MutoHexapodHardware::read(
  const rclcpp::Time &, const rclcpp::Duration &)
{
  if (!sensor_) {
    RCLCPP_ERROR(logger_, "read: sensor non initialisé");
    return hardware_interface::return_type::ERROR;
  }

  // Lecture hardware si: debug activé OU mode passif (angles réels requis)
  if (update_state_from_hardware_ || !torque_enabled_) {
    std::unique_lock<std::mutex> lock(serial_mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
      // FIX: réinitialiser le compteur si on obtient le mutex
      read_missed_cycles_ = 0;
      read_state_from_hardware();
    } else {
      // FIX: signaler les données stale après N cycles consécutifs
      ++read_missed_cycles_;
      if (read_missed_cycles_ % 10 == 0) {
        RCLCPP_WARN_THROTTLE(logger_, rt_clock_, 2000,
          "read: serial_mutex_ occupé (thread IMU), %d cycles consécutifs avec données stale",
          read_missed_cycles_);
      }
    }
    return hardware_interface::return_type::OK;
  }

  // Mode production: state = command, pas d'accès série.
  state_positions_ = command_positions_;
  return hardware_interface::return_type::OK;
}

// ─── write (cycle RT) ─────────────────────────────────────────────────────────
//
// torque_enabled=true  (PRODUCTION):
//   writeRaw(batch) — envoi synchrone de toutes les consignes. ~2ms, 50Hz.
//   Si serial_mutex_ est pris par le thread IMU, le cycle est sauté.
//   Les servos maintiennent leur dernière position → acceptable.
//
// torque_enabled=false (CALIBRATION):
//   write() bloqué — aucune consigne envoyée, servos libres.
//
hardware_interface::return_type MutoHexapodHardware::write(
  const rclcpp::Time &, const rclcpp::Duration &)
{
  if (!sensor_) {
    RCLCPP_ERROR(logger_, "write: sensor non initialisé");
    return hardware_interface::return_type::ERROR;
  }

  // Mode calibration: write bloqué, servos libres.
  if (!torque_enabled_) {
    return hardware_interface::return_type::OK;
  }

  std::vector<uint8_t> batch;
  batch.reserve(kServoFrameSize * servo_ids_.size());

  for (std::size_t i = 0; i < servo_ids_.size(); ++i) {
    const double cmd = command_positions_[i];
    if (!std::isfinite(cmd)) {
      // FIX: utiliser rt_clock_ membre au lieu de Clock::make_shared() (allocation RT)
      RCLCPP_WARN_THROTTLE(logger_, rt_clock_, 2000,
        "Commande non-finie pour joint %s, ignorée", info_.joints[i].name.c_str());
      continue;
    }

    double deg = cmd * kRadToDeg;
    if (servo_inversions_[i]) deg = -deg;
    deg += servo_offsets_deg_[i];

    const int16_t clamped = static_cast<int16_t>(
      std::max(-90.0, std::min(90.0, std::round(deg))));
    const uint8_t angle_byte = degrees_to_protocol_byte(clamped);
    const uint8_t id    = servo_ids_[i];
    const uint8_t spd_h = static_cast<uint8_t>((servo_speed_ >> 8) & 0xFF);
    const uint8_t spd_l = static_cast<uint8_t>( servo_speed_       & 0xFF);

    // Trame single servo (protocole MUTO §"Control a single steering gear angle"):
    //   header1(0x55) header2(0x00) length(0x0C) instr(0x01) addr(0x40)
    //   data1(id) data2(angle) data3(spd_h) data4(spd_l) check tail1(0x00) tail2(0xAA)
    // Checksum = 255 - (length + instr + addr + data1..4) % 256
    const uint8_t len   = static_cast<uint8_t>(kServoFrameSize);  // 0x0C
    const uint8_t instr = 0x01;
    const uint8_t addr  = kRegServoPosition;  // 0x40
    const uint8_t chk   = static_cast<uint8_t>(
      255 - ((len + instr + addr + id + angle_byte + spd_h + spd_l) % 256));

    batch.push_back(0x55); batch.push_back(0x00);
    batch.push_back(len);  batch.push_back(instr);
    batch.push_back(addr); batch.push_back(id);
    batch.push_back(angle_byte);
    batch.push_back(spd_h); batch.push_back(spd_l);
    batch.push_back(chk);
    batch.push_back(0x00); batch.push_back(0xAA);
  }

  if (batch.empty()) return hardware_interface::return_type::OK;

  std::unique_lock<std::mutex> lock(serial_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    // Thread IMU en cours de lecture — servos gardent leur dernière position.
    return hardware_interface::return_type::OK;
  }

  try {
    sensor_->writeRaw(batch);
  } catch (const std::exception & e) {
    // FIX: utiliser rt_clock_ membre
    RCLCPP_ERROR_THROTTLE(logger_, rt_clock_, 1000,
      "Batch write failed: %s", e.what());
    return hardware_interface::return_type::ERROR;
  }

  return hardware_interface::return_type::OK;
}

// ─── validate_joint_interfaces ───────────────────────────────────────────────

bool MutoHexapodHardware::validate_joint_interfaces(
  const hardware_interface::HardwareInfo & info) const
{
  for (const auto & j : info.joints) {
    if (j.command_interfaces.size() != 1 ||
        j.command_interfaces[0].name != hardware_interface::HW_IF_POSITION) {
      RCLCPP_ERROR(logger_, "Joint '%s': besoin d'exactement 1 interface command 'position'", j.name.c_str());
      return false;
    }
    if (j.state_interfaces.size() != 1 ||
        j.state_interfaces[0].name != hardware_interface::HW_IF_POSITION) {
      RCLCPP_ERROR(logger_, "Joint '%s': besoin d'exactement 1 interface state 'position'", j.name.c_str());
      return false;
    }
  }
  return true;
}

// ─── parse_servo_ids ─────────────────────────────────────────────────────────

bool MutoHexapodHardware::parse_servo_ids(
  const std::string & raw, std::vector<uint8_t> & out) const
{
  out.clear();
  if (raw.empty()) return false;
  std::string s = raw;
  std::replace(s.begin(), s.end(), ',', ' ');
  std::replace(s.begin(), s.end(), ';', ' ');
  std::istringstream ss(s);
  int v;
  while (ss >> v) {
    if (v < 1 || v > 18) {
      RCLCPP_ERROR(logger_, "servo_id %d hors plage [1-18]", v); out.clear(); return false;
    }
    out.push_back(static_cast<uint8_t>(v));
  }
  return !out.empty();
}

// ─── parse_bool_list ─────────────────────────────────────────────────────────

bool MutoHexapodHardware::parse_bool_list(
  const std::string & raw, std::vector<bool> & out) const
{
  out.clear();
  if (raw.empty()) return false;
  std::string s = raw;
  std::replace(s.begin(), s.end(), ',', ' ');
  std::replace(s.begin(), s.end(), ';', ' ');
  std::istringstream ss(s);
  std::string t;
  while (ss >> t) {
    bool v;
    if (!parse_bool_token(t, v)) {
      RCLCPP_ERROR(logger_, "Token bool invalide '%s'", t.c_str()); out.clear(); return false;
    }
    out.push_back(v);
  }
  return !out.empty();
}

// ─── parse_double_list ───────────────────────────────────────────────────────

bool MutoHexapodHardware::parse_double_list(
  const std::string & raw, std::vector<double> & out) const
{
  out.clear();
  if (raw.empty()) return false;
  std::string s = raw;
  std::replace(s.begin(), s.end(), ',', ' ');
  std::replace(s.begin(), s.end(), ';', ' ');
  std::istringstream ss(s);
  std::string t;
  while (ss >> t) {
    double v;
    if (!parse_double_token(t, v)) {
      RCLCPP_ERROR(logger_, "Token double invalide '%s'", t.c_str()); out.clear(); return false;
    }
    out.push_back(v);
  }
  return !out.empty();
}

}  // namespace muto_hardware

PLUGINLIB_EXPORT_CLASS(muto_hardware::MutoHexapodHardware, hardware_interface::SystemInterface)