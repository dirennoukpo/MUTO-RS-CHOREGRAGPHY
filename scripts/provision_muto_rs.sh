#!/bin/bash
##
## provision_muto_rs.sh - Provision MUTO-RS Robot (Raspberry Pi 5)
##
## Installs Docker using Docker's official Debian/Ubuntu repository, configures hardware
## access, installs Tailscale, and prepares the deployment directory layout.
##
## Designed to run on the robot after copying the repository assets to /tmp.
##

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
. "$SCRIPT_DIR/utils.sh"

log "🤖 Provisioning MUTO-RS Robot"

# ─────────────────────────────────────────────────────────────
# STEP 1: Pre-flight checks
# ─────────────────────────────────────────────────────────────

log "📋 Running pre-flight checks"
ensure_not_root

if ! grep -q "Raspberry Pi 5" /proc/device-tree/model 2>/dev/null; then
    warn "This doesn't appear to be a Raspberry Pi 5"
    warn "Continuing anyway. Press Ctrl+C to abort if this is wrong."
    sleep 3
fi

ensure_internet
log "✅ Pre-flight checks passed"

# ─────────────────────────────────────────────────────────────
# STEP 2: Setup logging
# ─────────────────────────────────────────────────────────────

PROVISION_LOG="$HOME/muto_rs_provision.log"
exec > >(tee -a "$PROVISION_LOG") 2>&1
log "Starting MUTO-RS provisioning"

# ─────────────────────────────────────────────────────────────
# STEP 3: Setup deployment structure
# ─────────────────────────────────────────────────────────────

log "📦 Setting up deployment structure"

# Create directories
mkdir -p "$HOME/muto_rs"/{docker,config}
mkdir -p "$HOME/muto_rs_data"/{logs,rosbags,backups}

if [ -f "$SCRIPT_DIR/docker-compose.muto_rs.yml" ]; then
    cp "$SCRIPT_DIR/docker-compose.muto_rs.yml" "$HOME/muto_rs/docker/"
	log "Copied docker-compose.muto_rs.yml"
fi

if [ -f "$SCRIPT_DIR/.env.muto_rs.example" ]; then
    cp "$SCRIPT_DIR/.env.muto_rs.example" "$HOME/muto_rs/config/.env.muto_rs.example"
    log "Copied .env.muto_rs.example"
fi

if [ -f "$SCRIPT_DIR/.env.muto_rs" ]; then
    cp "$SCRIPT_DIR/.env.muto_rs" "$HOME/muto_rs/config/.env.muto_rs"
    log "Copied .env.muto_rs"
elif [ -f "$SCRIPT_DIR/.env.muto_rs.example" ] && [ ! -f "$HOME/muto_rs/config/.env.muto_rs" ]; then
    cp "$SCRIPT_DIR/.env.muto_rs.example" "$HOME/muto_rs/config/.env.muto_rs"
    log "Initialized .env.muto_rs from example"
fi

if [ -f "$SCRIPT_DIR/dds_config.xml" ]; then
    cp "$SCRIPT_DIR/dds_config.xml" "$HOME/muto_rs/config/"
    log "Copied dds_config.xml"
fi

log "✅ Deployment structure ready"

# ─────────────────────────────────────────────────────────────
# STEP 4: Load environment variables
# ─────────────────────────────────────────────────────────────

ENV_FILE="$HOME/muto_rs/config/.env.muto_rs"
if [ ! -f "$ENV_FILE" ]; then
    warn ".env.muto_rs not found; using defaults"
else
    load_env_file "$ENV_FILE"
fi

# ─────────────────────────────────────────────────────────────
# STEP 5: Enable Hardware Interfaces
# ─────────────────────────────────────────────────────────────

log "⚙️ Configuring hardware interfaces"
# Keep UART enabled for hardware, disable serial login shell to avoid interactive prompts.
sudo raspi-config nonint do_serial_hw 0 2>/dev/null || warn "Serial HW enable skipped"
sudo raspi-config nonint do_serial_cons 1 2>/dev/null || warn "Serial console disable skipped"
sudo raspi-config nonint do_i2c 0 2>/dev/null || warn "I2C config skipped"
sudo raspi-config nonint do_ssh 0 2>/dev/null || warn "SSH config skipped"
log "✅ Hardware interfaces configured"

# ─────────────────────────────────────────────────────────────
# STEP 6: Install Docker
# ─────────────────────────────────────────────────────────────

log "🐳 Setting up Docker"

if command -v docker >/dev/null 2>&1; then
	log "Docker already installed: $(docker --version)"
else
    install_docker_official_repo
	log "Docker installed from official repository"
fi

sudo systemctl enable --now docker
sudo usermod -aG docker "$USER" || true
log "Docker daemon enabled and user added to docker group"

# Force group membership to take effect immediately (without logout/in)
# This ensures docker is accessible right after provisioning
if ! newgrp docker <<'GROUPTEST'; then
    docker --version >/dev/null 2>&1
GROUPTEST
    warn "Docker group membership may require logout/in to take effect"
else
    log "Docker group membership activated immediately"
fi

# ─────────────────────────────────────────────────────────────
# STEP 7: Configure Hardware Permissions
# ─────────────────────────────────────────────────────────────

log "🔐 Configuring hardware permissions"

sudo usermod -aG gpio,i2c,dialout $USER || true

log "User groups configured (gpio, i2c, dialout)"
warn "You may need to log out and back in for groups to take effect (or restart SSH session)"

# ─────────────────────────────────────────────────────────────
# STEP 8: Install Tailscale (VPN for multi-robot networks)
# ─────────────────────────────────────────────────────────────

log "🌐 Setting up Tailscale VPN"

if command -v tailscale &> /dev/null; then
    log "Tailscale already installed"
else
    log "Installing Tailscale"
    curl -fsSL https://tailscale.com/install.sh | sh
    log "Tailscale installed"
fi

# Restrict Tailscale sudo access to current user
sudo tee /etc/sudoers.d/muto-tailscale > /dev/null <<EOF
$USER ALL=(ALL) NOPASSWD: /usr/bin/tailscale
EOF
sudo chmod 440 /etc/sudoers.d/muto-tailscale

log "Tailscale sudo access configured"

# Attempt to connect with provided credentials
if [ -n "$TS_AUTHKEY" ] && [ -n "$TS_HOSTNAME" ]; then
    log "Connecting to Tailscale network"
    sudo tailscale up \
        --authkey="$TS_AUTHKEY" \
        --hostname="$TS_HOSTNAME" \
        --accept-routes="${TS_ACCEPT_ROUTES:-false}" \
        --advertise-exit-node="${TS_ADVERTISE_EXIT_NODE:-false}" \
        2>/dev/null && \
        log "Tailscale connected" || \
        warn "Tailscale connection failed. Configure manually or check credentials."
else
    warn "Tailscale credentials not set in .env.muto_rs"
    warn "Configure later: sudo tailscale up --authkey=<key> --hostname=<name>"
fi

# ─────────────────────────────────────────────────────────────
# STEP 9: Final Summary
# ─────────────────────────────────────────────────────────────

log "✅ MUTO-RS Robot provisioning complete"
log "Next steps: log out/in for group changes, review ~/muto_rs/config/.env.muto_rs, then deploy with docker compose"
log "Log saved to: $PROVISION_LOG"
