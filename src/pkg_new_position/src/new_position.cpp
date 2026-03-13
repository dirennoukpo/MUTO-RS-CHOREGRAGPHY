/*
** new_position.cpp for MUTO-RS-CHOREGRAGPHY [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/pkg_new_position/src
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Fri Mar 13 3:28:12 PM 2026 dirennoukpo
** Last update Sat Mar 13 3:42:10 PM 2026 dirennoukpo
*/

#include "../include/pkg_new_position/new_position.hpp"

SetNewPosition::SetNewPosition(const std::string &name, const BT::NodeConfiguration &config)
  : BT::SyncActionNode(name, config)
{
}

BT::PortsList SetNewPosition::providedPorts()
{
    return
    {
        BT::InputPort<geometry_msgs::msg::Twist>("ref_position"),
        BT::InputPort<double>("offset_x", 3.0, "Offset to add on X axis"),
        BT::InputPort<double>("offset_y", 3.0, "Offset to add on Y axis"),
        BT::InputPort<double>("offset_z", 3.0, "Offset to add on Z axis"),
        BT::OutputPort<geometry_msgs::msg::Twist>("position")
    };
}

BT::NodeStatus SetNewPosition::tick()
{
    auto ref = getInput<geometry_msgs::msg::Twist>("ref_position");
    if (!ref) {
        throw BT::RuntimeError("Missing InputPort [ref_position]: ", ref.error());
    }

    auto offset_x = getInput<double>("offset_x");
    auto offset_y = getInput<double>("offset_y");
    auto offset_z = getInput<double>("offset_z");

    geometry_msgs::msg::Twist new_pose = ref.value();
    new_pose.linear.x += offset_x ? offset_x.value() : 3.0;
    new_pose.linear.y += offset_y ? offset_y.value() : 3.0;
    new_pose.linear.z += offset_z ? offset_z.value() : 3.0;
    // angular (orientation) is left unchanged

    setOutput("position", new_pose);
    return BT::NodeStatus::SUCCESS;
}

BT_REGISTER_NODES(factory) {
    factory.registerNodeType<SetNewPosition>("SetNewPosition");
}
