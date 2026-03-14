/*
** get_position.cpp for pkg_get_position [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/pkg_get_position/src
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Thu Mar 12 10:27:33 AM 2026 dirennoukpo
** Last update Sat Mar 13 10:23:59 PM 2026 dirennoukpo
*/

#include "../include/pkg_get_position/get_position.hpp"

GetPosition::GetPosition(const std::string &name, const BT::NodeConfiguration &config)
    : BT::StatefulActionNode(name, config)
{
    _node = rclcpp::Node::make_shared("get_position");
}

void GetPosition::update_subscription(const std::string &robot_id)
{
    if (_robot_id == robot_id && _sub) {
        return;
    }

    _robot_id = robot_id;
    _has_pose = false;
    const auto qos = rclcpp::QoS(1).transient_local().reliable();
    _sub = _node->create_subscription<geometry_msgs::msg::PoseStamped>
    (
        "/robot" + robot_id + "/pose",
        qos,
        [this] (const geometry_msgs::msg::PoseStamped::SharedPtr pose)
        {
            _pose = *pose;
            _has_pose = true;
        }
    );
}

BT::PortsList GetPosition::providedPorts()
{
    return
    {
        BT::InputPort<std::string>("robot_id"),
        BT::InputPort<int>("wait_timeout_ms", 2000, "Timeout before reporting a missing pose"),
        BT::OutputPort<geometry_msgs::msg::PoseStamped>("position")
    };
}

BT::NodeStatus GetPosition::onStart()
{
    auto id = getInput<std::string>("robot_id");
    if (!id) {
        throw BT::RuntimeError("Missing InputPort [robot_id]: ", id.error());
    }

    update_subscription(id.value());
    const auto timeout_ms = getInput<int>("wait_timeout_ms").value_or(2000);
    _deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    return wait_for_pose();
}

BT::NodeStatus GetPosition::onRunning()
{
    return wait_for_pose();
}

void GetPosition::onHalted()
{
}

BT::NodeStatus GetPosition::wait_for_pose()
{
    rclcpp::spin_some(_node);

    if (_has_pose) {
        setOutput("position", _pose);
        return BT::NodeStatus::SUCCESS;
    }

    if (std::chrono::steady_clock::now() >= _deadline) {
        return BT::NodeStatus::FAILURE;
    }

    return BT::NodeStatus::RUNNING;
}

BT_REGISTER_NODES(factory) {
    factory.registerNodeType<GetPosition>("GetPosition");
}
