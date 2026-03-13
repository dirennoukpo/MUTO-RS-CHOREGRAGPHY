#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
export HOST_USER="$(id -un)"
export HOST_GROUP="$(id -gn)"

if ! docker info >/dev/null 2>&1; then
	echo "[ERROR] Impossible d'acceder au daemon Docker depuis ce terminal."
	echo "[INFO] Cause frequente: terminal ouvert avant l'ajout au groupe 'docker'."
	echo "[FIX] Execute: newgrp docker"
	echo "[FIX] Puis relance: ./docker/run_docker.sh"
	exit 1
fi

docker compose down --remove-orphans >/dev/null 2>&1 || true
docker compose up -d --build --force-recreate

docker compose exec ros-humble-dev bash
