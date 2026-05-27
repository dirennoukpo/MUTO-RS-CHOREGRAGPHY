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
//   │    Copie dans imu_cache_ (protégé par imu_mutex_)           │
//   │    Relâche serial_mutex_                                     │
//   ├─────────────────────────────────────────────────────────────┤
//   │  Node interne ROS2 (même processus)                         │
//   │    Timer 10Hz → lit imu_cache_ → publie                     │
//   │    Topics: /muto/imu, /muto/imu/raw, /muto/imu/mag         │
//   └─────────────────────────────────────────────────────────────┘
//
// Paramètres xacro nouveaux:
//   imu_publish_rate  (défaut: 10.0) Hz de publication IMU
//   imu_frame_id      (défaut: "imu_link") frame TF de l'IMU
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

// ─── Cache IMU (données protégées par imu_mutex_) ─────────────────────────────
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
  bool read_state_from_hardware();

  // ── Thread IMU ────────────────────────────────────────────────────────────
  void imu_thread_fn();
  void start_imu_thread();
  void stop_imu_thread() noexcept;
  void publish_imu();

  // ── Thread torqueOff continu (actif uniquement si torque_enabled_=false) ──
  // Envoie torqueOff() en permanence pour contrer la réactivation implicite
  // du couple par les trames position (reg 0x40) sur les servos STS/SCS.
  void torque_off_thread_fn();
  void start_torque_off_thread();
  void stop_torque_off_thread() noexcept;

  // ── Logger ───────────────────────────────────────────────────────────────
  rclcpp::Logger logger_{rclcpp::get_logger("muto_hardware.MutoHexapodHardware")};

  // ── Driver (Sensor hérite de Driver: servos + IMU sur le même port) ──────
  // Sensor hérite de Driver → writeRaw(), torqueOn(), torqueOff() sont disponibles.
  std::unique_ptr<muto_link::Sensor> sensor_;

  // ── Paramètres ───────────────────────────────────────────────────────────
  std::string  port_{"/dev/ttyUSB0"};
  int          baud_{115200};
  double       read_timeout_sec_{0.050};
  int          retry_count_{1};
  bool         validate_checksum_{true};
  uint16_t     servo_speed_{kDefaultServoSpeed};
  bool         update_state_from_hardware_{false};
  bool         torque_enabled_{true};   // false → torqueOn/Off jamais appelés
  double       imu_publish_rate_{10.0};
  std::string  imu_frame_id_{"imu_link"};

  // ── Joints ───────────────────────────────────────────────────────────────
  std::vector<uint8_t> servo_ids_;
  std::vector<bool>    servo_inversions_;
  std::vector<double>  servo_offsets_deg_;
  std::vector<double>  command_positions_;
  std::vector<double>  state_positions_;

  // ── Mutex série: partagé entre cycle RT et thread IMU ────────────────────
  // Le cycle RT (write) et le thread IMU (read IMU) ne peuvent pas accéder
  // au port série simultanément. serial_mutex_ protège tous les accès.
  std::mutex serial_mutex_;

  // ── Thread IMU ────────────────────────────────────────────────────────────
  std::thread         imu_thread_;
  std::atomic<bool>   imu_running_{false};
  std::mutex          imu_cache_mutex_;
  ImuCache            imu_cache_;

  // ── Thread torqueOff continu ──────────────────────────────────────────────
  std::thread       torque_off_thread_;
  std::atomic<bool> torque_off_running_{false};

  // ── Node interne ROS2 pour publication IMU ────────────────────────────────
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
    try { servo_speed_ = static_cast<uint16_t>(std::min(std::stoul(sv), (unsigned long)65535)); }
    catch(...) {}
  }

  {auto it = pm.find("update_state_from_hardware"); if(it!=pm.end()) update_state_from_hardware_ = parse_bool(it->second, false);}

  // Torque
  {auto it = pm.find("torque_enabled"); if(it!=pm.end()) torque_enabled_ = parse_bool(it->second, true);}

  // Paramètres IMU
  const auto & ir = get("imu_publish_rate", empty);
  if (!ir.empty()) { try { imu_publish_rate_ = std::stod(ir); } catch(...) {} }
  if (imu_publish_rate_ <= 0.0 || imu_publish_rate_ > 50.0) imu_publish_rate_ = 10.0;

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
        if (inv_set) RCLCPP_WARN(logger_, "servo_invert_ids OR'ed with existing servo_inversions");
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
    torque_enabled_ ? "ON" : "OFF (passif)",
    imu_publish_rate_, imu_frame_id_.c_str());

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

bool MutoHexapodHardware::read_state_from_hardware() {
  // Appelé hors cycle RT uniquement (on_activate) → pas de serial_mutex_ ici
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
// Attend serial_mutex_ avant chaque lecture pour ne pas entrer en collision
// avec write() du cycle RT. Si le cycle RT est en cours, il attend
// qu'il libère le mutex (max 2ms), puis lit l'IMU (~52ms pour les deux
// lectures), puis libère le mutex.
//
// Impact sur le cycle RT: quasi nul car le thread IMU ne tient le mutex
// que pendant les ~52ms de lecture IMU, qui tombent dans les ~18ms libres
// entre deux cycles servo (50Hz = 20ms, servo write = 2ms → 18ms libres).
// En pratique le thread IMU s'intercale dans les intervalles libres.
//
void MutoHexapodHardware::imu_thread_fn() {
  const auto period_ns = std::chrono::nanoseconds(
    static_cast<long>(1e9 / imu_publish_rate_));

  RCLCPP_INFO(logger_, "IMU thread started (%.0f Hz)", imu_publish_rate_);

  while (imu_running_.load(std::memory_order_relaxed)) {
    const auto t_start = std::chrono::steady_clock::now();

    if (sensor_) {
      try {
        // Acquérir le mutex série pour éviter la collision avec write()
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

        // Publier depuis le thread IMU (les publishers sont thread-safe dans rclcpp)
        publish_imu();

      } catch (const std::exception & e) {
        RCLCPP_WARN_THROTTLE(logger_, *rclcpp::Clock::make_shared(), 5000,
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

  const auto stamp = imu_node_->now();

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
    // Covariance orientation (~0.1° std → 0.1 * π/180 ≈ 1.7e-3 rad std → var ≈ 3e-6)
    msg.orientation_covariance = {3e-6, 0, 0,  0, 3e-6, 0,  0, 0, 3e-6};

    // Accélération (m/s²) — scale facteur kAngleScale non applicable ici, déjà en m/s²
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
    // Valeurs brutes (unité capteur) — pas de facteur de conversion connu
    msg.magnetic_field.x = static_cast<double>(cache.mag_x_raw);
    msg.magnetic_field.y = static_cast<double>(cache.mag_y_raw);
    msg.magnetic_field.z = static_cast<double>(cache.mag_z_raw);
    msg.magnetic_field_covariance = {-1, 0, 0,  0, -1, 0,  0, 0, -1}; // -1 = inconnu
    pub_mag_->publish(msg);
  }

  // ── sensor_msgs/Temperature ───────────────────────────────────────────────
  {
    sensor_msgs::msg::Temperature msg;
    msg.header.stamp    = stamp;
    msg.header.frame_id = imu_frame_id_;
    msg.temperature     = static_cast<double>(cache.temp_c);
    msg.variance        = 0.0;  // inconnu
    pub_temp_->publish(msg);
  }
}

// ─── Thread torqueOff continu ─────────────────────────────────────────────────
//
// Problème hardware STS/SCS: écrire en registre 0x40 (position) réactive
// le couple implicitement, même après torqueOff(). À 50Hz, le cycle RT
// envoie une trame position toutes les 20ms → le couple est réactivé 50x/s.
//
// Solution: ce thread tourne à ~100Hz et envoie torqueOff() en permanence
// via try_lock sur serial_mutex_. Il est plus rapide que le cycle RT (20ms
// vs ~10ms) donc il coupe le couple entre chaque trame position.
//
// Actif uniquement quand torque_enabled_=false.
//
void MutoHexapodHardware::torque_off_thread_fn() {
  RCLCPP_INFO(logger_, "torqueOff thread started (100 Hz)");

  while (torque_off_running_.load(std::memory_order_relaxed)) {
    if (sensor_) {
      // try_lock non-bloquant: si le cycle RT ou le thread IMU tient le mutex,
      // on saute ce cycle (~10ms plus tard on réessaie). Pas de deadlock possible.
      std::unique_lock<std::mutex> lock(serial_mutex_, std::try_to_lock);
      if (lock.owns_lock()) {
        try {
          sensor_->torqueOff();
        } catch (const std::exception & e) {
          RCLCPP_WARN_THROTTLE(logger_, *rclcpp::Clock::make_shared(), 5000,
            "torqueOff thread error: %s", e.what());
        }
      }
    }
    // ~100Hz → intervalle 10ms. Suffisant pour couper le couple entre deux
    // trames position du cycle RT (20ms) et les lectures IMU (50-100ms).
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  RCLCPP_INFO(logger_, "torqueOff thread stopped");
}

void MutoHexapodHardware::start_torque_off_thread() {
  torque_off_running_.store(true);
  torque_off_thread_ = std::thread(&MutoHexapodHardware::torque_off_thread_fn, this);
}

void MutoHexapodHardware::stop_torque_off_thread() noexcept {
  torque_off_running_.store(false);
  if (torque_off_thread_.joinable()) {
    torque_off_thread_.join();
  }
}

void MutoHexapodHardware::start_imu_thread() {
  // Créer le node interne ROS2 pour la publication
  rclcpp::NodeOptions opts;
  opts.automatically_declare_parameters_from_overrides(true);
  imu_node_ = rclcpp::Node::make_shared("muto_imu_publisher", opts);

  const auto qos = rclcpp::SensorDataQoS();
  pub_imu_  = imu_node_->create_publisher<sensor_msgs::msg::Imu>        ("muto/imu",     qos);
  pub_mag_  = imu_node_->create_publisher<sensor_msgs::msg::MagneticField>("muto/imu/mag", qos);
  pub_temp_ = imu_node_->create_publisher<sensor_msgs::msg::Temperature>  ("muto/imu/temp", 10);

  RCLCPP_INFO(logger_, "IMU publishers created: /muto/imu, /muto/imu/mag, /muto/imu/temp");

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
      // Les servos mémorisent leur dernier état (couple ON par défaut).
      // Il faut appeler torqueOff() explicitement pour forcer le relâchement.
      sensor_->torqueOff();
      RCLCPP_WARN(logger_, "torque_enabled=false: torqueOff() envoyé, servos libres (aucun couple)");
    }

    // Lecture initiale HORS cycle RT
    if (!read_state_from_hardware())
      std::fill(state_positions_.begin(), state_positions_.end(), 0.0);
    command_positions_ = state_positions_;

  } catch (const std::exception & e) {
    RCLCPP_ERROR(logger_, "Activation failed: %s", e.what());
    release_driver();
    return CallbackReturn::ERROR;
  }

  // Démarrer le thread IMU après l'activation du driver
  start_imu_thread();

  // Si torque désactivé, démarrer le thread torqueOff continu
  if (!torque_enabled_) {
    start_torque_off_thread();
  }

  RCLCPP_INFO(logger_, "Hardware activated on %s (torque=%s, IMU @ %.0f Hz)",
    port_.c_str(), torque_enabled_ ? "ON" : "OFF", imu_publish_rate_);
  return CallbackReturn::SUCCESS;
}

// ─── on_deactivate ────────────────────────────────────────────────────────────

CallbackReturn MutoHexapodHardware::on_deactivate(const rclcpp_lifecycle::State &) noexcept {
  // Arrêter le thread torqueOff EN PREMIER (libère serial_mutex_ immédiatement)
  stop_torque_off_thread();
  // Arrêter le thread IMU ensuite
  stop_imu_thread();
  release_driver();
  RCLCPP_INFO(logger_, "Hardware deactivated");
  return CallbackReturn::SUCCESS;
}

// ─── read (cycle RT) ──────────────────────────────────────────────────────────

hardware_interface::return_type MutoHexapodHardware::read(
  const rclcpp::Time &, const rclcpp::Duration &)
{
  if (!sensor_) {
    RCLCPP_ERROR(logger_, "read: sensor not initialized");
    return hardware_interface::return_type::ERROR;
  }

  if (!update_state_from_hardware_) {
    state_positions_ = command_positions_;
    return hardware_interface::return_type::OK;
  }

  // Mode debug: lecture série des servos (bloquant ~26ms, ≤20Hz obligatoire)
  {
    std::unique_lock<std::mutex> lock(serial_mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
      read_state_from_hardware();
    }
    // Si le thread IMU tient le mutex, on garde les dernières valeurs connues
  }
  return hardware_interface::return_type::OK;
}

// ─── write (cycle RT) ─────────────────────────────────────────────────────────
//
// Utilise try_lock (non-bloquant) sur serial_mutex_.
//
// Si le thread IMU tient le mutex (lecture ~52ms en cours), write() saute
// l'envoi ce cycle plutôt que de bloquer et provoquer un overrun.
// Les servos hobby tolèrent sans problème 1 cycle manqué à 50Hz (20ms).
//
// Si le mutex est libre (cas normal), il est acquis immédiatement et le
// batch est envoyé en ~2ms, bien dans le budget de 20ms.
//
hardware_interface::return_type MutoHexapodHardware::write(
  const rclcpp::Time &, const rclcpp::Duration &)
{
  if (!sensor_) {
    RCLCPP_ERROR(logger_, "write: sensor not initialized");
    return hardware_interface::return_type::ERROR;
  }

  // Mode passif: aucune trame envoyée.
  // Le firmware de la carte MUTO réactive le torque dès réception d'une
  // commande 0x40 (position). Seul torqueOff() répété par le thread dédié
  // maintient les servos libres. write() doit rester silencieux.
  if (!torque_enabled_) {
    return hardware_interface::return_type::OK;
  }

  std::vector<uint8_t> batch;
  batch.reserve(kServoFrameSize * servo_ids_.size());

  for (std::size_t i = 0; i < servo_ids_.size(); ++i) {
    const double cmd = command_positions_[i];
    if (!std::isfinite(cmd)) {
      RCLCPP_WARN_THROTTLE(logger_, *rclcpp::Clock::make_shared(), 2000,
        "Non-finite command for joint %s, skipping", info_.joints[i].name.c_str());
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

    const uint8_t len   = static_cast<uint8_t>(kServoFrameSize);
    const uint8_t instr = 0x01;
    const uint8_t addr  = kRegServoPosition;
    const uint8_t chk   = static_cast<uint8_t>(
      255 - ((len + instr + addr + id + angle_byte + spd_h + spd_l) % 256));

    batch.push_back(0x55);
    batch.push_back(0x00);
    batch.push_back(len);
    batch.push_back(instr);
    batch.push_back(addr);
    batch.push_back(id);
    batch.push_back(angle_byte);
    batch.push_back(spd_h);
    batch.push_back(spd_l);
    batch.push_back(chk);
    batch.push_back(0x00);
    batch.push_back(0xAA);
  }

  if (batch.empty()) return hardware_interface::return_type::OK;

  // try_lock: non-bloquant. Si le thread IMU tient le mutex, on saute ce
  // cycle sans overrun. Les servos hobby (STS/SCS) retiennent leur dernière
  // position → 1 cycle manqué à 50Hz est imperceptible mécaniquement.
  std::unique_lock<std::mutex> lock(serial_mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    // IMU en cours de lecture — cycle sauté, pas d'overrun
    return hardware_interface::return_type::OK;
  }

  try {
    sensor_->writeRaw(batch);
  } catch (const std::exception & e) {
    RCLCPP_ERROR_THROTTLE(logger_, *rclcpp::Clock::make_shared(), 1000,
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
      RCLCPP_ERROR(logger_, "Joint '%s': need exactly 1 'position' command interface", j.name.c_str());
      return false;
    }
    if (j.state_interfaces.size() != 1 ||
        j.state_interfaces[0].name != hardware_interface::HW_IF_POSITION) {
      RCLCPP_ERROR(logger_, "Joint '%s': need exactly 1 'position' state interface", j.name.c_str());
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
      RCLCPP_ERROR(logger_, "servo_id %d out of [1-18]", v); out.clear(); return false;
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
      RCLCPP_ERROR(logger_, "Invalid bool token '%s'", t.c_str()); out.clear(); return false;
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
      RCLCPP_ERROR(logger_, "Invalid double token '%s'", t.c_str()); out.clear(); return false;
    }
    out.push_back(v);
  }
  return !out.empty();
}

}  // namespace muto_hardware

PLUGINLIB_EXPORT_CLASS(muto_hardware::MutoHexapodHardware, hardware_interface::SystemInterface)