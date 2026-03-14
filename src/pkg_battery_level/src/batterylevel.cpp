/*
** batterylevel.cpp for pkg_battery_level [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/pkg_battery_level/src
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Thu Mar 12 8:09:51 AM 2026 dirennoukpo
** Last update Sat Mar 13 2:53:52 PM 2026 dirennoukpo
*/

#include "../include/pkg_battery_level/batterylevel.hpp"
#include "behaviortree_cpp_v3/bt_factory.h"

CheckBatteryLevel::CheckBatteryLevel(const std::string &name, const BT::NodeConfiguration &config)
  : BT::StatefulActionNode(name, config)
{
    _node = rclcpp::Node::make_shared("check_battery_level");
}

void CheckBatteryLevel::update_subscription(const std::string &robot_id)
{
    if (_robot_id == robot_id && _sub) {
        return;
    }

    _robot_id = robot_id;
    _has_level = false;
    const auto qos = rclcpp::QoS(1).transient_local().reliable();
    _sub = _node->create_subscription<std_msgs::msg::Float32>
    (
        "/robot" + robot_id + "/voltage",
        qos,
        [this] (const std_msgs::msg::Float32::SharedPtr value)
        {
            _level = value->data;
            _has_level = true;
        }
    );
}

BT::PortsList CheckBatteryLevel::providedPorts()
{
    return
    {
        BT::InputPort<std::string>("robot_id"),
        BT::InputPort<float>("min_battery_level", 0.0F, "Battery threshold"),
        BT::InputPort<int>("wait_timeout_ms", 2000, "Timeout before reporting missing battery telemetry"),
        BT::OutputPort<float>("battery_level")
    };
}

BT::NodeStatus CheckBatteryLevel::onStart()
{
    auto id = getInput<std::string>("robot_id");
    if (!id) {
        throw BT::RuntimeError("Missing InputPort [robot_id]: ", id.error());
    }

    update_subscription(id.value());
    const auto timeout_ms = getInput<int>("wait_timeout_ms").value_or(2000);
    _deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    return wait_for_battery();
}

BT::NodeStatus CheckBatteryLevel::onRunning()
{
    return wait_for_battery();
}

void CheckBatteryLevel::onHalted()
{
}

BT::NodeStatus CheckBatteryLevel::wait_for_battery()
{
    rclcpp::spin_some(_node);

    if (!_has_level.load()) {
        if (std::chrono::steady_clock::now() >= _deadline) {
            return BT::NodeStatus::FAILURE;
        }
        return BT::NodeStatus::RUNNING;
    }

    float threshold = 0.0F;
    if (auto min_battery_level = getInput<float>("min_battery_level")) {
        threshold = min_battery_level.value();
    }

    const float level = _level.load();
    setOutput("battery_level", level);

    if (level < threshold) {
        return BT::NodeStatus::FAILURE;
    }

    return BT::NodeStatus::SUCCESS;
}

BT_REGISTER_NODES(factory) {
    factory.registerNodeType<CheckBatteryLevel>("CheckBatteryLevel");
}

