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
		echo "  --offline   Lance le container sans build (image locale requise)."
		exit 0
		;;
	esac
done

if [ "${OFFLINE_MODE}" -eq 1 ]; then
	make up-offline
else
	make up
fi

make shell
