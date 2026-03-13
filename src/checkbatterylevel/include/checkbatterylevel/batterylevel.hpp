/*
** batterylevel.hpp for checkbatterylevel [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/checkbatterylevel/include/checkbatterylevel
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Thu Mar 12 8:11:48 AM 2026 dirennoukpo
** Last update Fri Mar 12 11:52:13 AM 2026 dirennoukpo
*/

#pragma once
#include <atomic>

#include "rclcpp/rclcpp.hpp"
#include "behaviortree_cpp_v3/action_node.h"
#include "std_msgs/msg/float32.hpp"

class CheckBatteryLevel : public BT::SyncActionNode
{
    public:
        CheckBatteryLevel(const std::string &name, const BT::NodeConfiguration &config);
        static BT::PortsList providedPorts();
        BT::NodeStatus tick() override;

    private:
        std::atomic<float> _level{0.0F};
        std::atomic<bool> _has_level{false};
        rclcpp::Node::SharedPtr _node;
        rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr _sub;
};
