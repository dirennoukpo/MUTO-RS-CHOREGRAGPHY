/*
** new_position.cpp for MUTO-RS-CHOREGRAGPHY [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/pkg_new_position/src
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Fri Mar 13 3:28:12 PM 2026 dirennoukpo
** Last update Sat Mar 13 10:24:32 PM 2026 dirennoukpo
*/

#include "../include/pkg_new_position/new_position.hpp"

#include "tf2/LinearMath/Matrix3x3.h"
#include "tf2/LinearMath/Quaternion.h"

SetNewPosition::SetNewPosition(const std::string &name, const BT::NodeConfiguration &config)
  : BT::SyncActionNode(name, config)
{
}

BT::PortsList SetNewPosition::providedPorts()
{
    return
    {
           BT::InputPort<geometry_msgs::msg::PoseStamped>("ref_position"),
        BT::InputPort<double>("offset_x", 3.0, "Offset to add on X axis"),
        BT::InputPort<double>("offset_y", 3.0, "Offset to add on Y axis"),
           BT::InputPort<double>("offset_z", 0.0, "Offset to add on Z axis"),
           BT::OutputPort<geometry_msgs::msg::PoseStamped>("position")
    };
}

BT::NodeStatus SetNewPosition::tick()
{
    auto ref = getInput<geometry_msgs::msg::PoseStamped>("ref_position");
    if (!ref) {
        throw BT::RuntimeError("Missing InputPort [ref_position]: ", ref.error());
    }

    auto offset_x = getInput<double>("offset_x");
    auto offset_y = getInput<double>("offset_y");
    auto offset_z = getInput<double>("offset_z");

    const double delta_x = offset_x ? offset_x.value() : 3.0;
    const double delta_y = offset_y ? offset_y.value() : 3.0;
    const double delta_z = offset_z ? offset_z.value() : 0.0;

    geometry_msgs::msg::PoseStamped new_pose = ref.value();
    if (new_pose.header.frame_id.empty()) {
        new_pose.header.frame_id = "map";
    }

    tf2::Quaternion quaternion(
        new_pose.pose.orientation.x,
        new_pose.pose.orientation.y,
        new_pose.pose.orientation.z,
        new_pose.pose.orientation.w
    );

    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
    tf2::Matrix3x3(quaternion).getRPY(roll, pitch, yaw);

    new_pose.pose.position.x += (delta_x * std::cos(yaw)) - (delta_y * std::sin(yaw));
    new_pose.pose.position.y += (delta_x * std::sin(yaw)) + (delta_y * std::cos(yaw));
    new_pose.pose.position.z += delta_z;

    setOutput("position", new_pose);
    return BT::NodeStatus::SUCCESS;
}

BT_REGISTER_NODES(factory) {
    factory.registerNodeType<SetNewPosition>("SetNewPosition");
}
