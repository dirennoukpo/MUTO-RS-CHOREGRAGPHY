/*
** batterylevel.cpp for checkbatterylevel [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/checkbatterylevel/src
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Thu Mar 12 8:09:51 AM 2026 dirennoukpo
** Last update Fri Mar 12 10:48:16 AM 2026 dirennoukpo
*/

#include "../include/checkbatterylevel/batterylevel.hpp"

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
        }
    );
}

BT::PortsList CheckBatteryLevel::providedPorts()
{
    return
    {
        BT::InputPort<std::string>("robot_id"),
        BT::OutputPort<float>("battery_level")
    };
}

BT::NodeStatus CheckBatteryLevel::tick()
{
    // float threshold = 7.0;
    
    // if (_level < threshold) {
    //     return BT::NodeStatus::FAILURE;
    // }
    setOutput("battery_level", _level);
    return BT::NodeStatus::SUCCESS;
}

BT_REGISTER_NODES(factory) {
    factory.registerNodeType<CheckBatteryLevel>("CheckBatteryLevel");
}
