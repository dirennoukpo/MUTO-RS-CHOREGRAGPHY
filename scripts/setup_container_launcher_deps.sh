#!/usr/bin/env bash
set -euo pipefail

# Installs host dependencies required by docker/run_docker.sh
# - Docker Engine
# - Docker Compose v2 plugin
# - User permissions for non-sudo docker usage

if [[ "${EUID}" -eq 0 ]]; then
  echo "Run this script as your normal user (not root)."
  echo "It will use sudo only when needed."
  exit 1
fi

if ! command -v sudo >/dev/null 2>&1; then
  echo "sudo is required but not installed."
  exit 1
fi

if [[ ! -f /etc/os-release ]]; then
  echo "Cannot detect OS. /etc/os-release not found."
  exit 1
fi

# shellcheck disable=SC1091
source /etc/os-release
DISTRO_ID="${ID:-}"
DISTRO_CODENAME="${VERSION_CODENAME:-}"

if [[ "${DISTRO_ID}" != "ubuntu" ]]; then
  echo "This helper currently supports Ubuntu hosts."
  echo "Detected: ${DISTRO_ID}"
  exit 1
fi

if [[ -z "${DISTRO_CODENAME}" ]]; then
  echo "Could not detect Ubuntu codename."
  exit 1
fi

echo "[1/6] Installing base packages..."
sudo apt-get update
sudo apt-get install -y ca-certificates curl gnupg lsb-release

echo "[2/6] Adding Docker apt repository..."
sudo install -m 0755 -d /etc/apt/keyrings
if [[ ! -f /etc/apt/keyrings/docker.gpg ]]; then
  curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
fi
sudo chmod a+r /etc/apt/keyrings/docker.gpg

echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu ${DISTRO_CODENAME} stable" \
  | sudo tee /etc/apt/sources.list.d/docker.list >/dev/null

echo "[3/6] Installing Docker Engine and Compose plugin..."
sudo apt-get update
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

echo "[4/6] Enabling Docker service..."
sudo systemctl enable docker
sudo systemctl start docker

echo "[5/6] Granting current user access to docker group..."
if ! getent group docker >/dev/null; then
  sudo groupadd docker
fi
sudo usermod -aG docker "${USER}"

echo "[6/6] Normalizing workspace scripts..."
if command -v dos2unix >/dev/null 2>&1; then
  dos2unix docker/*.sh scripts/*.sh >/dev/null 2>&1 || true
fi
chmod +x docker/*.sh scripts/*.sh || true

echo
echo "Done. Next steps:"
echo "1) Restart your shell session (or run: newgrp docker)"
echo "2) Verify: docker compose version"
echo "3) Launch container: ./docker/run_docker.sh"
