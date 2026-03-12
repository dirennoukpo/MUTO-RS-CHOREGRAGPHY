/*
** compute_offset_goal.hpp for location [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/location/include/location
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Thu Mar 12 11:17:18 AM 2026 dirennoukpo
** Last update Fri Mar 12 11:46:27 AM 2026 dirennoukpo
*/

#pragma once

#include "rclcpp/rclcpp.hpp"
#include "behaviortree_cpp_v3/action_node.h"
#include "behaviortree_cpp_v3/bt_factory.h"
#include "geometry_msgs/msg/twist.hpp"

class ComputeFollowerPose : public BT::SyncActionNode
{
    public:
        ComputeFollowerPose(const std::string &name, const BT::NodeConfiguration &config);
        static BT::PortsList providedPorts();
        BT::NodeStatus tick() override;
};