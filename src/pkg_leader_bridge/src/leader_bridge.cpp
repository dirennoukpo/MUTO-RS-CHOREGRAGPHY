#include <memory>
#include <string>

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "geometry_msgs/msg/pose_with_covariance_stamped.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/battery_state.hpp"
#include "std_msgs/msg/float32.hpp"

class LeaderBridge : public rclcpp::Node
{
public:
  LeaderBridge()
  : Node("leader_bridge")
  {
    const auto robot_id = declare_parameter<std::string>("robot_id", "1");
    const auto pose_source_topic = declare_parameter<std::string>("pose_source_topic", "/amcl_pose");
    const auto pose_source_type = declare_parameter<std::string>("pose_source_type", "pose_with_covariance_stamped");
    const auto voltage_source_topic = declare_parameter<std::string>("voltage_source_topic", "/voltage");
    const auto voltage_source_type = declare_parameter<std::string>("voltage_source_type", "float32");
    const auto pose_output_topic = declare_parameter<std::string>("pose_output_topic", "/robot" + robot_id + "/pose");
    const auto voltage_output_topic = declare_parameter<std::string>("voltage_output_topic", "/robot" + robot_id + "/voltage");

    pose_publisher_ = create_publisher<geometry_msgs::msg::PoseStamped>(pose_output_topic, 10);
    voltage_publisher_ = create_publisher<std_msgs::msg::Float32>(voltage_output_topic, 10);

    configure_pose_bridge(pose_source_topic, pose_source_type);
    configure_voltage_bridge(voltage_source_topic, voltage_source_type);

    RCLCPP_INFO(
      get_logger(),
      "Leader bridge ready: pose %s (%s) -> %s, voltage %s (%s) -> %s",
      pose_source_topic.c_str(),
      pose_source_type.c_str(),
      pose_output_topic.c_str(),
      voltage_source_topic.c_str(),
      voltage_source_type.c_str(),
      voltage_output_topic.c_str());
  }

private:
  void configure_pose_bridge(const std::string &topic, const std::string &type)
  {
    if (type == "pose_stamped") {
      pose_stamped_subscription_ = create_subscription<geometry_msgs::msg::PoseStamped>(
        topic,
        10,
        [this](const geometry_msgs::msg::PoseStamped::SharedPtr message) {
          pose_publisher_->publish(*message);
        });
      return;
    }

    if (type == "pose_with_covariance_stamped") {
      pose_with_covariance_subscription_ = create_subscription<geometry_msgs::msg::PoseWithCovarianceStamped>(
        topic,
        10,
        [this](const geometry_msgs::msg::PoseWithCovarianceStamped::SharedPtr message) {
          geometry_msgs::msg::PoseStamped pose;
          pose.header = message->header;
          pose.pose = message->pose.pose;
          pose_publisher_->publish(pose);
        });
      return;
    }

    if (type == "odometry") {
      odometry_subscription_ = create_subscription<nav_msgs::msg::Odometry>(
        topic,
        10,
        [this](const nav_msgs::msg::Odometry::SharedPtr message) {
          geometry_msgs::msg::PoseStamped pose;
          pose.header = message->header;
          pose.pose = message->pose.pose;
          pose_publisher_->publish(pose);
        });
      return;
    }

    throw std::runtime_error("Unsupported pose_source_type: " + type);
  }

  void configure_voltage_bridge(const std::string &topic, const std::string &type)
  {
    if (type == "float32") {
      float_subscription_ = create_subscription<std_msgs::msg::Float32>(
        topic,
        10,
        [this](const std_msgs::msg::Float32::SharedPtr message) {
          voltage_publisher_->publish(*message);
        });
      return;
    }

    if (type == "battery_state") {
      battery_state_subscription_ = create_subscription<sensor_msgs::msg::BatteryState>(
        topic,
        10,
        [this](const sensor_msgs::msg::BatteryState::SharedPtr message) {
          std_msgs::msg::Float32 voltage;
          voltage.data = static_cast<float>(message->voltage);
          voltage_publisher_->publish(voltage);
        });
      return;
    }

    throw std::runtime_error("Unsupported voltage_source_type: " + type);
  }

  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr pose_publisher_;
  rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr voltage_publisher_;
  rclcpp::Subscription<geometry_msgs::msg::PoseStamped>::SharedPtr pose_stamped_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::PoseWithCovarianceStamped>::SharedPtr pose_with_covariance_subscription_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odometry_subscription_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr float_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::BatteryState>::SharedPtr battery_state_subscription_;
};

int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<LeaderBridge>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}