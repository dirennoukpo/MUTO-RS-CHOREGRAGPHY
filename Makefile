SHELL := /bin/bash

COMPOSE_FILE := docker-compose.yaml
ENV_FILE := config/.env
ENV_TEMPLATE := config/.env.example
SERVICE := ros-humble-dev
IMAGE := ros-humble-dev:latest

HOST_ENV = HOST_UID="$$(id -u)" HOST_GID="$$(id -g)" HOST_USER="$$(id -un)" HOST_GROUP="$$(id -gn)"
COMPOSE = docker compose --env-file "$(ENV_FILE)" -f "$(COMPOSE_FILE)"

.PHONY: help env check-docker build up up-offline down shell logs ps restart clean

help:
	@echo "Docker workflow targets:"
	@echo "  make env     -> copy $(ENV_TEMPLATE) to $(ENV_FILE)"
	@echo "  make build   -> docker compose build"
	@echo "  make up      -> docker compose up -d"
	@echo "  make up-offline -> docker compose up -d (without build)"
	@echo "  make down    -> docker compose down --remove-orphans"
	@echo "  make shell   -> open bash in $(SERVICE) container"
	@echo "  make logs    -> follow compose logs"
	@echo "  make ps      -> list compose services"
	@echo "  make restart -> down + up"

env:
	@if [ ! -f "$(ENV_TEMPLATE)" ]; then \
		echo "Missing $(ENV_TEMPLATE)"; \
		exit 1; \
	fi
	cp -n "$(ENV_TEMPLATE)" "$(ENV_FILE)" || true
	@if [ -f "$(ENV_FILE)" ]; then \
		echo "Environment file ready: $(ENV_FILE)"; \
	fi

check-docker:
	@if ! docker info >/dev/null 2>&1; then \
		echo "[ERROR] Docker daemon inaccessible from this terminal."; \
		exit 1; \
	fi

build: env check-docker
	@$(HOST_ENV) $(COMPOSE) build

up: env check-docker
	@$(HOST_ENV) $(COMPOSE) up -d --force-recreate

up-offline: env check-docker
	@if ! docker image inspect "$(IMAGE)" >/dev/null 2>&1; then \
		echo "[ERROR] Offline mode requires local image $(IMAGE)."; \
		exit 1; \
	fi
	@$(HOST_ENV) $(COMPOSE) up -d --force-recreate

down:
	@$(COMPOSE) down --remove-orphans

shell:
	@$(COMPOSE) exec "$(SERVICE)" bash

logs:
	@$(COMPOSE) logs -f

ps:
	@$(COMPOSE) ps

restart: down up

clean: down
	@docker image prune -f
