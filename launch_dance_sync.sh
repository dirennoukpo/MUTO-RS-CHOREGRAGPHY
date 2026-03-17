#!/usr/bin/env bash
# launch_dance_sync.sh
#
# Alternative simple approach: SSH into each robot simultaneously and launch
# dance_demo.py at exactly the same time (no ROS2 topic needed).
#
# All robots execute the same script with the same parameters → they stay
# in sync because the choreography is deterministic and time-based.
#
# Usage:
#   chmod +x launch_dance_sync.sh
#   ./launch_dance_sync.sh
#   ./launch_dance_sync.sh --loops 2 --speed 2 --beat 0.9
#
# Requirements:
#   - sshpass installed  (sudo apt install sshpass)
#   - dance_demo.py already copied to /home/pi/ on each robot
#   - Robots reachable on the LAN

set -euo pipefail

# ── Configuration ────────────────────────────────────────────────────────────
ROBOTS=(
    "pi@192.168.1.136"
    # "pi@192.168.1.137"   # add more robots here
)
PASSWORD="yahboom"
REMOTE_SCRIPT="/home/pi/dance_demo.py"

LOOPS=1
SPEED=2
STEP_WIDTH=16
BEAT=1.0
# ─────────────────────────────────────────────────────────────────────────────

# Parse optional CLI overrides
while [[ $# -gt 0 ]]; do
    case $1 in
        --loops)      LOOPS="$2";      shift 2 ;;
        --speed)      SPEED="$2";      shift 2 ;;
        --step-width) STEP_WIDTH="$2"; shift 2 ;;
        --beat)       BEAT="$2";       shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

CMD="python3 ${REMOTE_SCRIPT} --loops ${LOOPS} --speed ${SPEED} --step-width ${STEP_WIDTH} --beat ${BEAT}"

echo "=== MUTO-RS Dance Sync Launcher ==="
echo "Robots   : ${ROBOTS[*]}"
echo "Command  : ${CMD}"
echo ""

# Copy dance_demo.py to each robot (skip if already there)
echo "─── Deploying dance_demo.py to robots..."
for ROBOT in "${ROBOTS[@]}"; do
    echo "  → scp dance_demo.py to ${ROBOT}:${REMOTE_SCRIPT}"
    sshpass -p "${PASSWORD}" scp -o StrictHostKeyChecking=no \
        "$(dirname "$0")/dance_demo.py" \
        "${ROBOT}:${REMOTE_SCRIPT}" &
done
wait
echo "  ✓ Deploy done"
echo ""

# Launch dance on all robots simultaneously
echo "─── Launching choreography on all robots simultaneously..."
PIDS=()
for ROBOT in "${ROBOTS[@]}"; do
    echo "  → Starting on ${ROBOT}"
    sshpass -p "${PASSWORD}" ssh -o StrictHostKeyChecking=no \
        "${ROBOT}" "${CMD}" &
    PIDS+=($!)
done

echo ""
echo "  ✓ Dance running on ${#ROBOTS[@]} robot(s). Press Ctrl+C to stop all."
echo ""

# Wait for all to finish (or Ctrl+C)
for PID in "${PIDS[@]}"; do
    wait "${PID}" || true
done

echo "=== Choreography finished ==="
