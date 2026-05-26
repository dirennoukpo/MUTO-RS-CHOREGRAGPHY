#!/bin/bash
set -e

# ────────────────────────────────────────────────
# Sourcing ROS2 et workspace
# ────────────────────────────────────────────────
source /opt/ros/${ROS_DISTRO}/setup.bash
if [ -f /muto_ws/install/setup.bash ]; then
    source /muto_ws/install/setup.bash
fi

# Variables critiques FastDDS
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export FASTRTPS_DEFAULT_PROFILES_FILE=/etc/fastdds/fastdds_base.xml

# ────────────────────────────────────────────────
# Commande par défaut
# ────────────────────────────────────────────────
if [ "$#" -eq 0 ]; then
    echo "[INFO] Aucun argument fourni → shell interactif"
    exec bash
else
    echo "[INFO] Lancement de la commande : $@"
    exec "$@"
fi
