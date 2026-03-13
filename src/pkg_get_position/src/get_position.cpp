/*
** get_position.cpp for pkg_get_position [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/pkg_get_position/src
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Thu Mar 12 10:27:33 AM 2026 dirennoukpo
** Last update Sat Mar 13 2:46:55 PM 2026 dirennoukpo
*/

#include "../include/pkg_get_position/get_position.hpp"

CheckLeaderPose::CheckLeaderPose(const std::string &name, const BT::NodeConfiguration &config)
  : BT::SyncActionNode(name, config)
{
    _node = rclcpp::Node::make_shared("check_leader_pose");
}

void CheckLeaderPose::update_subscription(const std::string &robot_id)
{
    if (_robot_id == robot_id && _sub) {
        return;
    }

    _robot_id = robot_id;
    _has_pose = false;
    _sub = _node->create_subscription<geometry_msgs::msg::Twist>
    (
        "/robot" + robot_id + "/cmd_vel",
        10,
        [this] (const geometry_msgs::msg::Twist::SharedPtr pose)
        {
            _pose = *pose;
            _has_pose = true;
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
    auto id = getInput<std::string>("robot_id");
    if (!id) {
        throw BT::RuntimeError("Missing InputPort [robot_id]: ", id.error());
    }

    update_subscription(id.value());
    rclcpp::spin_some(_node);

    if (!_has_pose) {
        return BT::NodeStatus::RUNNING;
    }

    setOutput("leader_pose", _pose);
    return BT::NodeStatus::SUCCESS;
}

BT_REGISTER_NODES(factory) {
    factory.registerNodeType<CheckLeaderPose>("CheckLeaderPose");
}
