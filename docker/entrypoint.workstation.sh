#!/bin/bash
##
## entrypoint.workstation.sh - Development entrypoint for MUTO-RS Workstation
##
## For local development machines. Provides:
## - Auto-build if workspace not built
## - Debugging and development tools
## - Symlink support for live code editing
## - Verbose logging for troubleshooting
##
## Made by dirennoukpo
## Login   <diren.noukpo@epitech.eu>
##
## Started on  Thu Apr 16 1:18:32 PM 2026 dirennoukpo
## Last update Fri Apr 16 3:36:27 PM 2026 dirennoukpo
##

set -eo pipefail

echo "[entrypoint] 🎮 MUTO-RS Workstation Development Container"

set -eo pipefail
echo ""

# ─────────────────────────────────────────────────────────────
# STEP 1: Source ROS2 environment
# ─────────────────────────────────────────────────────────────

if [ ! -f "/opt/ros/${ROS_DISTRO}/setup.bash" ]; then
    echo "[entrypoint] ❌ ERROR: ROS2 ${ROS_DISTRO} not found"
    echo "[entrypoint]    Expected: /opt/ros/${ROS_DISTRO}/setup.bash"
    exit 1
fi

set +e
source "/opt/ros/${ROS_DISTRO}/setup.bash" 2>/dev/null
ROS_SETUP_RESULT=$?
set -e

if [ $ROS_SETUP_RESULT -ne 0 ]; then
    echo "[entrypoint] ⚠️  ROS setup returned code $ROS_SETUP_RESULT"
fi

echo "[entrypoint] ✓ ROS2 ${ROS_DISTRO} sourced"

# ─────────────────────────────────────────────────────────────
# STEP 2: Check and auto-build workspace if needed
# ─────────────────────────────────────────────────────────────

if [ ! -f "/muto_ws/install/setup.bash" ]; then
    echo "[entrypoint] ⚠️  Workspace not built, attempting auto-build..."
    echo "[entrypoint]"
    
    if [ ! -d "/muto_ws/src" ]; then
        echo "[entrypoint] ❌ ERROR: Source directory not found: /muto_ws/src"
        echo "[entrypoint]    Workspace structure appears corrupted."
        exit 1
    fi
    
    echo "[entrypoint] 🔨 Building workspace (this may take a few minutes)..."
    cd /muto_ws
    
    set +e
    rm -rf build install log
    colcon build --cmake-args -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -20
    BUILD_RESULT=$?
    set -e
    
    if [ $BUILD_RESULT -ne 0 ]; then
        echo "[entrypoint] ❌ ERROR: Workspace build failed (code $BUILD_RESULT)"
        echo "[entrypoint]    Try manual build: colcon build --cmake-args -DCMAKE_BUILD_TYPE=Debug"
        exit 1
    fi
    
    echo "[entrypoint] ✅ Workspace built successfully"
    echo ""
else
    echo "[entrypoint] ✓ Workspace already built"
fi

# ─────────────────────────────────────────────────────────────
# STEP 3: Source built workspace
# ─────────────────────────────────────────────────────────────

set +e
source "/muto_ws/install/setup.bash" 2>/dev/null
WS_SETUP_RESULT=$?
set -e

if [ $WS_SETUP_RESULT -ne 0 ]; then
    echo "[entrypoint] ⚠️  Workspace setup returned code $WS_SETUP_RESULT"
fi

echo "[entrypoint] ✓ Workspace sourced"

# ─────────────────────────────────────────────────────────────
# STEP 4: Print useful development info
# ─────────────────────────────────────────────────────────────

echo "[entrypoint] ✅ Environment ready"
echo ""
echo "[entrypoint] Useful commands:"
echo "  colcon build                          # Rebuild workspace"
echo "  ros2 run muto_rs_synchronization dance_leader.py  # Start leader"
echo "  ros2 run muto_rs_synchronization dance_follower.py # Start follower"
echo "  ros2 launch muto_rs_synchronization dance_choreography.launch.py"
echo ""

# ─────────────────────────────────────────────────────────────
# STEP 5: Execute command or start bash
# ─────────────────────────────────────────────────────────────

exec "$@"
