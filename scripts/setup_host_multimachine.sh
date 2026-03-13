#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [ -f /opt/ros/humble/setup.bash ]; then
  source /opt/ros/humble/setup.bash
else
  echo "ROS 2 Humble not found on host: /opt/ros/humble/setup.bash"
  echo "Install ROS 2 Humble on the host, then run: source scripts/setup_host_multimachine.sh"
  return 1 2>/dev/null || exit 1
fi

# Multi-machine ROS 2 settings (matching the Docker container setup)
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-33}"
export ROS_LOCALHOST_ONLY=0
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export FASTDDS_BUILTIN_TRANSPORTS=UDPv4

if [ -f "${WS_ROOT}/install/setup.bash" ]; then
  source "${WS_ROOT}/install/setup.bash"
fi

echo "Host ROS 2 multi-machine environment enabled"
echo "ROS_DOMAIN_ID=${ROS_DOMAIN_ID}"
echo "ROS_LOCALHOST_ONLY=${ROS_LOCALHOST_ONLY}"
echo "RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION}"
echo "FASTDDS_BUILTIN_TRANSPORTS=${FASTDDS_BUILTIN_TRANSPORTS}"
