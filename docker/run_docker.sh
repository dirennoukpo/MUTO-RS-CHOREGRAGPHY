#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

export HOST_UID="$(id -u)"
export HOST_GID="$(id -g)"
export HOST_USER="$(id -un)"
export HOST_GROUP="$(id -gn)"

docker compose down --remove-orphans >/dev/null 2>&1 || true
docker compose up -d --build --force-recreate

docker compose exec ros-humble-dev bash
