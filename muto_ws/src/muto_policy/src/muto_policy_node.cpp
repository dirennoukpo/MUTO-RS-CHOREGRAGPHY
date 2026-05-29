#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <std_msgs/msg/float64_multi_array.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <cmath>
#include <iostream>
#include <fstream>

// ─── Constantes configurables ────────────────────────────────────────────────
static constexpr size_t  NUM_JOINTS       = 18;
static constexpr size_t  NUM_IMU_CHANNELS = 6;
static constexpr size_t  INPUT_SIZE       = NUM_JOINTS + NUM_IMU_CHANNELS; // 24
static constexpr float   ACTION_SCALE     = 0.25f;
// FIX: Le clamp utile est ACTION_SCALE * sortie_max_réseau.
// Isaac Lab exporte typiquement des logits bornés à ±1 → ±ACTION_SCALE rad après scale.
// MAX_JOINT_RAD est conservé comme garde-fou absolu (sécurité mécanique),
// mais le clamp effectif est ±(ACTION_SCALE * réseau_max) en pratique.
static constexpr float   MAX_JOINT_RAD    = 1.5707f;  // π/2 — limite mécanique hexapode

class MutoPolicyInferenceNode : public rclcpp::Node
{
public:
  MutoPolicyInferenceNode()
  : Node("muto_policy_inference_node"),
    env_(ORT_LOGGING_LEVEL_WARNING, "MutoPolicy"),
    session_{nullptr},
    model_loaded_(false),
    imu_received_(false),
    infer_pending_(false),
    infer_stop_(false)
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

    // ── 3. Lire les vrais noms des tenseurs ───────────────────────────────────
    if (!resolve_tensor_names()) return;

    // ── 4. Valider les dimensions ─────────────────────────────────────────────
    validate_model_io();

    // ── 5. Ordre des joints (doit correspondre à Isaac Lab) ───────────────────
    // FIX CRITIQUE : virgule manquante après "yh3_Joint" dans le code original.
    // En C++, deux string literals adjacents sans opérateur sont concaténés
    // silencieusement : "yh3_Joint" "zq1_Joint" → "yh3_Jointzq1_Joint".
    // Résultat : 17 joints au lieu de 18, inférence annulée à chaque cycle.
    // Ordre IDENTIQUE à leg_controller dans controllers.yaml.
    // Gauche (z) d'abord, puis droite (y) — chaque groupe: Coxa/Femur/Tibia.
    // CRITIQUE: tout désalignement entre cette liste et le YAML inverse
    // silencieusement les commandes entre pattes → comportement imprévisible.
    expected_joint_order_ = {
      // ── Droite ──────────────────────────────
      "yq1_Joint", "yq2_Joint", "yq3_Joint",  // Front Right - Coxa/Femur/Tibia
      "yz1_Joint", "yz2_Joint", "yz3_Joint",  // Mid Right   - Coxa/Femur/Tibia
      "yh1_Joint", "yh2_Joint", "yh3_Joint"   // Rear Right  - Coxa/Femur/Tibia
      // ── Gauche ──────────────────────────────
      "zq1_Joint", "zq2_Joint", "zq3_Joint",  // Front Left  - Coxa/Femur/Tibia
      "zz1_Joint", "zz2_Joint", "zz3_Joint",  // Mid Left    - Coxa/Femur/Tibia
      "zh1_Joint", "zh2_Joint", "zh3_Joint",  // Rear Left   - Coxa/Femur/Tibia
    };

    // Vérification à la compilation: si NUM_JOINTS change, ce assert signale
    // qu'il faut aussi mettre à jour la liste.
    if (expected_joint_order_.size() != NUM_JOINTS) {
      RCLCPP_FATAL(this->get_logger(),
        "expected_joint_order_ contient %zu noms mais NUM_JOINTS = %zu. "
        "Corrigez la liste ou la constante.",
        expected_joint_order_.size(), NUM_JOINTS);
      return;
    }

    imu_data_.resize(NUM_IMU_CHANNELS, 0.0f);
    input_buffer_.resize(INPUT_SIZE, 0.0f);

    // FIX: Pré-allouer les structures ONNX réutilisées à chaque inférence.
    // Évite les allocations dans la boucle RT (fragmentation, latence).
    mem_info_ = std::make_unique<Ort::MemoryInfo>(
      Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
    input_shape_ = {1, static_cast<int64_t>(INPUT_SIZE)};

    // Stocker clock_ pour éviter get_clock() répété dans les callbacks
    clock_ = this->get_clock();

    // ── 6. Thread d'inférence dédié ──────────────────────────────────────────
    // FIX CRITIQUE : session_.Run() (~10ms) ne doit pas bloquer l'executor ROS2.
    // Un thread dédié attend un signal, exécute l'inférence, publie le résultat.
    // Les callbacks restent non-bloquants → aucune perte de message.
    infer_thread_ = std::thread(&MutoPolicyInferenceNode::infer_thread_fn, this);

    // ── 7. Abonnements / publications ────────────────────────────────────────
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

  ~MutoPolicyInferenceNode()
  {
    // Arrêter proprement le thread d'inférence
    {
      std::lock_guard<std::mutex> lock(infer_mutex_);
      infer_stop_ = true;
      infer_pending_ = true;
    }
    infer_cv_.notify_one();
    if (infer_thread_.joinable()) infer_thread_.join();
  }

private:
  // ── Résolution dynamique des noms de tenseurs ─────────────────────────────
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

    auto input_name_ptr  = session_.GetInputNameAllocated(0, allocator);
    auto output_name_ptr = session_.GetOutputNameAllocated(0, allocator);
    input_name_  = std::string(input_name_ptr.get());
    output_name_ = std::string(output_name_ptr.get());

    // FIX: stocker les pointeurs C pour session_.Run() sans .c_str() répété
    // (les strings membres ne bougent plus après init → pointeurs stables)
    input_name_cstr_  = input_name_.c_str();
    output_name_cstr_ = output_name_.c_str();

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

    int64_t expected_in = static_cast<int64_t>(INPUT_SIZE);
    if (!input_shape.empty() && input_shape.back() != -1 &&
        input_shape.back() != expected_in) {
      RCLCPP_WARN(this->get_logger(),
        "ATTENTION : le modèle attend %ld valeurs en entrée, "
        "mais INPUT_SIZE = %zu. Ajustez la constante dans le code !",
        input_shape.back(), INPUT_SIZE);
    }

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
    imu_received_ = true;
  }

  // ── Callback JointState — non-bloquant, délègue au thread d'inférence ────
  //
  // FIX CRITIQUE: session_.Run() retiré de ce callback.
  // Le callback remplit input_buffer_ et signale le thread d'inférence.
  // Durée du callback: <1ms (copie mémoire uniquement).
  //
  void joint_state_callback(const sensor_msgs::msg::JointState::SharedPtr msg)
  {
    if (!model_loaded_) return;

    if (msg->name.size() < NUM_JOINTS || msg->position.size() < NUM_JOINTS) {
      RCLCPP_WARN_THROTTLE(this->get_logger(), *clock_, 2000,
        "JointState incomplet : %zu joints reçus, %zu attendus.",
        msg->name.size(), NUM_JOINTS);
      return;
    }

    // FIX: warning si IMU jamais reçu (données à zéro → politique aveugle)
    if (!imu_received_) {
      RCLCPP_WARN_THROTTLE(this->get_logger(), *clock_, 5000,
        "Aucune donnée IMU reçue sur /muto/imu — inférence avec IMU=0 !");
    }

    // Remplir les positions joints dans l'ordre attendu par la politique
    for (size_t i = 0; i < NUM_JOINTS; ++i) {
      const auto& joint_name = expected_joint_order_[i];
      auto it = std::find(msg->name.begin(), msg->name.end(), joint_name);
      if (it == msg->name.end()) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *clock_, 2000,
          "Joint manquant : '%s'", joint_name.c_str());
        return;
      }
      size_t idx = std::distance(msg->name.begin(), it);
      input_buffer_infer_[i] = static_cast<float>(msg->position[idx]);
    }

    // Snapshot IMU thread-safe
    {
      std::lock_guard<std::mutex> lock(imu_mutex_);
      std::copy(imu_data_.begin(), imu_data_.end(),
                input_buffer_infer_.begin() + NUM_JOINTS);
    }

    // Signaler le thread d'inférence (sans bloquer)
    {
      std::lock_guard<std::mutex> lock(infer_mutex_);
      // Copier vers le buffer d'inférence protégé
      input_buffer_ = input_buffer_infer_;
      infer_pending_ = true;
    }
    infer_cv_.notify_one();
  }

  // ── Thread d'inférence dédié ──────────────────────────────────────────────
  //
  // session_.Run() est bloquant (~5–15ms selon le modèle).
  // Ce thread absorbe cette latence sans impacter l'executor ROS2.
  // Si un nouveau JointState arrive pendant l'inférence, il est simplement
  // ignoré (infer_pending_ déjà true) → pas d'accumulation de requêtes.
  //
  void infer_thread_fn()
  {
    RCLCPP_INFO(this->get_logger(), "Thread d'inférence démarré");

    // Buffer local pour l'inférence (évite les allocations répétées)
    std::vector<float> local_input(INPUT_SIZE);

    while (true) {
      // Attendre un signal du callback
      {
        std::unique_lock<std::mutex> lock(infer_mutex_);
        infer_cv_.wait(lock, [this]{ return infer_pending_; });
        if (infer_stop_) break;
        local_input = input_buffer_;
        infer_pending_ = false;
      }

      // ── Inférence ONNX (hors mutex — peut prendre ~10ms) ─────────────────
      // FIX: mem_info_ et input_shape_ pré-alloués dans le constructeur.
      // Seul le tenseur d'entrée est recréé (wrapping du buffer, pas de copie).
      Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        *mem_info_,
        local_input.data(), local_input.size(),
        input_shape_.data(), input_shape_.size());

      std::vector<Ort::Value> output_tensors;
      try {
        output_tensors = session_.Run(
          Ort::RunOptions{nullptr},
          &input_name_cstr_,  &input_tensor, 1,
          &output_name_cstr_, 1);
      }
      catch (const Ort::Exception& e) {
        RCLCPP_ERROR(this->get_logger(), "Erreur inférence ONNX : %s", e.what());
        continue;
      }

      const float* output_values =
        output_tensors.front().GetTensorMutableData<float>();

      // ── Construction et publication de la commande ────────────────────────
      auto cmd_msg = std_msgs::msg::Float64MultiArray();
      cmd_msg.data.resize(NUM_JOINTS);
      bool valid = true;

      for (size_t i = 0; i < NUM_JOINTS; ++i) {
        // FIX: clamp AVANT scale → s'assure que la sortie brute du réseau
        // est bornée avant multiplication. Puis clamp MAX_JOINT_RAD comme
        // garde-fou mécanique absolu.
        // Convention Isaac Lab: sortie réseau ∈ [-1, 1] typiquement.
        float raw = output_values[i];
        if (!std::isfinite(raw)) {
          RCLCPP_ERROR(this->get_logger(),
            "Sortie ONNX non-finie à l'index %zu, commande annulée.", i);
          valid = false;
          break;
        }
        float scaled = raw * ACTION_SCALE;
        cmd_msg.data[i] = static_cast<double>(
          std::clamp(scaled, -MAX_JOINT_RAD, MAX_JOINT_RAD));
      }

      if (valid) cmd_pub_->publish(cmd_msg);
    }

    RCLCPP_INFO(this->get_logger(), "Thread d'inférence arrêté");
  }

  // ── Membres ───────────────────────────────────────────────────────────────
  Ort::Env     env_;
  Ort::Session session_;
  bool         model_loaded_;

  // Noms réels des tenseurs (lus depuis le modèle, pas hardcodés)
  std::string  input_name_;
  std::string  output_name_;
  // FIX: pointeurs C stables pour session_.Run() (strings membres ne bougent pas)
  const char*  input_name_cstr_  = nullptr;
  const char*  output_name_cstr_ = nullptr;

  // Structures ONNX pré-allouées (FIX: évite les allocations dans la boucle RT)
  std::unique_ptr<Ort::MemoryInfo> mem_info_;
  std::array<int64_t, 2>           input_shape_{};

  rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr joint_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr         imu_sub_;
  rclcpp::Publisher<std_msgs::msg::Float64MultiArray>::SharedPtr  cmd_pub_;

  // FIX: clock_ stocké pour éviter get_clock() répété dans les callbacks
  rclcpp::Clock::SharedPtr clock_;

  std::vector<std::string> expected_joint_order_;

  // IMU: protégé par imu_mutex_, flag de réception
  std::mutex         imu_mutex_;
  std::vector<float> imu_data_;
  std::atomic<bool>  imu_received_;

  // Buffers d'entrée: input_buffer_infer_ rempli dans le callback (pas de lock),
  // input_buffer_ copié sous infer_mutex_ et lu dans le thread d'inférence.
  std::vector<float> input_buffer_infer_;  // écrit dans joint_state_callback
  std::vector<float> input_buffer_;        // protégé par infer_mutex_

  // Thread d'inférence dédié
  std::thread              infer_thread_;
  std::mutex               infer_mutex_;
  std::condition_variable  infer_cv_;
  bool                     infer_pending_;
  bool                     infer_stop_;
};

int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<MutoPolicyInferenceNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}