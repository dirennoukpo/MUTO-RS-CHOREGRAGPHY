/*
** batterylevel.hpp for pkg_battery_level [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/pkg_battery_level/include/pkg_battery_level
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Thu Mar 12 8:11:48 AM 2026 dirennoukpo
** Last update Sat Mar 13 2:53:45 PM 2026 dirennoukpo
*/

#pragma once
#include <atomic>
#include <chrono>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "behaviortree_cpp_v3/action_node.h"
#include "std_msgs/msg/float32.hpp"

class CheckBatteryLevel : public BT::StatefulActionNode
{
    public:
        CheckBatteryLevel(const std::string &name, const BT::NodeConfiguration &config);
        static BT::PortsList providedPorts();
        BT::NodeStatus onStart() override;
        BT::NodeStatus onRunning() override;
        void onHalted() override;

    private:
        void update_subscription(const std::string &robot_id);
        BT::NodeStatus wait_for_battery();

        std::atomic<float> _level{0.0F};
        std::atomic<bool> _has_level{false};
        rclcpp::Node::SharedPtr _node;
        rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr _sub;
        std::string _robot_id;
        std::chrono::steady_clock::time_point _deadline;
};
