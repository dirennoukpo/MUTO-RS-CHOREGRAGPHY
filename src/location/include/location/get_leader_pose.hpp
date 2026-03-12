/*
** get_leader_pose.hpp for location [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/location/include/location
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Thu Mar 12 10:27:25 AM 2026 dirennoukpo
** Last update Fri Mar 12 11:13:12 AM 2026 dirennoukpo
*/

#pragma once

#include "rclcpp/rclcpp.hpp"
#include "behaviortree_cpp_v3/action_node.h"
#include "behaviortree_cpp_v3/bt_factory.h"
#include "geometry_msgs/msg/twist.hpp"

class CheckLeaderPose : public BT::SyncActionNode
{
    public:
        CheckLeaderPose(const std::string &name, const BT::NodeConfiguration &config);
        static BT::PortsList providedPorts();
        BT::NodeStatus tick() override;
    private:
        rclcpp::Node::SharedPtr _node;
        rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr _sub;
        geometry_msgs::msg::Twist _pose;
};
