#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <std_msgs/msg/float64_multi_array.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>
#include <algorithm>
#include <mutex>
#include <cmath>
#include <iostream>
#include <fstream>

// ─── Constantes configurables ────────────────────────────────────────────────
static constexpr size_t  NUM_JOINTS       = 18;
static constexpr size_t  NUM_IMU_CHANNELS = 6;
static constexpr size_t  INPUT_SIZE       = NUM_JOINTS + NUM_IMU_CHANNELS; // 24
static constexpr float   ACTION_SCALE     = 0.25f;
static constexpr double  MAX_JOINT_RAD    = 3.14159;

class MutoPolicyInferenceNode : public rclcpp::Node
{
public:
  MutoPolicyInferenceNode()
  : Node("muto_policy_inference_node"),
    env_(ORT_LOGGING_LEVEL_WARNING, "MutoPolicy"),
    session_{nullptr},
    model_loaded_(false)
  {
    // ── 1. Chemin du modèle ONNX ─────────────────────────────────────────────
    this->declare_parameter<std::string>("policy_path", "muto_walk_policy.onnx");
    std::string policy_param = this->get_parameter("policy_path").as_string();

    RCLCPP_INFO(this->get_logger(), "policy_path reçu : '%s'", policy_param.c_str());

    if (policy_param.empty()) {
      RCLCPP_FATAL(this->get_logger(), "Le paramètre 'policy_path' est vide !");
      return;
    }

    // Résolution du chemin relatif → absolu
    if (policy_param.front() != '/') {
      try {
        std::string pkg_path =
          ament_index_cpp::get_package_share_directory("muto_description");
        policy_param = pkg_path + "/config/" + policy_param;
        RCLCPP_INFO(this->get_logger(), "Chemin absolu résolu : %s", policy_param.c_str());
      } catch (const std::exception& e) {
        RCLCPP_FATAL(this->get_logger(),
          "Package 'muto_description' introuvable : %s", e.what());
        return;
      }
    }

    // Vérifier que le fichier existe AVANT de charger ONNX
    // (évite un crash silencieux sans message utile)
    {
      std::ifstream f(policy_param);
      if (!f.good()) {
        RCLCPP_FATAL(this->get_logger(),
          "Fichier ONNX introuvable : '%s'\n"
          "  → Copiez votre modèle dans :\n"
          "    install/muto_description/share/muto_description/config/\n"
          "  → Ou passez un chemin absolu : policy_path:=/chemin/complet/modele.onnx",
          policy_param.c_str());
        return;
      }
    }

    RCLCPP_INFO(this->get_logger(), "Chargement ONNX : %s", policy_param.c_str());

    // ── 2. Options ONNX Runtime ───────────────────────────────────────────────
    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(2);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    session_options.EnableMemPattern();
    session_options.EnableCpuMemArena();

    try {
      session_ = Ort::Session(env_, policy_param.c_str(), session_options);
    }
    catch (const Ort::Exception& e) {
      RCLCPP_FATAL(this->get_logger(),
        "Impossible de charger le modèle ONNX : %s", e.what());
      return;
    }

    // ── 3. Lire les vrais noms des tenseurs (FIX CRITIQUE) ────────────────────
    // Ne pas hardcoder "input"/"output" : ça dépend du modèle exporté.
    if (!resolve_tensor_names()) {
      return;  // FATAL déjà loggé dans la fonction
    }

    // ── 4. Valider les dimensions ─────────────────────────────────────────────
    validate_model_io();

    // ── 5. Ordre des joints (doit correspondre à Isaac Lab) ───────────────────
    expected_joint_order_ = {
      "zq1_Joint", "zq2_Joint", "zq3_Joint",
      "zz1_Joint", "zz2_Joint", "zz3_Joint",
      "zh1_Joint", "zh2_Joint", "zh3_Joint",
      "yq1_Joint", "yq2_Joint", "yq3_Joint",
      "yz1_Joint", "yz2_Joint", "yz3_Joint",
      "yh1_Joint", "yh2_Joint", "yh3_Joint"
    };

    imu_data_.resize(NUM_IMU_CHANNELS, 0.0f);
    input_buffer_.resize(INPUT_SIZE, 0.0f);

    // ── 6. Abonnements / publications ────────────────────────────────────────
    imu_sub_ = this->create_subscription<sensor_msgs::msg::Imu>(
      "/muto/imu", 10,
      std::bind(&MutoPolicyInferenceNode::imu_callback, this, std::placeholders::_1));

    joint_sub_ = this->create_subscription<sensor_msgs::msg::JointState>(
      "/joint_states", 1,
      std::bind(&MutoPolicyInferenceNode::joint_state_callback, this, std::placeholders::_1));

    cmd_pub_ = this->create_publisher<std_msgs::msg::Float64MultiArray>(
      "/leg_controller/commands", 1);

    model_loaded_ = true;
    RCLCPP_INFO(this->get_logger(),
      "✓ Nœud Muto RS prêt — %zu joints + %zu IMU → %zu dims | "
      "tenseur entrée: '%s' | tenseur sortie: '%s'",
      NUM_JOINTS, NUM_IMU_CHANNELS, INPUT_SIZE,
      input_name_.c_str(), output_name_.c_str());
  }

private:
  // ── Résolution dynamique des noms de tenseurs ─────────────────────────────
  // FIX CRITIQUE : "input"/"output" sont des conventions, pas des garanties.
  // Le vrai nom dépend du framework d'export (Isaac Lab, PyTorch, etc.)
  bool resolve_tensor_names()
  {
    Ort::AllocatorWithDefaultOptions allocator;

    size_t num_inputs  = session_.GetInputCount();
    size_t num_outputs = session_.GetOutputCount();

    if (num_inputs < 1) {
      RCLCPP_FATAL(this->get_logger(), "Le modèle ONNX n'a aucune entrée !");
      return false;
    }
    if (num_outputs < 1) {
      RCLCPP_FATAL(this->get_logger(), "Le modèle ONNX n'a aucune sortie !");
      return false;
    }

    // Lire le vrai nom du premier tenseur d'entrée
    auto input_name_ptr  = session_.GetInputNameAllocated(0, allocator);
    auto output_name_ptr = session_.GetOutputNameAllocated(0, allocator);
    input_name_  = std::string(input_name_ptr.get());
    output_name_ = std::string(output_name_ptr.get());

    // Logger tous les noms disponibles (utile pour debug)
    RCLCPP_INFO(this->get_logger(),
      "Modèle ONNX : %zu entrée(s), %zu sortie(s)", num_inputs, num_outputs);
    for (size_t i = 0; i < num_inputs; ++i) {
      auto n = session_.GetInputNameAllocated(i, allocator);
      auto info = session_.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
      RCLCPP_INFO(this->get_logger(),
        "  Entrée[%zu] : '%s'  shape=[%s]", i, n.get(), shape_to_string(info).c_str());
    }
    for (size_t i = 0; i < num_outputs; ++i) {
      auto n = session_.GetOutputNameAllocated(i, allocator);
      auto info = session_.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
      RCLCPP_INFO(this->get_logger(),
        "  Sortie[%zu] : '%s'  shape=[%s]", i, n.get(), shape_to_string(info).c_str());
    }

    return true;
  }

  // ── Validation des dimensions d'entrée/sortie ────────────────────────────
  void validate_model_io()
  {
    auto input_shape = session_.GetInputTypeInfo(0)
                               .GetTensorTypeAndShapeInfo().GetShape();
    auto output_shape = session_.GetOutputTypeInfo(0)
                                .GetTensorTypeAndShapeInfo().GetShape();

    // L'entrée doit avoir une dim finale == INPUT_SIZE (ou -1 = dynamique)
    int64_t expected_in = static_cast<int64_t>(INPUT_SIZE);
    if (!input_shape.empty() && input_shape.back() != -1 &&
        input_shape.back() != expected_in) {
      RCLCPP_WARN(this->get_logger(),
        "ATTENTION : le modèle attend %ld valeurs en entrée, "
        "mais INPUT_SIZE = %zu. Ajustez la constante dans le code !",
        input_shape.back(), INPUT_SIZE);
    }

    // La sortie doit avoir une dim finale == NUM_JOINTS (ou -1 = dynamique)
    int64_t expected_out = static_cast<int64_t>(NUM_JOINTS);
    if (!output_shape.empty() && output_shape.back() != -1 &&
        output_shape.back() != expected_out) {
      RCLCPP_WARN(this->get_logger(),
        "ATTENTION : le modèle produit %ld valeurs en sortie, "
        "mais NUM_JOINTS = %zu. Vérifiez la correspondance !",
        output_shape.back(), NUM_JOINTS);
    }
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

  // ── Callback IMU ─────────────────────────────────────────────────────────
  void imu_callback(const sensor_msgs::msg::Imu::SharedPtr msg)
  {
    std::lock_guard<std::mutex> lock(imu_mutex_);
    imu_data_[0] = static_cast<float>(msg->angular_velocity.x);
    imu_data_[1] = static_cast<float>(msg->angular_velocity.y);
    imu_data_[2] = static_cast<float>(msg->angular_velocity.z);
    imu_data_[3] = static_cast<float>(msg->linear_acceleration.x);
    imu_data_[4] = static_cast<float>(msg->linear_acceleration.y);
    imu_data_[5] = static_cast<float>(msg->linear_acceleration.z);
  }

  // ── Callback JointState — déclencheur de l'inférence ─────────────────────
  void joint_state_callback(const sensor_msgs::msg::JointState::SharedPtr msg)
  {
    if (!model_loaded_) return;

    if (msg->name.size() < NUM_JOINTS || msg->position.size() < NUM_JOINTS) {
      RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
        "JointState incomplet : %zu joints reçus, %zu attendus.",
        msg->name.size(), NUM_JOINTS);
      return;
    }

    // Remplir les positions joints dans l'ordre attendu
    for (size_t i = 0; i < NUM_JOINTS; ++i) {
      const auto& joint_name = expected_joint_order_[i];
      auto it = std::find(msg->name.begin(), msg->name.end(), joint_name);
      if (it == msg->name.end()) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
          "Joint manquant : '%s'", joint_name.c_str());
        return;
      }
      size_t idx = std::distance(msg->name.begin(), it);
      input_buffer_[i] = static_cast<float>(msg->position[idx]);
    }

    // Ajouter les données IMU
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

    // Utiliser les vrais noms de tenseurs lus depuis le modèle
    const char* input_names[]  = {input_name_.c_str()};
    const char* output_names[] = {output_name_.c_str()};

    std::vector<Ort::Value> output_tensors;
    try {
      output_tensors = session_.Run(
        Ort::RunOptions{nullptr},
        input_names,  &input_tensor, 1,
        output_names, 1);
    }
    catch (const Ort::Exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Erreur inférence ONNX : %s", e.what());
      return;
    }

    const float* output_values =
      output_tensors.front().GetTensorMutableData<float>();

    // ── Publication ─────────────────────────────────────────────────────────
    auto cmd_msg = std_msgs::msg::Float64MultiArray();
    cmd_msg.data.resize(NUM_JOINTS);

    for (size_t i = 0; i < NUM_JOINTS; ++i) {
      float raw = output_values[i] * ACTION_SCALE;
      if (!std::isfinite(raw)) {
        RCLCPP_ERROR(this->get_logger(),
          "Sortie ONNX non-finie à l'index %zu, commande annulée.", i);
        return;
      }
      cmd_msg.data[i] = static_cast<double>(
        std::clamp(raw,
          static_cast<float>(-MAX_JOINT_RAD),
          static_cast<float>( MAX_JOINT_RAD)));
    }

    cmd_pub_->publish(cmd_msg);
  }

  // ── Membres ───────────────────────────────────────────────────────────────
  Ort::Env     env_;
  Ort::Session session_;
  bool         model_loaded_;

  // Noms réels des tenseurs (lus depuis le modèle, pas hardcodés)
  std::string  input_name_;
  std::string  output_name_;

  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr joint_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr         imu_sub_;
  rclcpp::Publisher<std_msgs::msg::Float64MultiArray>::SharedPtr  cmd_pub_;

  std::vector<std::string> expected_joint_order_;
  std::vector<float>       imu_data_;
  std::vector<float>       input_buffer_;
  std::mutex               imu_mutex_;
};

int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<MutoPolicyInferenceNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}