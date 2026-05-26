#!/bin/bash
set -e

# Source ROS2 si dispo
if [ -f /opt/ros/${ROS_DISTRO}/setup.bash ]; then
  source /opt/ros/${ROS_DISTRO}/setup.bash
fi

# Source workspace si dispo
if [ -f /muto_ws/install/setup.bash ]; then
  source /muto_ws/install/setup.bash
fi

export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export FASTRTPS_DEFAULT_PROFILES_FILE=/etc/fastdds/fastdds_base.xml

# Exécute la commande passée par docker-compose (ex: bash)
exec "$@"
