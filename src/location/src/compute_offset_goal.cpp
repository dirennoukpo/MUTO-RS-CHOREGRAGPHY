/*
** compute_offset_goal.cpp for location [WSL: Ubuntu-22.04] in /home/edwin/MUTO-RS-CHOREGRAGPHY/src/location/src
**
** Made by dirennoukpo
** Login   <diren.noukpo@epitech.eu>
**
** Started on  Thu Mar 12 11:16:49 AM 2026 dirennoukpo
** Last update Fri Mar 12 11:46:27 AM 2026 dirennoukpo
*/

#include "../include/location/compute_offset_goal.hpp"

ComputeFollowerPose::ComputeFollowerPose(const std::string &name, const BT::NodeConfiguration &config)
  : BT::SyncActionNode(name, config)
{}

BT::PortsList ComputeFollowerPose::providedPorts()
{
    return
    {
        BT::InputPort<geometry_msgs::msg::Twist>("leader_pose"),
        BT::InputPort<geometry_msgs::msg::Twist>("direct_goal"),
        BT::OutputPort<geometry_msgs::msg::Twist>("follower_goal")
    };
}

BT::NodeStatus ComputeFollowerPose::tick()
{
    auto s_leader_pose = getInput<geometry_msgs::msg::Twist>("leader_pose");
    if (!s_leader_pose) {
        throw BT::RuntimeError("Missing InputPort [leader_pose]: ", s_leader_pose.error());
    }

    auto s_direct_goal = getInput<geometry_msgs::msg::Twist>("direct_goal");
    if (!s_direct_goal) {
        throw BT::RuntimeError("Missing InputPort [direct_goal]: ", s_direct_goal.error());
    }

    const auto &leader_pose = s_leader_pose.value();
    const auto &direct_goal = s_direct_goal.value();

    geometry_msgs::msg::Twist follower_goal;
    follower_goal.linear.x = leader_pose.linear.x + direct_goal.linear.x;
    follower_goal.linear.y = leader_pose.linear.y + direct_goal.linear.y;
    follower_goal.linear.z = leader_pose.linear.z + direct_goal.linear.z;
    follower_goal.angular.x = leader_pose.angular.x + direct_goal.angular.x;
    follower_goal.angular.y = leader_pose.angular.y + direct_goal.angular.y;
    follower_goal.angular.z = leader_pose.angular.z + direct_goal.angular.z;

    setOutput("follower_goal", follower_goal);
    return BT::NodeStatus::SUCCESS;
}

BT_REGISTER_NODES(factory) {
    factory.registerNodeType<ComputeFollowerPose>("ComputeFollowerPose");
}
