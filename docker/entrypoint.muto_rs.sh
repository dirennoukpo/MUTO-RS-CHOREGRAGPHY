#!/bin/bash
##
## entrypoint.muto_rs.sh - Production entrypoint for MUTO-RS Robot
## 
## For Raspberry Pi 5 deployment. Enforces strict checks:
## - Workspace MUST be pre-built in the image
## - Fails immediately if workspace not found
## - Suitable for production deployments on constrained hardware
##
## Made by dirennoukpo
## Login   <diren.noukpo@epitech.eu>
##
## Started on  Thu Apr 16 1:18:37 PM 2026 dirennoukpo
## Last update Fri Apr 16 1:54:42 PM 2026 dirennoukpo
##

set -eo pipefail

echo "[entrypoint] 🤖 MUTO-RS Production Container"
echo "[entrypoint] ROS_DISTRO: $ROS_DISTRO"
echo "[entrypoint] ROS_DOMAIN_ID: $ROS_DOMAIN_ID"

# ─────────────────────────────────────────────────────────────
# STEP 1: Source ROS2 environment
# ─────────────────────────────────────────────────────────────

if [ ! -f "/opt/ros/${ROS_DISTRO}/setup.bash" ]; then
    echo "[entrypoint] ❌ ERROR: ROS2 ${ROS_DISTRO} installation not found"
    echo "[entrypoint]    Expected: /opt/ros/${ROS_DISTRO}/setup.bash"
    exit 1
fi

set +e
source "/opt/ros/${ROS_DISTRO}/setup.bash" 2>/dev/null
ROS_SETUP_RESULT=$?
set -e

if [ $ROS_SETUP_RESULT -ne 0 ]; then
    echo "[entrypoint] ⚠️  Warning: ROS2 setup returned code $ROS_SETUP_RESULT (usually non-critical)"
fi

# ─────────────────────────────────────────────────────────────
# STEP 2: Verify workspace is built
# ─────────────────────────────────────────────────────────────

if [ ! -f "/muto_ws/install/setup.bash" ]; then
    echo "[entrypoint] ❌ CRITICAL ERROR: Workspace not built"
    echo "[entrypoint]    Expected: /muto_ws/install/setup.bash"
    echo "[entrypoint]"
    echo "[entrypoint]    This is a PRODUCTION image. The workspace MUST be"
    echo "[entrypoint]    pre-built during the docker build process."
    echo "[entrypoint]"
    echo "[entrypoint]    Check the image build logs for compilation errors."
    exit 1
fi

echo "[entrypoint] ✓ Workspace verified at /muto_ws"

# ─────────────────────────────────────────────────────────────
# STEP 3: Source built workspace
# ─────────────────────────────────────────────────────────────

set +e
source "/muto_ws/install/setup.bash" 2>/dev/null
WS_SETUP_RESULT=$?
set -e

if [ $WS_SETUP_RESULT -ne 0 ]; then
    echo "[entrypoint] ⚠️  Warning: Workspace setup returned code $WS_SETUP_RESULT"
fi

echo "[entrypoint] ✅ Environment ready"
echo ""

# ─────────────────────────────────────────────────────────────
# STEP 4: Execute command or start bash
# ─────────────────────────────────────────────────────────────

exec "$@"
