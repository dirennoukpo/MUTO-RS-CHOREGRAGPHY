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

namespace muto_hardware {
namespace {

constexpr double kPi           = 3.14159265358979323846;
constexpr double kRadToDeg     = 180.0 / kPi;
constexpr double kDegToRad     = kPi / 180.0;
constexpr uint16_t kDefaultServoSpeed = 300;   // vitesse par défaut (0 = max absolu, dangereux)

// ─── Helpers ──────────────────────────────────────────────────────────────────

bool parse_bool(const std::string & value, bool default_value) {
  if (value.empty()) { return default_value; }
  std::string lowered = value;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lowered == "true"  || lowered == "1" || lowered == "yes" || lowered == "on")  { return true; }
  if (lowered == "false" || lowered == "0" || lowered == "no"  || lowered == "off") { return false; }
  return default_value;
}

// Décodage du byte d'angle protocole → degrés (même logique que Driver::readServoAngleDeg)
double protocol_to_degrees(uint8_t angle_byte) {
  const int16_t protocol_angle = (angle_byte > 127)
    ? static_cast<int16_t>(angle_byte) - 256
    : static_cast<int16_t>(angle_byte);
  return (protocol_angle < 0)
    ? (static_cast<double>(protocol_angle) / 128.0) * 90.0
    : (static_cast<double>(protocol_angle) / 127.0) * 90.0;
}

bool parse_bool_token(const std::string & token, bool & out_value) {
  if (token.empty()) {
    return false;
  }
  std::string lowered = token;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") {
    out_value = true;
    return true;
  }
  if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") {
    out_value = false;
    return true;
  }
  return false;
}

bool parse_double_token(const std::string & token, double & out_value) {
  if (token.empty()) {
    return false;
  }
  try {
    std::size_t idx = 0;
    out_value = std::stod(token, &idx);
    return idx == token.size();
  } catch (const std::exception &) {
    return false;
  }
}

}  // namespace

using CallbackReturn = hardware_interface::CallbackReturn;

// ─── Classe principale ────────────────────────────────────────────────────────

class MutoHexapodHardware : public hardware_interface::SystemInterface {
public:
  CallbackReturn on_init(
    const hardware_interface::HardwareComponentInterfaceParams & params) override;

  std::vector<hardware_interface::StateInterface>   export_state_interfaces()   override;
  std::vector<hardware_interface::CommandInterface> export_command_interfaces() override;

  CallbackReturn on_activate  (const rclcpp_lifecycle::State & previous_state) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State & previous_state) noexcept override;

  hardware_interface::return_type read (const rclcpp::Time & time, const rclcpp::Duration & period) override;
  hardware_interface::return_type write(const rclcpp::Time & time, const rclcpp::Duration & period) override;

private:
  // FIX : séparation validation joints / validation IDs
  bool validate_joint_interfaces(const hardware_interface::HardwareInfo & info) const;
  bool parse_servo_ids(const std::string & raw, std::vector<uint8_t> & out_ids) const;
  bool parse_bool_list(const std::string & raw, std::vector<bool> & out_values) const;
  bool parse_double_list(const std::string & raw, std::vector<double> & out_values) const;

  // Libère le driver proprement (torqueOff best-effort puis close)
  void release_driver() noexcept;

  rclcpp::Logger logger_{rclcpp::get_logger("muto_hardware.MutoHexapodHardware")};
  std::unique_ptr<muto_link::Driver> driver_;

  std::string  port_{"/dev/ttyUSB0"};
  int          baud_{115200};
  double       read_timeout_sec_{0.05};
  int          retry_count_{1};
  bool         validate_checksum_{true};
  // FIX : servo_speed_ initialisé à kDefaultServoSpeed, pas 0 (0 = vitesse max non contrôlée)
  uint16_t     servo_speed_{kDefaultServoSpeed};

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
  if (hardware_interface::SystemInterface::on_init(params) != CallbackReturn::SUCCESS) {
    return CallbackReturn::ERROR;
  }

  // FIX : vérifier joints.empty() AVANT validate_joint_interfaces pour éviter
  //       de masquer une config vide avec un "succès" de la boucle vide
  if (info_.joints.empty()) {
    RCLCPP_ERROR(logger_, "No joints configured in hardware info");
    return CallbackReturn::ERROR;
  }

  if (info_.joints.size() > 18) {
    RCLCPP_ERROR(logger_, "Hardware supports up to 18 servos, got %zu", info_.joints.size());
    return CallbackReturn::ERROR;
  }

  if (!validate_joint_interfaces(info_)) {
    return CallbackReturn::ERROR;
  }

  const auto & params_map = info_.hardware_parameters;

  // --- port ---
  const auto port_it = params_map.find("port");
  if (port_it != params_map.end() && !port_it->second.empty()) {
    port_ = port_it->second;
  }

  // --- baud ---
  const auto baud_it = params_map.find("baud");
  if (baud_it != params_map.end() && !baud_it->second.empty()) {
    try {
      baud_ = std::stoi(baud_it->second);
    } catch (const std::exception &) {
      RCLCPP_WARN(logger_, "Invalid baud value '%s', keeping default %d",
        baud_it->second.c_str(), baud_);
    }
  }
  if (baud_ <= 0) {
    RCLCPP_WARN(logger_, "baud must be > 0, resetting to 115200");
    baud_ = 115200;
  }

  // --- read_timeout_sec ---
  const auto timeout_it = params_map.find("read_timeout_sec");
  if (timeout_it != params_map.end() && !timeout_it->second.empty()) {
    try {
      read_timeout_sec_ = std::stod(timeout_it->second);
    } catch (const std::exception &) {
      RCLCPP_WARN(logger_, "Invalid read_timeout_sec '%s', keeping default %.3f",
        timeout_it->second.c_str(), read_timeout_sec_);
    }
  }
  if (read_timeout_sec_ <= 0.0) {
    RCLCPP_WARN(logger_, "read_timeout_sec must be > 0, resetting to 0.05");
    read_timeout_sec_ = 0.05;
  }

  // --- retry_count ---
  const auto retry_it = params_map.find("retry_count");
  if (retry_it != params_map.end() && !retry_it->second.empty()) {
    try {
      retry_count_ = std::stoi(retry_it->second);
    } catch (const std::exception &) {
      RCLCPP_WARN(logger_, "Invalid retry_count '%s', keeping default %d",
        retry_it->second.c_str(), retry_count_);
    }
  }
  if (retry_count_ < 1) {
    RCLCPP_WARN(logger_, "retry_count must be >= 1, resetting to 1");
    retry_count_ = 1;
  }

  // --- validate_checksum ---
  const auto checksum_it = params_map.find("validate_checksum");
  if (checksum_it != params_map.end()) {
    validate_checksum_ = parse_bool(checksum_it->second, validate_checksum_);
  }

  // --- servo_speed ---
  const auto speed_it = params_map.find("servo_speed");
  if (speed_it != params_map.end() && !speed_it->second.empty()) {
    try {
      const unsigned long value = std::stoul(speed_it->second);
      servo_speed_ = static_cast<uint16_t>(
        std::min<unsigned long>(value, std::numeric_limits<uint16_t>::max()));
    } catch (const std::exception &) {
      RCLCPP_WARN(logger_, "Invalid servo_speed '%s', keeping default %u",
        speed_it->second.c_str(), servo_speed_);
    }
  }

  // --- servo_ids ---
  servo_ids_.clear();
  const auto ids_it = params_map.find("servo_ids");
  if (ids_it != params_map.end() && !ids_it->second.empty()) {
    if (!parse_servo_ids(ids_it->second, servo_ids_)) {
      RCLCPP_WARN(logger_, "Invalid servo_ids '%s', using sequential IDs",
        ids_it->second.c_str());
      servo_ids_.clear();
    }
  }

  // IDs séquentiels par défaut (1-based)
  if (servo_ids_.empty()) {
    servo_ids_.reserve(info_.joints.size());
    for (std::size_t i = 0; i < info_.joints.size(); ++i) {
      servo_ids_.push_back(static_cast<uint8_t>(i + 1));
    }
  }

  if (servo_ids_.size() != info_.joints.size()) {
    RCLCPP_ERROR(logger_, "servo_ids size (%zu) must match joints size (%zu)",
      servo_ids_.size(), info_.joints.size());
    return CallbackReturn::ERROR;
  }

  // Détection de doublons
  {
    auto sorted_ids = servo_ids_;
    std::sort(sorted_ids.begin(), sorted_ids.end());
    const auto dup = std::adjacent_find(sorted_ids.begin(), sorted_ids.end());
    if (dup != sorted_ids.end()) {
      RCLCPP_ERROR(logger_, "servo_ids contains duplicate value %u", *dup);
      return CallbackReturn::ERROR;
    }
  }

  // --- servo_inversions (optionnel) ---
  servo_inversions_.assign(info_.joints.size(), false);
  bool inversions_set = false;
  const auto inversions_it = params_map.find("servo_inversions");
  if (inversions_it != params_map.end() && !inversions_it->second.empty()) {
    std::vector<bool> parsed;
    if (parse_bool_list(inversions_it->second, parsed)) {
      if (parsed.size() == 1) {
        servo_inversions_.assign(info_.joints.size(), parsed.front());
      } else if (parsed.size() == info_.joints.size()) {
        servo_inversions_ = parsed;
      } else {
        RCLCPP_WARN(logger_, "servo_inversions size (%zu) must be 1 or %zu, ignoring",
          parsed.size(), info_.joints.size());
      }
      inversions_set = true;
    } else {
      RCLCPP_WARN(logger_, "Invalid servo_inversions '%s', ignoring",
        inversions_it->second.c_str());
    }
  }

  // --- servo_invert_ids (optionnel) ---
  const auto invert_ids_it = params_map.find("servo_invert_ids");
  if (invert_ids_it != params_map.end() && !invert_ids_it->second.empty()) {
    std::vector<uint8_t> invert_ids;
    if (parse_servo_ids(invert_ids_it->second, invert_ids)) {
      if (inversions_set) {
        RCLCPP_WARN(logger_,
          "servo_inversions already set; servo_invert_ids will be OR'ed with existing flags");
      }
      for (std::size_t i = 0; i < servo_ids_.size(); ++i) {
        if (std::find(invert_ids.begin(), invert_ids.end(), servo_ids_[i]) != invert_ids.end()) {
          servo_inversions_[i] = true;
        }
      }
    } else {
      RCLCPP_WARN(logger_, "Invalid servo_invert_ids '%s', ignoring",
        invert_ids_it->second.c_str());
    }
  }

  // --- servo_offsets (optionnel, en degres) ---
  servo_offsets_deg_.assign(info_.joints.size(), 0.0);
  const auto offsets_it = params_map.find("servo_offsets");
  if (offsets_it != params_map.end() && !offsets_it->second.empty()) {
    std::vector<double> parsed;
    if (parse_double_list(offsets_it->second, parsed)) {
      if (parsed.size() == 1) {
        servo_offsets_deg_.assign(info_.joints.size(), parsed.front());
      } else if (parsed.size() == info_.joints.size()) {
        servo_offsets_deg_ = parsed;
      } else {
        RCLCPP_WARN(logger_, "servo_offsets size (%zu) must be 1 or %zu, ignoring",
          parsed.size(), info_.joints.size());
      }
    } else {
      RCLCPP_WARN(logger_, "Invalid servo_offsets '%s', ignoring",
        offsets_it->second.c_str());
    }
  }

  command_positions_.assign(info_.joints.size(), 0.0);
  state_positions_.assign(info_.joints.size(), 0.0);

  RCLCPP_INFO(logger_, "Initialized: port=%s baud=%d timeout=%.3fs retries=%d speed=%u joints=%zu",
    port_.c_str(), baud_, read_timeout_sec_, retry_count_, servo_speed_, info_.joints.size());

  return CallbackReturn::SUCCESS;
}

// ─── export interfaces ────────────────────────────────────────────────────────

std::vector<hardware_interface::StateInterface> MutoHexapodHardware::export_state_interfaces() {
  std::vector<hardware_interface::StateInterface> interfaces;
  interfaces.reserve(info_.joints.size());
  for (std::size_t i = 0; i < info_.joints.size(); ++i) {
    interfaces.emplace_back(
      info_.joints[i].name, hardware_interface::HW_IF_POSITION, &state_positions_[i]);
  }
  return interfaces;
}

std::vector<hardware_interface::CommandInterface> MutoHexapodHardware::export_command_interfaces() {
  std::vector<hardware_interface::CommandInterface> interfaces;
  interfaces.reserve(info_.joints.size());
  for (std::size_t i = 0; i < info_.joints.size(); ++i) {
    interfaces.emplace_back(
      info_.joints[i].name, hardware_interface::HW_IF_POSITION, &command_positions_[i]);
  }
  return interfaces;
}

// ─── release_driver (RAII helper) ────────────────────────────────────────────

// FIX : logique de libération centralisée — torqueOff best-effort puis close,
//       peu importe d'où on l'appelle (on_deactivate, erreur d'activation, etc.)
void MutoHexapodHardware::release_driver() noexcept {
  if (!driver_) { return; }
  try { driver_->torqueOff(); } catch (const std::exception & e) {
    RCLCPP_WARN(logger_, "torqueOff failed during release: %s", e.what());
  }
  try { driver_->close(); } catch (const std::exception & e) {
    RCLCPP_WARN(logger_, "close failed during release: %s", e.what());
  }
  driver_.reset();
}

// ─── on_activate ─────────────────────────────────────────────────────────────

CallbackReturn MutoHexapodHardware::on_activate(const rclcpp_lifecycle::State & /*previous_state*/) {
  try {
    auto transport = std::make_unique<muto_link::UsbSerial>(port_, baud_, read_timeout_sec_);
    muto_link::DriverOptions options;
    options.read_timeout_sec  = read_timeout_sec_;
    options.retry_count       = retry_count_;
    options.validate_checksum = validate_checksum_;
    driver_ = std::make_unique<muto_link::Driver>(std::move(transport), options);

    driver_->open();
    driver_->torqueOn();

    // Lecture initiale pour pré-remplir state_positions_ et éviter un saut
    // au premier cycle de contrôle
    if (read(rclcpp::Time(0), rclcpp::Duration(0, 0)) != hardware_interface::return_type::OK) {
      RCLCPP_WARN(logger_, "Initial read failed — state initialized to zero");
    } else {
      command_positions_ = state_positions_;
    }
  } catch (const std::exception & e) {
    RCLCPP_ERROR(logger_, "Activation failed: %s", e.what());
    // FIX : utiliser release_driver pour garantir le nettoyage même si open()
    //       a réussi mais torqueOn() a échoué
    release_driver();
    return CallbackReturn::ERROR;
  }

  RCLCPP_INFO(logger_, "Hardware activated on %s", port_.c_str());
  return CallbackReturn::SUCCESS;
}

// ─── on_deactivate ────────────────────────────────────────────────────────────

CallbackReturn MutoHexapodHardware::on_deactivate(
  const rclcpp_lifecycle::State & /*previous_state*/) noexcept {
  // FIX : release_driver() est noexcept et centralise torqueOff + close.
  //       On ne retourne jamais ERROR ici pour ne pas bloquer le lifecycle.
  release_driver();
  RCLCPP_INFO(logger_, "Hardware deactivated");
  return CallbackReturn::SUCCESS;
}

// ─── read ─────────────────────────────────────────────────────────────────────

hardware_interface::return_type MutoHexapodHardware::read(
  const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/)
{
  if (!driver_) {
    RCLCPP_ERROR(logger_, "read called but driver is not initialized");
    return hardware_interface::return_type::ERROR;
  }

  try {
    // Le protocole retourne toujours 18 bytes (un par servo, indexé 0-17).
    // On passe servo_id=1 car la réponse est toujours le tableau complet.
    const auto raw = driver_->readServoAngle(1);

    // FIX : vérification explicite que la réponse couvre tous nos servo_ids_
    const uint8_t max_id = *std::max_element(servo_ids_.begin(), servo_ids_.end());
    if (raw.size() < static_cast<std::size_t>(max_id)) {
      RCLCPP_ERROR(logger_,
        "Servo angle response too small (%zu bytes) for max servo_id=%u",
        raw.size(), max_id);
      return hardware_interface::return_type::ERROR;
    }

    for (std::size_t i = 0; i < servo_ids_.size(); ++i) {
      const uint8_t servo_id = servo_ids_[i];
      const double angle_deg = protocol_to_degrees(
        raw[static_cast<std::size_t>(servo_id - 1)]);
      double ros_deg = angle_deg - servo_offsets_deg_[i];
      if (servo_inversions_[i]) {
        ros_deg = -ros_deg;
      }
      state_positions_[i] = ros_deg * kDegToRad;
    }
  } catch (const std::exception & e) {
    RCLCPP_ERROR(logger_, "Read failed: %s", e.what());
    return hardware_interface::return_type::ERROR;
  }

  return hardware_interface::return_type::OK;
}

// ─── write ────────────────────────────────────────────────────────────────────

hardware_interface::return_type MutoHexapodHardware::write(
  const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/)
{
  if (!driver_) {
    RCLCPP_ERROR(logger_, "write called but driver is not initialized");
    return hardware_interface::return_type::ERROR;
  }

  // FIX : isoler les erreurs par servo — une exception sur un seul servo
  //       ne doit pas empêcher les autres servos d'être mis à jour.
  //       On propage quand même ERROR à la fin si au moins un servo a échoué.
  bool any_error = false;

  for (std::size_t i = 0; i < servo_ids_.size(); ++i) {
    const uint8_t servo_id    = servo_ids_[i];
    const double  command_rad = command_positions_[i];

    if (!std::isfinite(command_rad)) {
      RCLCPP_WARN(logger_, "Joint %s command is not finite, skipping",
        info_.joints[i].name.c_str());
      continue;
    }

    double angle_deg = command_rad * kRadToDeg;
    if (servo_inversions_[i]) {
      angle_deg = -angle_deg;
    }
    angle_deg += servo_offsets_deg_[i];
    const int16_t angle_cmd = static_cast<int16_t>(std::lround(angle_deg));

    try {
      driver_->servoMove(servo_id, angle_cmd, servo_speed_);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(logger_, "servoMove failed for joint %s (id=%u): %s",
        info_.joints[i].name.c_str(), servo_id, e.what());
      any_error = true;
      // On continue pour les autres servos
    }
  }

  return any_error
    ? hardware_interface::return_type::ERROR
    : hardware_interface::return_type::OK;
}

// ─── validate_joint_interfaces ───────────────────────────────────────────────

bool MutoHexapodHardware::validate_joint_interfaces(
  const hardware_interface::HardwareInfo & info) const
{
  for (const auto & joint : info.joints) {
    if (joint.command_interfaces.size() != 1) {
      RCLCPP_ERROR(logger_, "Joint '%s': expected 1 command interface, got %zu",
        joint.name.c_str(), joint.command_interfaces.size());
      return false;
    }
    if (joint.command_interfaces[0].name != hardware_interface::HW_IF_POSITION) {
      RCLCPP_ERROR(logger_, "Joint '%s': command interface is '%s', expected '%s'",
        joint.name.c_str(), joint.command_interfaces[0].name.c_str(),
        hardware_interface::HW_IF_POSITION);
      return false;
    }
    if (joint.state_interfaces.size() != 1) {
      RCLCPP_ERROR(logger_, "Joint '%s': expected 1 state interface, got %zu",
        joint.name.c_str(), joint.state_interfaces.size());
      return false;
    }
    if (joint.state_interfaces[0].name != hardware_interface::HW_IF_POSITION) {
      RCLCPP_ERROR(logger_, "Joint '%s': state interface is '%s', expected '%s'",
        joint.name.c_str(), joint.state_interfaces[0].name.c_str(),
        hardware_interface::HW_IF_POSITION);
      return false;
    }
  }
  return true;
}

// ─── parse_servo_ids ─────────────────────────────────────────────────────────

bool MutoHexapodHardware::parse_servo_ids(
  const std::string & raw, std::vector<uint8_t> & out_ids) const
{
  out_ids.clear();
  if (raw.empty()) { return false; }

  std::string normalized = raw;
  std::replace(normalized.begin(), normalized.end(), ',', ' ');
  std::replace(normalized.begin(), normalized.end(), ';', ' ');

  std::istringstream stream(normalized);
  int value = 0;
  while (stream >> value) {
    if (value < 1 || value > 18) {
      RCLCPP_ERROR(logger_, "servo_id %d out of range [1-18]", value);
      out_ids.clear();
      return false;
    }
    out_ids.push_back(static_cast<uint8_t>(value));
  }

  // FIX : détecter les caractères invalides résiduels dans le stream
  if (!stream.eof()) {
    std::string leftover;
    stream >> leftover;
    if (!leftover.empty()) {
      RCLCPP_ERROR(logger_, "servo_ids contains invalid token '%s'", leftover.c_str());
      out_ids.clear();
      return false;
    }
  }

  return !out_ids.empty();
}

// ─── parse_bool_list ────────────────────────────────────────────────────────

bool MutoHexapodHardware::parse_bool_list(
  const std::string & raw, std::vector<bool> & out_values) const
{
  out_values.clear();
  if (raw.empty()) { return false; }

  std::string normalized = raw;
  std::replace(normalized.begin(), normalized.end(), ',', ' ');
  std::replace(normalized.begin(), normalized.end(), ';', ' ');

  std::istringstream stream(normalized);
  std::string token;
  while (stream >> token) {
    bool value = false;
    if (!parse_bool_token(token, value)) {
      RCLCPP_ERROR(logger_, "servo_inversions contains invalid token '%s'", token.c_str());
      out_values.clear();
      return false;
    }
    out_values.push_back(value);
  }

  return !out_values.empty();
}

// ─── parse_double_list ──────────────────────────────────────────────────────

bool MutoHexapodHardware::parse_double_list(
  const std::string & raw, std::vector<double> & out_values) const
{
  out_values.clear();
  if (raw.empty()) { return false; }

  std::string normalized = raw;
  std::replace(normalized.begin(), normalized.end(), ',', ' ');
  std::replace(normalized.begin(), normalized.end(), ';', ' ');

  std::istringstream stream(normalized);
  std::string token;
  while (stream >> token) {
    double value = 0.0;
    if (!parse_double_token(token, value)) {
      RCLCPP_ERROR(logger_, "servo_offsets contains invalid token '%s'", token.c_str());
      out_values.clear();
      return false;
    }
    out_values.push_back(value);
  }

  return !out_values.empty();
}

}  // namespace muto_hardware

PLUGINLIB_EXPORT_CLASS(muto_hardware::MutoHexapodHardware, hardware_interface::SystemInterface)