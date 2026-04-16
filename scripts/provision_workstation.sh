#!/bin/bash
##
## provision_workstation.sh - Provision MUTO-RS Workstation (Local Development)
##
## Installs Docker using the official repository on Linux, sets up the
## workstation workspace, and prepares optional GPU and Tailscale support.
##

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
. "$SCRIPT_DIR/utils.sh"

log "🎮 Provisioning MUTO-RS Workstation (Local Development)"

# ─────────────────────────────────────────────────────────────
# STEP 1: Pre-flight checks
# ─────────────────────────────────────────────────────────────

log "📋 Running pre-flight checks"
ensure_not_root

# Check OS
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS_TYPE="Linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    OS_TYPE="macOS"
else
    OS_TYPE="Unknown"
fi
log "Detected OS: $OS_TYPE"
ensure_internet
log "✅ Pre-flight checks passed"

# ─────────────────────────────────────────────────────────────
# STEP 2: Setup logging
# ─────────────────────────────────────────────────────────────

LOG_DIR="$HOME/muto_rs_workstation/logs"
mkdir -p "$LOG_DIR"
PROVISION_LOG="$LOG_DIR/provision.log"

exec > >(tee -a "$PROVISION_LOG") 2>&1
log "Starting MUTO-RS workstation provisioning"

# ─────────────────────────────────────────────────────────────
# STEP 3: Create directory structure
# ─────────────────────────────────────────────────────────────

log "📁 Setting up directory structure"

mkdir -p "$HOME/muto_rs_workstation"/{docker,config,scripts}
mkdir -p "$HOME/muto_rs_workstation/data"/{logs,rosbags,datasets}

log "Directory structure created"
log "$HOME/muto_rs_workstation/"

# ─────────────────────────────────────────────────────────────
# STEP 4: Copy configuration files
# ─────────────────────────────────────────────────────────────

log "📦 Copying configuration files"

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJ_ROOT="$(dirname "$SCRIPT_DIR")"

# Copy docker-compose
if [ -f "$PROJ_ROOT/docker/docker-compose.workstation.yml" ]; then
    cp "$PROJ_ROOT/docker/docker-compose.workstation.yml" "$HOME/muto_rs_workstation/docker/"
    log "docker-compose.workstation.yml copied"
fi

# Copy .env configuration
if [ -f "$PROJ_ROOT/config/.env.workstation.example" ]; then
    if [ ! -f "$HOME/muto_rs_workstation/config/.env.workstation" ]; then
        cp "$PROJ_ROOT/config/.env.workstation.example" "$HOME/muto_rs_workstation/config/.env.workstation"
        log ".env.workstation initialized"
    else
        log ".env.workstation already exists (skipping)"
    fi
fi

# Copy DDS config
if [ -f "$PROJ_ROOT/config/dds_config.xml" ]; then
    cp "$PROJ_ROOT/config/dds_config.xml" "$HOME/muto_rs_workstation/config/"
    log "dds_config.xml copied"
fi

# Copy setup_dds script
if [ -f "$PROJ_ROOT/scripts/setup_dds.sh" ]; then
    cp "$PROJ_ROOT/scripts/setup_dds.sh" "$HOME/muto_rs_workstation/scripts/"
    chmod +x "$HOME/muto_rs_workstation/scripts/setup_dds.sh"
    log "setup_dds.sh copied"
fi

# ─────────────────────────────────────────────────────────────
# STEP 5: Install Docker
# ─────────────────────────────────────────────────────────────

log "🐳 Setting up Docker"

if command -v docker &> /dev/null; then
    log "Docker already installed: $(docker --version)"
else
    case "$OS_TYPE" in
        Linux)
            install_docker_official_ubuntu
            sudo usermod -aG docker $USER
            log "Docker installed from official repository"
            warn "You may need to log out and back in for group changes"
            ;;
        macOS)
            log "Please install Docker Desktop for Mac manually: https://docs.docker.com/desktop/install/mac-install/"
            read -p "  Press Enter when Docker is installed..."
            ;;
    esac
fi

if ! docker compose version &> /dev/null; then
    warn "Docker Compose plugin not available in the current shell."
    warn "If needed, install docker-compose-plugin from Docker's official repo."
fi

# ─────────────────────────────────────────────────────────────
# STEP 6: Configure GPU Support (Optional)
# ─────────────────────────────────────────────────────────────

if command -v nvidia-smi &> /dev/null; then
    log "🎮 GPU detected (NVIDIA)"
    
    if ! command -v nvidia-docker &> /dev/null; then
        log "Installing NVIDIA Container Toolkit"
        # Ubuntu/Debian
        distribution=$(. /etc/os-release;echo $ID$VERSION_ID)
        curl -s -L https://nvidia.github.io/nvidia-docker/gpgkey | sudo apt-key add - 2>/dev/null || true
        curl -s -L https://nvidia.github.io/nvidia-docker/$distribution/nvidia-docker.list | \
            sudo tee /etc/apt/sources.list.d/nvidia-docker.list >/dev/null
        sudo apt-get update && sudo apt-get install -y nvidia-docker2
        sudo systemctl restart docker
        log "NVIDIA Container Toolkit installed"
    fi
else
    log "No NVIDIA GPU detected (CPU mode)"
fi

# ─────────────────────────────────────────────────────────────
# STEP 7: Install Tailscale (VPN for multi-robot networks)
# ─────────────────────────────────────────────────────────────

log "🌐 Setting up Tailscale VPN"

if command -v tailscale &> /dev/null; then
    log "Tailscale already installed"
else
    case "$OS_TYPE" in
        Linux)
            log "Installing Tailscale for Linux"
            curl -fsSL https://tailscale.com/install.sh | sh
            log "Tailscale installed"
            ;;
        macOS)
            log "Installing Tailscale for macOS"
            if command -v brew &> /dev/null; then
                brew tap tailscale/tap
                brew install tailscale
                log "Tailscale installed"
            else
                log "Please install Tailscale manually: https://tailscale.com/download/mac"
            fi
            ;;
    esac
fi

# ─────────────────────────────────────────────────────────────
# STEP 8: Final Setup Instructions
# ─────────────────────────────────────────────────────────────

log "✅ Workstation provisioning complete"
log "Next steps: edit ~/muto_rs_workstation/config/.env.workstation, then run make build ENV=workstation"
log "Log saved to: $PROVISION_LOG"

read -p "Logout now? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    pkill -KILL -u $USER
fi
