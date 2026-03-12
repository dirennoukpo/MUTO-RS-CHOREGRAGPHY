/*
** get_leader_pose.cpp for location [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/location/src
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Thu Mar 12 10:27:33 AM 2026 dirennoukpo
** Last update Fri Mar 12 11:12:58 AM 2026 dirennoukpo
*/

#include "../include/location/get_leader_pose.hpp"

CheckLeaderPose::CheckLeaderPose(const std::string &name, const BT::NodeConfiguration &config)
  : BT::SyncActionNode(name, config)
{
    _node = rclcpp::Node::make_shared("check_leader_pose");
    auto id = getInput<std::string>("robot_id");
    if (!id)
        throw BT::RuntimeError("Missing InputPort [robot_id]");
    std::string robot_id = id.value();
    _sub = _node->create_subscription<geometry_msgs::msg::Twist>
    (
        "/robot" + robot_id + "/cmd_vel",
        10,
        [this] (const geometry_msgs::msg::Twist::SharedPtr pose)
        {
            _pose = *pose;
        }
    );
}

BT::PortsList CheckLeaderPose::providedPorts()
{
    return
    {
        BT::InputPort<std::string>("robot_id"),
        BT::OutputPort<geometry_msgs::msg::Twist>("leader_pose")
    };
}

BT::NodeStatus CheckLeaderPose::tick()
{
    setOutput("leader_pose", _pose);
    return BT::NodeStatus::SUCCESS;
}


BT_REGISTER_NODES(factory) {
    factory.registerNodeType<CheckLeaderPose>("CheckLeaderPose");
}
