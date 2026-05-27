#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "hardware_interface/handle.hpp"
#include "hardware_interface/system_interface.hpp"
#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "pluginlib/class_list_macros.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/state.hpp"

#include "muto_link/driver.hpp"
#include "muto_link/errors.hpp"
#include "muto_link/transport.hpp"

// ═══════════════════════════════════════════════════════════════════════════════
// ANALYSE DE L'OVERRUN — CAUSES RACINES ET CORRECTIONS
// ═══════════════════════════════════════════════════════════════════════════════
//
// Mesures logs (50Hz = budget 20ms par cycle):
//   Read  : ~26ms  (turnaround firmware MUTO ~23ms, incompressible)
//   Write : ~16ms  (18 × servoMove() = 18 × tcdrain() sur USB-serial)
//   Total : ~42ms  → overrun systématique de 3 cycles
//
// CAUSE #1 — Write: tcdrain() répété 18 fois
//   Chaque servoMove() appelle transport_->write() puis tcdrain().
//   Sur adaptateur USB-serial (CH340 / FTDI / CP210x), tcdrain() est
//   retenu par le latency timer USB (1-4ms). Résultat: 18 × ~1ms = ~18ms
//   alors que la TX série réelle de 18 trames de 12 bytes = 1.88ms.
//
//   FIX: accumuler les 18 trames en mémoire → 1 seul writeRaw() → 1 tcdrain().
//   Résultat: write passe de ~16ms à ~2ms. (14ms gagnés)
//
// CAUSE #2 — Read: turnaround firmware ~23ms (hardware fixe, non négociable)
//   La carte MUTO met ~23ms entre la réception de la requête et l'envoi
//   de la réponse. Réduire le timeout ROS2 ou le baud rate ne change rien.
//
//   FIX: supprimer la lecture série du cycle RT.
//   Pour un hexapode à servos hobby (STS, SCS, etc.), les servos n'ont pas
//   d'encodeur absolu indépendant — le feedback série retourne la position
//   cible interne du servo, pas sa position mécanique réelle. Le contrôleur
//   ROS2 (JointGroupPositionController) n'utilise pas ce feedback pour la
//   commande. state = command est une approximation correcte.
//   Résultat: read passe de ~26ms à ~0ms. (26ms gagnés)
//
// RÉSULTAT: cycle RT ~2ms → 50Hz largement atteignable (budget 20ms).
//
// PARAMÈTRE update_state_from_hardware (xacro, défaut: false):
//   false → state = command (production, 50Hz)
//   true  → lecture série dans le RT (debug uniquement, impose ≤ 20Hz)
//
// ═══════════════════════════════════════════════════════════════════════════════

namespace muto_hardware {
namespace {

constexpr double   kPi               = 3.14159265358979323846;
constexpr double   kRadToDeg         = 180.0 / kPi;
constexpr double   kDegToRad         = kPi / 180.0;
constexpr uint16_t kDefaultServoSpeed = 300;

// Taille fixe d'une trame servoMove: header(2)+LEN(1)+INSTR(1)+ADDR(1)+data(4)+CHK(1)+tail(2) = 12
constexpr std::size_t kServoFrameSize = 12;

// Registres protocole MUTO (dupliqués ici pour éviter de dépendre de l'accès à driver_ privé)
constexpr uint8_t kRegServoPosition = 0x40;

// ─── Helpers ──────────────────────────────────────────────────────────────────

bool parse_bool(const std::string & value, bool default_value) {
  if (value.empty()) { return default_value; }
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

// Conversion symétrique de driver.cpp (servoMove)
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
  bool validate_joint_interfaces(const hardware_interface::HardwareInfo &) const;
  bool parse_servo_ids   (const std::string &, std::vector<uint8_t> &) const;
  bool parse_bool_list   (const std::string &, std::vector<bool>    &) const;
  bool parse_double_list (const std::string &, std::vector<double>  &) const;

  void release_driver() noexcept;
  bool read_state_from_hardware();   // Hors cycle RT uniquement

  rclcpp::Logger logger_{rclcpp::get_logger("muto_hardware.MutoHexapodHardware")};
  std::unique_ptr<muto_link::Driver> driver_;

  std::string  port_{"/dev/ttyUSB0"};
  int          baud_{115200};
  double       read_timeout_sec_{0.050};
  int          retry_count_{1};
  bool         validate_checksum_{true};
  uint16_t     servo_speed_{kDefaultServoSpeed};
  bool         update_state_from_hardware_{false};

  std::vector<uint8_t> servo_ids_;
  std::vector<bool>    servo_inversions_;
  std::vector<double>  servo_offsets_deg_;
  std::vector<double>  command_positions_;
  std::vector<double>  state_positions_;
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

  // port
  const auto & pv = get("port", empty); if (!pv.empty()) port_ = pv;

  // baud
  const auto & bv = get("baud", empty);
  if (!bv.empty()) { try { baud_ = std::stoi(bv); } catch(...) {} }
  if (baud_ <= 0) baud_ = 115200;

  // read_timeout_sec
  const auto & tv = get("read_timeout_sec", empty);
  if (!tv.empty()) { try { read_timeout_sec_ = std::stod(tv); } catch(...) {} }
  if (read_timeout_sec_ <= 0.0) read_timeout_sec_ = 0.050;

  // retry_count
  const auto & rv = get("retry_count", empty);
  if (!rv.empty()) { try { retry_count_ = std::stoi(rv); } catch(...) {} }
  if (retry_count_ < 1) retry_count_ = 1;

  // validate_checksum
  {auto it = pm.find("validate_checksum"); if(it!=pm.end()) validate_checksum_ = parse_bool(it->second, true);}

  // servo_speed
  const auto & sv = get("servo_speed", empty);
  if (!sv.empty()) {
    try { servo_speed_ = static_cast<uint16_t>(std::min(std::stoul(sv), (unsigned long)65535)); }
    catch(...) {}
  }

  // update_state_from_hardware (false par défaut = mode production sans lecture RT)
  {auto it = pm.find("update_state_from_hardware"); if(it!=pm.end()) update_state_from_hardware_ = parse_bool(it->second, false);}

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
    auto dup = std::adjacent_find(sorted.begin(), sorted.end());
    if (dup != sorted.end()) {
      RCLCPP_ERROR(logger_, "Duplicate servo_id %u", *dup); return CallbackReturn::ERROR;
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
        if      (parsed.size() == 1)                servo_inversions_.assign(info_.joints.size(), parsed[0]);
        else if (parsed.size() == info_.joints.size()) servo_inversions_ = parsed;
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
        if      (parsed.size() == 1)                  servo_offsets_deg_.assign(info_.joints.size(), parsed[0]);
        else if (parsed.size() == info_.joints.size()) servo_offsets_deg_ = parsed;
      }
    }
  }

  command_positions_.assign(info_.joints.size(), 0.0);
  state_positions_.assign(info_.joints.size(), 0.0);

  RCLCPP_INFO(logger_,
    "Initialized: port=%s baud=%d timeout=%.3fs retries=%d speed=%u joints=%zu RT_state=%s",
    port_.c_str(), baud_, read_timeout_sec_, retry_count_, servo_speed_, info_.joints.size(),
    update_state_from_hardware_ ? "hardware (≤20Hz!)" : "command (50Hz OK)");

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
  if (!driver_) return;
  try { driver_->torqueOff(); } catch (const std::exception & e) {
    RCLCPP_WARN(logger_, "torqueOff during release: %s", e.what()); }
  try { driver_->close(); } catch (const std::exception & e) {
    RCLCPP_WARN(logger_, "close during release: %s", e.what()); }
  driver_.reset();
}

// ─── read_state_from_hardware ─────────────────────────────────────────────────

bool MutoHexapodHardware::read_state_from_hardware() {
  try {
    const auto raw = driver_->readServoAngle(1);
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

// ─── on_activate ─────────────────────────────────────────────────────────────

CallbackReturn MutoHexapodHardware::on_activate(const rclcpp_lifecycle::State &) {
  try {
    auto transport = std::make_unique<muto_link::UsbSerial>(port_, baud_, read_timeout_sec_);
    muto_link::DriverOptions opts;
    opts.read_timeout_sec  = read_timeout_sec_;
    opts.retry_count       = retry_count_;
    opts.validate_checksum = validate_checksum_;
    driver_ = std::make_unique<muto_link::Driver>(std::move(transport), opts);
    driver_->open();
    driver_->torqueOn();

    // Lecture initiale HORS cycle RT: pas de contrainte temporelle
    if (!read_state_from_hardware())
      std::fill(state_positions_.begin(), state_positions_.end(), 0.0);
    command_positions_ = state_positions_;

  } catch (const std::exception & e) {
    RCLCPP_ERROR(logger_, "Activation failed: %s", e.what());
    release_driver();
    return CallbackReturn::ERROR;
  }
  RCLCPP_INFO(logger_, "Hardware activated on %s", port_.c_str());
  return CallbackReturn::SUCCESS;
}

// ─── on_deactivate ────────────────────────────────────────────────────────────

CallbackReturn MutoHexapodHardware::on_deactivate(const rclcpp_lifecycle::State &) noexcept {
  release_driver();
  RCLCPP_INFO(logger_, "Hardware deactivated");
  return CallbackReturn::SUCCESS;
}

// ─── read (cycle RT) ──────────────────────────────────────────────────────────
//
// Par défaut (update_state_from_hardware=false): state = command → 0ms.
// Mode debug (=true): lecture série bloquante ~26ms → update_rate ≤ 20Hz obligatoire.
//
hardware_interface::return_type MutoHexapodHardware::read(
  const rclcpp::Time &, const rclcpp::Duration &)
{
  if (!driver_) {
    RCLCPP_ERROR(logger_, "read: driver not initialized");
    return hardware_interface::return_type::ERROR;
  }

  if (!update_state_from_hardware_) {
    // Mode production: state suit command sans latence série
    state_positions_ = command_positions_;
    return hardware_interface::return_type::OK;
  }

  // Mode debug: lecture série (ne pas utiliser à >20Hz)
  read_state_from_hardware();   // échec non-fatal: on garde les dernières valeurs
  return hardware_interface::return_type::OK;
}

// ─── write (cycle RT) ─────────────────────────────────────────────────────────
//
// FIX: toutes les trames servoMove sont construites dans un buffer unique,
// puis envoyées en 1 seul writeRaw() → 1 seul tcdrain() côté OS/USB.
//
// Avant: 18 × tcdrain() ≈ 16ms   Après: 1 × tcdrain() ≈ 2ms
//
hardware_interface::return_type MutoHexapodHardware::write(
  const rclcpp::Time &, const rclcpp::Duration &)
{
  if (!driver_) {
    RCLCPP_ERROR(logger_, "write: driver not initialized");
    return hardware_interface::return_type::ERROR;
  }

  // ── Batch: construction de toutes les trames en mémoire ───────────────────
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
    const uint8_t id   = servo_ids_[i];
    const uint8_t spd_h = static_cast<uint8_t>((servo_speed_ >> 8) & 0xFF);
    const uint8_t spd_l = static_cast<uint8_t>( servo_speed_       & 0xFF);

    // Checksum: 255 - ((LEN + INSTR + ADDR + ID + ANGLE + SPD_H + SPD_L) % 256)
    const uint8_t len   = static_cast<uint8_t>(kServoFrameSize);  // 12
    const uint8_t instr = 0x01;  // Write
    const uint8_t addr  = kRegServoPosition;  // 0x40
    const uint8_t chk   = static_cast<uint8_t>(
      255 - ((len + instr + addr + id + angle_byte + spd_h + spd_l) % 256));

    // Trame complète (12 bytes)
    batch.push_back(0x55);       // header1
    batch.push_back(0x00);       // header2
    batch.push_back(len);
    batch.push_back(instr);
    batch.push_back(addr);
    batch.push_back(id);
    batch.push_back(angle_byte);
    batch.push_back(spd_h);
    batch.push_back(spd_l);
    batch.push_back(chk);
    batch.push_back(0x00);       // tail1
    batch.push_back(0xAA);       // tail2
  }

  if (batch.empty()) return hardware_interface::return_type::OK;

  // ── Envoi batch: 1 seul write() + 1 seul tcdrain() ───────────────────────
  try {
    driver_->writeRaw(batch);
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