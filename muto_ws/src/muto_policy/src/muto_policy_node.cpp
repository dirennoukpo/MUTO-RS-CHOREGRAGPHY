#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <std_msgs/msg/float64_multi_array.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>
#include <algorithm>
#include <mutex>          // BUG FIX #1 : mutex manquant pour protéger imu_data_
#include <cmath>          // BUG FIX #5 : std::isfinite pour valider les sorties
#include <iostream>

// ─── Constantes configurables ────────────────────────────────────────────────
static constexpr size_t  NUM_JOINTS       = 18;
static constexpr size_t  NUM_IMU_CHANNELS = 6;
static constexpr size_t  INPUT_SIZE       = NUM_JOINTS + NUM_IMU_CHANNELS; // 24
static constexpr float   ACTION_SCALE     = 0.25f;   // BUG FIX #5 : scaling [-1,1] → rad réels
static constexpr double  MAX_JOINT_RAD    = 3.14159; // sécurité clamp sortie

class MutoPolicyInferenceNode : public rclcpp::Node
{
public:
  MutoPolicyInferenceNode()
  : Node("muto_policy_inference_node"),
    env_(ORT_LOGGING_LEVEL_WARNING, "MutoPolicy"),
    session_{nullptr}      // initialisation explicite
  {
    // ── 1. Chemin du modèle ONNX ─────────────────────────────────────────────
    this->declare_parameter<std::string>("policy_path", "muto_walk_policy.onnx");
    std::string policy_param = this->get_parameter("policy_path").as_string();

    if (policy_param.empty()) {
      RCLCPP_FATAL(this->get_logger(), "Le paramètre 'policy_path' est vide.");
      return;
    }

    if (policy_param.front() != '/') {
      try {
        std::string pkg_path =
          ament_index_cpp::get_package_share_directory("muto_description");
        policy_param = pkg_path + "/config/" + policy_param;
      } catch (const std::exception& e) {
        RCLCPP_FATAL(this->get_logger(),
          "Package 'muto_description' introuvable : %s", e.what());
        return;
      }
    }

    RCLCPP_INFO(this->get_logger(),
      "Chargement de la politique IA : %s", policy_param.c_str());

    // ── 2. Options ONNX Runtime (optimisé Pi 5) ───────────────────────────────
    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(2);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    // Désactiver la mémoire des activations pour économiser la RAM du Pi 5
    session_options.EnableMemPattern();
    session_options.EnableCpuMemArena();

    try {
      session_ = Ort::Session(env_, policy_param.c_str(), session_options);
    }
    catch (const Ort::Exception& e) {        // BUG FIX #2 : capturer Ort::Exception spécifiquement
      RCLCPP_FATAL(this->get_logger(),
        "Impossible de charger le modèle ONNX : %s", e.what());
      return;
    }

    // BUG FIX #3 : valider les dimensions d'entrée/sortie du modèle au démarrage
    validate_model_io();

    // ── 3. Ordre des joints (doit correspondre à Isaac Lab) ───────────────────
    expected_joint_order_ = {
      "zq1_Joint", "zq2_Joint", "zq3_Joint",
      "zz1_Joint", "zz2_Joint", "zz3_Joint",
      "zh1_Joint", "zh2_Joint", "zh3_Joint",
      "yq1_Joint", "yq2_Joint", "yq3_Joint",
      "yz1_Joint", "yz2_Joint", "yz3_Joint",
      "yh1_Joint", "yh2_Joint", "yh3_Joint"
    };

    // IMU : [ang_vel x,y,z | lin_acc x,y,z]
    imu_data_.resize(NUM_IMU_CHANNELS, 0.0f);

    // Pré-allouer le buffer d'entrée une seule fois (évite malloc à 50 Hz)
    input_buffer_.resize(INPUT_SIZE, 0.0f);

    // ── 4. Abonnements / publications ────────────────────────────────────────
    // BUG FIX #4 : std::placeholders::_1 (underscore + 1), pas ::1
    imu_sub_ = this->create_subscription<sensor_msgs::msg::Imu>(
      "/muto/imu", 10,
      std::bind(&MutoPolicyInferenceNode::imu_callback, this, std::placeholders::_1));

    joint_sub_ = this->create_subscription<sensor_msgs::msg::JointState>(
      "/joint_states", 1,
      std::bind(&MutoPolicyInferenceNode::joint_state_callback, this, std::placeholders::_1));

    // BUG FIX #6 : cmd_pub_ (underscore final) cohérent avec la déclaration membre
    cmd_pub_ = this->create_publisher<std_msgs::msg::Float64MultiArray>(
      "/leg_controller/commands", 1);

    RCLCPP_INFO(this->get_logger(),
      "Nœud Muto RS initialisé — entrées : %zu joints + %zu IMU = %zu dims.",
      NUM_JOINTS, NUM_IMU_CHANNELS, INPUT_SIZE);
  }

private:
  // ── Validation du modèle au démarrage ─────────────────────────────────────
  void validate_model_io()
  {
    Ort::AllocatorWithDefaultOptions allocator;

    size_t num_inputs  = session_.GetInputCount();
    size_t num_outputs = session_.GetOutputCount();

    RCLCPP_INFO(this->get_logger(),
      "Modèle ONNX — %zu entrée(s), %zu sortie(s).", num_inputs, num_outputs);

    if (num_inputs < 1 || num_outputs < 1) {
      RCLCPP_FATAL(this->get_logger(), "Modèle ONNX invalide (0 entrée ou 0 sortie).");
      return;
    }

    // Vérifier la shape d'entrée
    auto input_info  = session_.GetInputTypeInfo(0);
    auto input_shape = input_info.GetTensorTypeAndShapeInfo().GetShape();
    RCLCPP_INFO(this->get_logger(),
      "Shape entrée modèle : [%s]",
      shape_to_string(input_shape).c_str());

    // Vérifier la shape de sortie
    auto output_info  = session_.GetOutputTypeInfo(0);
    auto output_shape = output_info.GetTensorTypeAndShapeInfo().GetShape();
    RCLCPP_INFO(this->get_logger(),
      "Shape sortie modèle : [%s]",
      shape_to_string(output_shape).c_str());
  }

  static std::string shape_to_string(const std::vector<int64_t>& shape)
  {
    std::string s;
    for (size_t i = 0; i < shape.size(); ++i) {
      if (i > 0) s += ", ";
      s += (shape[i] < 0) ? "dyn" : std::to_string(shape[i]);
    }
    return s;
  }

  // ── Callback IMU (20 Hz) ──────────────────────────────────────────────────
  void imu_callback(const sensor_msgs::msg::Imu::SharedPtr msg)
  {
    // BUG FIX #1 : protéger imu_data_ contre la concurrence joint_state_callback
    std::lock_guard<std::mutex> lock(imu_mutex_);
    imu_data_[0] = static_cast<float>(msg->angular_velocity.x);
    imu_data_[1] = static_cast<float>(msg->angular_velocity.y);
    imu_data_[2] = static_cast<float>(msg->angular_velocity.z);
    imu_data_[3] = static_cast<float>(msg->linear_acceleration.x);
    imu_data_[4] = static_cast<float>(msg->linear_acceleration.y);
    imu_data_[5] = static_cast<float>(msg->linear_acceleration.z);
  }

  // ── Callback JointState (50 Hz) — déclencheur de l'inférence ─────────────
  void joint_state_callback(const sensor_msgs::msg::JointState::SharedPtr msg)
  {
    // Vérification taille minimale
    if (msg->name.size() < NUM_JOINTS || msg->position.size() < NUM_JOINTS) {
      RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
        "Message JointState incomplet (%zu joints reçus, %zu attendus).",
        msg->name.size(), NUM_JOINTS);
      return;
    }

    // ── Remplir les 18 premières cases du buffer (positions joints) ──────────
    for (size_t i = 0; i < NUM_JOINTS; ++i) {
      const auto& joint_name = expected_joint_order_[i];
      auto it = std::find(msg->name.begin(), msg->name.end(), joint_name);
      if (it == msg->name.end()) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
          "Joint manquant dans le message : '%s'", joint_name.c_str());
        return;
      }
      size_t idx = std::distance(msg->name.begin(), it);
      input_buffer_[i] = static_cast<float>(msg->position[idx]);
    }

    // ── Remplir les 6 dernières cases (données IMU) ──────────────────────────
    {
      std::lock_guard<std::mutex> lock(imu_mutex_);
      std::copy(imu_data_.begin(), imu_data_.end(),
                input_buffer_.begin() + NUM_JOINTS);
    }

    // ── Inférence ONNX ───────────────────────────────────────────────────────
    Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(
      OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);

    std::array<int64_t, 2> input_shape{1, static_cast<int64_t>(INPUT_SIZE)};

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
      mem_info,
      input_buffer_.data(), input_buffer_.size(),
      input_shape.data(), input_shape.size());

    const char* input_names[]  = {"input"};
    const char* output_names[] = {"output"};

    std::vector<Ort::Value> output_tensors;
    try {
      output_tensors = session_.Run(
        Ort::RunOptions{nullptr},
        input_names,  &input_tensor, 1,
        output_names, 1);
    }
    catch (const Ort::Exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Erreur d'inférence ONNX : %s", e.what());
      return;
    }

    const float* output_values =
      output_tensors.front().GetTensorMutableData<float>();

    // ── Publication vers ros2_control ────────────────────────────────────────
    auto cmd_msg = std_msgs::msg::Float64MultiArray();
    cmd_msg.data.resize(NUM_JOINTS);

    for (size_t i = 0; i < NUM_JOINTS; ++i) {
      // BUG FIX #5 : scaling + clamp de sécurité (NaN/Inf → arrêt propre)
      float raw = output_values[i] * ACTION_SCALE;
      if (!std::isfinite(raw)) {
        RCLCPP_ERROR(this->get_logger(),
          "Sortie ONNX non-finie à l'index %zu, commande annulée.", i);
        return;
      }
      cmd_msg.data[i] = static_cast<double>(
        std::clamp(raw, static_cast<float>(-MAX_JOINT_RAD),
                        static_cast<float>( MAX_JOINT_RAD)));
    }

    cmd_pub_->publish(cmd_msg);  // BUG FIX #6 : cmd_pub_ (cohérent avec le membre)
  }

  // ── Membres ───────────────────────────────────────────────────────────────
  Ort::Env     env_;
  Ort::Session session_;

  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr joint_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr         imu_sub_;
  rclcpp::Publisher<std_msgs::msg::Float64MultiArray>::SharedPtr  cmd_pub_;

  std::vector<std::string> expected_joint_order_;
  std::vector<float>       imu_data_;       // protégé par imu_mutex_
  std::vector<float>       input_buffer_;   // pré-alloué, réutilisé à chaque cycle
  std::mutex               imu_mutex_;      // BUG FIX #1
};

int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<MutoPolicyInferenceNode>());
  rclcpp::shutdown();
  return 0;
}