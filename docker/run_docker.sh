#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
export HOST_USER="$(id -un)"
export HOST_GROUP="$(id -gn)"

if ! docker info >/dev/null 2>&1; then
	echo "[ERROR] Impossible d'acceder au daemon Docker depuis ce terminal."

	if ! id -nG "${USER}" | grep -qw docker; then
		echo "[INFO] L'utilisateur '${USER}' n'est pas dans le groupe docker."
		echo "[FIX] Tentative d'ajout automatique via sudo usermod -aG docker ${USER}"
		sudo usermod -aG docker "${USER}"
		echo "[INFO] Ajout effectue. Deconnecte-toi puis reconnecte-toi (ou redemarre la VM)."
		echo "[INFO] Ensuite relance: ./docker/run_docker.sh"
		exit 1
	fi

	echo "[INFO] Tu es deja dans le groupe docker, mais la session n'a pas pris en compte le changement."
	echo "[FIX] Deconnecte-toi puis reconnecte-toi (ou redemarre la VM)."
	echo "[FIX] Puis relance: ./docker/run_docker.sh"
	exit 1
fi

docker compose down --remove-orphans >/dev/null 2>&1 || true
docker compose up -d --build --force-recreate

docker compose exec ros-humble-dev bash
