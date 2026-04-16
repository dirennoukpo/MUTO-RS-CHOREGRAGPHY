##
## provision_muto_rs.sh for MUTO-RS-CHOREGRAGPHY [WSL: Ubuntu] in /home/edwin/MUTO-RS-CHOREGRAGPHY/scripts
##
## Made by dirennoukpo
## Login   <diren.noukpo@epitech.eu>
##
## Started on  Thu Apr 16 11:21:23 AM 2026 dirennoukpo
## Last update Fri Apr 16 11:54:09 AM 2026 dirennoukpo
##

#!/bin/bash
set -e

echo "🤖 Provisioning MecaMate MUTO_RS..."

# Charger les variables du .env
ENV_FILE="$HOME/muto_rs/config/.env.muto_rs"
if [ ! -f "$ENV_FILE" ]; then
    echo "❌ Missing $ENV_FILE"
    exit 1
fi
export $(grep -v '^#' "$ENV_FILE" | xargs)

# Pre-flight checks
if [ "$EUID" -eq 0 ]; then
    echo "❌ Do not run as root"
    exit 1
fi

if ! grep -q "Raspberry Pi 5" /proc/device-tree/model 2>/dev/null; then
    echo "⚠️ Not a Raspberry Pi 5"
    exit 1
fi

if ! ping -c 1 google.com &> /dev/null; then
    echo "❌ No internet connection"
    exit 1
fi

echo "✓ Pre-checks passed"

# Enable hardware interfaces
echo "⚙️ Configuring interfaces..."
sudo raspi-config nonint do_serial 0
sudo raspi-config nonint do_i2c 0
echo "✓ Serial, I2C, GPIO enabled"

# Install Docker
echo "🐳 Installing Docker..."
if ! command -v docker &> /dev/null; then
    curl -fsSL https://get.docker.com -o /tmp/get-docker.sh
    sudo sh /tmp/get-docker.sh
    rm /tmp/get-docker.sh
fi
sudo usermod -aG docker $USER
sudo systemctl enable docker
echo "✓ Docker ready"

# Hardware permissions
sudo usermod -aG gpio,i2c,dialout $USER
echo "✓ Hardware access configured"

# Install Tailscale
echo "🌐 Installing Tailscale..."
if ! command -v tailscale &> /dev/null; then
    curl -fsSL https://tailscale.com/install.sh | sh
fi

# Restrict Tailscale to current user
sudo tee /etc/sudoers.d/tailscale-restrict > /dev/null <<EOF
$USER ALL=(ALL) NOPASSWD: /usr/bin/tailscale
EOF
sudo chmod 440 /etc/sudoers.d/tailscale-restrict

# Utiliser directement les variables du .env.muto_rs
if [ -z "$TS_AUTHKEY" ] || [ -z "$TS_HOSTNAME" ]; then
    echo "❌ Missing TS_AUTHKEY or TS_HOSTNAME in .env.muto_rs"
    exit 1
fi

sudo tailscale up \
    --authkey="$TS_AUTHKEY" \
    --hostname="$TS_HOSTNAME" \
    --accept-routes="$TS_ACCEPT_ROUTES" \
    --advertise-exit-node="$TS_ADVERTISE_EXIT_NODE"

echo "✓ Tailscale connected"

# Setup deployment structure
echo "📦 Setting up deployment..."
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
mkdir -p "$HOME/muto_rs"/{docker,config} "$HOME/mecamate_rosbag" "$HOME/mecamate_logs"

# Copy files maintaining directory structure
cp "$SCRIPT_DIR/docker-compose.muto_rs.yml" "$HOME/muto_rs/docker/" 2>/dev/null || \
    cp "$SCRIPT_DIR/../docker/docker-compose.muto_rs.yml" "$HOME/muto_rs/docker/"
cp "$SCRIPT_DIR/.env.muto_rs.example" "$HOME/muto_rs/config/.env.muto_rs" 2>/dev/null || \
    cp "$SCRIPT_DIR/../config/.env.muto_rs.example" "$HOME/muto_rs/config/.env.muto_rs"
cp "$SCRIPT_DIR/dds_config.xml" "$HOME/muto_rs/config/" 2>/dev/null || \
    cp "$SCRIPT_DIR/../config/dds_config.xml" "$HOME/muto_rs/config/"

echo "✓ Deployment ready at ~/muto_rs"

# Summary
echo ""
echo "✅ Provisioning complete"
echo ""
echo "Next steps:"
echo "  1. Reboot (required for group changes)"
echo "  2. Load Docker image"
echo "  3. Edit ~/muto_rs/config/.env.muto_rs if needed"
echo "  4. Deploy: cd ~/muto_rs && docker compose --env-file config/.env.muto_rs -f docker/docker-compose.muto_rs.yml up -d"
echo ""
