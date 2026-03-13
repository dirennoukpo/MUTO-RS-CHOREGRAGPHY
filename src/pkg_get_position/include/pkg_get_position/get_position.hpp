/*
** get_position.hpp for pkg_get_position [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/pkg_get_position/include/pkg_get_position
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Thu Mar 12 10:27:25 AM 2026 dirennoukpo
** Last update Sat Mar 13 2:46:57 PM 2026 dirennoukpo
*/

#pragma once

#include <string>

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
        void update_subscription(const std::string &robot_id);

        rclcpp::Node::SharedPtr _node;
        rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr _sub;
        std::string _robot_id;
        bool _has_pose{false};
        geometry_msgs::msg::Twist _pose;
};
