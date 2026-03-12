/*
** batterylevel.cpp for checkbatterylevel [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/checkbatterylevel/src
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Thu Mar 12 8:09:51 AM 2026 dirennoukpo
** Last update Fri Mar 12 11:52:46 AM 2026 dirennoukpo
*/

#include "checkbatterylevel/batterylevel.hpp"
#include "behaviortree_cpp_v3/bt_factory.h"

CheckBatteryLevel::CheckBatteryLevel(const std::string &name, const BT::NodeConfiguration &config)
  : BT::SyncActionNode(name, config)
{
    _node = rclcpp::Node::make_shared("check_battery_level");
    auto id = getInput<std::string>("robot_id");
    if (!id)
        throw BT::RuntimeError("Missing InputPort [robot_id]");
    std::string robot_id = id.value();
    _sub = _node->create_subscription<std_msgs::msg::Float32>
    (
        "/robot" + robot_id + "/voltage",
        10,
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
        BT::OutputPort<float>("battery_level")
    };
}

BT::NodeStatus CheckBatteryLevel::tick()
{
    rclcpp::spin_some(_node);

    if (!_has_level.load()) {
        return BT::NodeStatus::FAILURE;
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
