# muto_rs_nav_leader_follower

This folder contains assets for a leader-follower Navigation2 setup.

## Content

- maps/electronics_room.yaml
- maps/electronics_room.pgm
- behavior_trees/nav_leader_follower.xml

## Behavior Tree

The tree uses a `ReactiveSequence` to continuously:

1. Check follower battery level (`robot_id=2`)
2. Read leader pose (`robot_id=1`)
3. Compute an offset target pose (`offset_x=-2.0`, `offset_y=0.0`)
4. Navigate to the updated target

Blackboard keys:

- `{battery_level}`
- `{leader_position}`
- `{goal}`

## Launch

```bash
ros2 launch /workspace/src/muto_rs_nav_leader_follower/launch/nav2_bringup.launch.py
```

Optional (RViz/bench mode without dynamic odometry):

```bash
ros2 launch /workspace/src/muto_rs_nav_leader_follower/launch/nav2_bringup.launch.py use_sim_time:=False use_fake_map_tf:=False use_fake_odom_tf:=True use_fake_base_link_tf:=True
```
