SHELL := /bin/bash

COMPOSE_FILE := docker-compose.yaml
ENV_FILE := config/.env
ENV_TEMPLATE := config/.env.example
SERVICE := ros-humble-dev

.PHONY: help env build up down shell logs ps restart clean

help:
	@echo "Docker workflow targets:"
	@echo "  make env     -> copy $(ENV_TEMPLATE) to $(ENV_FILE)"
	@echo "  make build   -> docker compose build"
	@echo "  make up      -> docker compose up -d"
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

build:
	@HOST_UID="$$(id -u)" HOST_GID="$$(id -g)" HOST_USER="$$(id -un)" HOST_GROUP="$$(id -gn)" \
		docker compose --env-file "$(ENV_FILE)" -f "$(COMPOSE_FILE)" build

up:
	@HOST_UID="$$(id -u)" HOST_GID="$$(id -g)" HOST_USER="$$(id -un)" HOST_GROUP="$$(id -gn)" \
		docker compose --env-file "$(ENV_FILE)" -f "$(COMPOSE_FILE)" up -d --force-recreate

down:
	@docker compose --env-file "$(ENV_FILE)" -f "$(COMPOSE_FILE)" down --remove-orphans

shell:
	@docker compose --env-file "$(ENV_FILE)" -f "$(COMPOSE_FILE)" exec "$(SERVICE)" bash

logs:
	@docker compose --env-file "$(ENV_FILE)" -f "$(COMPOSE_FILE)" logs -f

ps:
	@docker compose --env-file "$(ENV_FILE)" -f "$(COMPOSE_FILE)" ps

restart: down up

clean: down
	@docker image prune -f
