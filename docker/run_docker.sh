#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"

docker compose down --remove-orphans >/dev/null 2>&1 || true
docker compose up -d --build --force-recreate

docker compose exec ros-humble-dev bash
