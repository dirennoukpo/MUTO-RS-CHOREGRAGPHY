##
## common.mk - Common Makefile variables and functions for MUTO-RS
##
## Made by dirennoukpo
## Login   <diren.noukpo@epitech.eu>
##
## Shared targets and variables for docker, muto_rs, and workstation makefiles

.PHONY: help check-docker help-all

## ─────────────────────────────────────────────────────────────
## SHARED VARIABLES
## ─────────────────────────────────────────────────────────────

# Docker image names
IMAGE_NAME_MUTO_RS?=muto-rs-robot:latest
IMAGE_NAME_WORKSTATION?=muto-workstation-dev:latest

# Docker and docker-compose detection
DOCKER?=$(shell command -v docker 2>/dev/null)
DOCKER_COMPOSE?=$(shell command -v docker-compose 2>/dev/null || command -v docker 2>/dev/null && echo docker$(SPACE)compose)

# Default target architecture
PLATFORM?=linux/amd64
ARCH?=$(shell echo $(PLATFORM) | cut -d'/' -f2)

## ─────────────────────────────────────────────────────────────
## COMMON TARGETS
## ─────────────────────────────────────────────────────────────

help: ## Show this help message
	@echo "MUTO-RS Development System"
	@echo ""
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  %-25s %s\n", $$1, $$2}'

help-all: help ## Show full help including modules
	@echo ""
	@echo "Additional module targets:"
	@echo ""
	@echo "  Docker Build & Deploy:"
	@echo "    make build [ENV=muto-rs|workstation] [PLATFORM=linux/arm64|linux/amd64] [CACHE=0]"
	@echo "    make send-image [SSH_HOST=user@host] [ENV=muto-rs|workstation]"
	@echo ""
	@echo "  MUTO-RS Robot:"
	@echo "    make provision-muto-rs [SSH_HOST=pi@robot-ip]"
	@echo "    make muto-rs-deploy [SSH_HOST=pi@robot-ip]"
	@echo "    make muto-rs-stop [SSH_HOST=pi@robot-ip]"
	@echo ""
	@echo "  Workstation:"
	@echo "    make provision-workstation"
	@echo "    make workstation-deploy"
	@echo "    make workstation-stop"

check-docker: ## Verify Docker is installed and running
	@if [ -z "$(DOCKER)" ]; then \
		echo "❌ Docker not found. Install Docker: https://docs.docker.com/get-docker/"; \
		exit 1; \
	fi
	@DOCKER_ERROR="$$( $(DOCKER) info 2>&1 >/dev/null )"; \
	if [ $$? -ne 0 ]; then \
		case "$$DOCKER_ERROR" in \
			*"permission denied"*|*"Got permission denied"*) \
				echo "❌ Docker socket permission denied."; \
				if getent group docker | grep -qw "$$USER"; then \
					echo "   Your user is already in the docker group, but this shell is stale."; \
					echo "   Use one of these now:"; \
					echo "   newgrp docker"; \
					echo "   sg docker -c 'make $(MAKECMDGOALS)'"; \
					echo "   Or open a new terminal session."; \
				else \
					echo "   Add your user to the docker group and re-login:"; \
					echo "   sudo usermod -aG docker $$USER"; \
				fi; \
				exit 1; \
				;; \
			*) \
				echo "❌ Docker daemon not running or inaccessible. Start Docker and retry."; \
				[ -n "$$DOCKER_ERROR" ] && echo "   $$DOCKER_ERROR"; \
				exit 1; \
				;; \
		esac; \
	fi
	@echo "✅ Docker is ready"

check-ssh: ## Verify SSH is configured for remote deployments
	@if [ -z "$(SSH_HOST)" ]; then \
		echo "⚠️  SSH_HOST not set. Remote commands will fail."; \
		exit 1; \
	fi
	@if ! ssh -q -o BatchMode=yes -o CheckHostIP=no $(SSH_HOST) exit 2>/dev/null; then \
		echo "❌ Cannot SSH to $(SSH_HOST)"; \
		exit 1; \
	fi
	@echo "✅ SSH connection verified: $(SSH_HOST)"

## ─────────────────────────────────────────────────────────────
## UTILITY VARIABLES (for use in other makefiles)
## ─────────────────────────────────────────────────────────────

SPACE:= 
SPACE+=

# Docker cache control
CACHE_FLAG=$(if $(filter 0,$(CACHE)),--no-cache,)

## ─────────────────────────────────────────────────────────────
## INCLUDES – Load other makefiles
## ─────────────────────────────────────────────────────────────

-include make/docker.mk
-include make/muto_rs.mk
-include make/workstation.mk

.PHONY: help check-docker check-ssh

