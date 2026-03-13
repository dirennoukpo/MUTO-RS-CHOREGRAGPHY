/*
** new_position.hpp for MUTO-RS-CHOREGRAGPHY [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/pkg_new_position/include/pkg_new_position
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Fri Mar 13 3:28:05 PM 2026 dirennoukpo
** Last update Sat Mar 13 3:40:34 PM 2026 dirennoukpo
*/

#pragma once
#include <string>
#include "rclcpp/rclcpp.hpp"
#include "behaviortree_cpp_v3/action_node.h"
#include "behaviortree_cpp_v3/bt_factory.h"
#include "geometry_msgs/msg/twist.hpp"

class SetNewPosition : public BT::SyncActionNode
{
    public:
        SetNewPosition(const std::string &name, const BT::NodeConfiguration &config);
        static BT::PortsList providedPorts();
        BT::NodeStatus tick() override;
    private:
};
