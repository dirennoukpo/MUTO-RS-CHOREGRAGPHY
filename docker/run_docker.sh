#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

OFFLINE_MODE=0
for arg in "$@"; do
	case "$arg" in
	--offline)
		OFFLINE_MODE=1
		;;
	-h|--help)
		echo "Usage: ./docker/run_docker.sh [--offline]"
		echo "  --offline   Lance le container sans build/pull (image locale requise)."
		exit 0
		;;
	esac
done

export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
export HOST_USER="$(id -un)"
export HOST_GROUP="$(id -gn)"

# X11 forwarding for GUI apps (rviz2, rqt) launched in the container.
export DISPLAY="${DISPLAY:-:0}"
export QT_X11_NO_MITSHM=1
if command -v xhost >/dev/null 2>&1; then
	xhost +local:docker >/dev/null 2>&1 || true
fi

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

if [ "${OFFLINE_MODE}" -eq 1 ]; then
	if ! docker image inspect ros-humble-dev:latest >/dev/null 2>&1; then
		echo "[ERROR] Mode hors-ligne: image locale ros-humble-dev:latest introuvable."
		echo "[FIX] Connecte-toi une fois a Internet puis execute ./docker/run_docker.sh pour builder l'image."
		exit 1
	fi
	echo "[INFO] Mode hors-ligne actif: aucun build/pull ne sera tente."
	docker compose up -d --force-recreate
else
	docker compose up -d --build --force-recreate
fi

docker compose exec ros-humble-dev bash
