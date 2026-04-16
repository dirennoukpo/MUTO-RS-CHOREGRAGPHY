##
## workstation.mk for MUTO-RS-CHOREGRAGPHY [WSL: Ubuntu] in /home/edwin/MUTO-RS-CHOREGRAGPHY/make
##
## Made by dirennoukpo
## Login   <diren.noukpo@epitech.eu>
##
## Started on  Thu Apr 16 10:25:21 AM 2026 dirennoukpo
## Last update Fri Apr 16 1:54:42 PM 2026 dirennoukpo
##

.PHONY: provision-workstation workstation-deploy workstation-stop workstation-logs workstation-status workstation-build

## ─────────────────────────────────────────────────────────────
## WORKSTATION TARGETS
## ─────────────────────────────────────────────────────────────

provision-workstation: ## Provision local workstation development environment
	@echo "🎮 Provisioning MUTO-RS Workstation locally..."
	@bash scripts/provision_workstation.sh | tee provision_workstation.log
	@echo "✅ Workstation provisioned"

workstation-build: check-docker ## Build workstation development image locally
	@echo "🏗 Building workstation image for development..."
	$(MAKE) build ENV=workstation PLATFORM=linux/amd64
	@echo "✅ Workstation image ready"

workstation-deploy: check-docker ## Deploy workstation stack locally
	@echo "🚀 Deploying MUTO-RS workstation stack..."
	@if [ ! -f "config/.env.workstation" ]; then \
		echo "⚠️  Creating .env.workstation from example..."; \
		cp config/.env.workstation.example config/.env.workstation; \
	fi
	docker compose --env-file config/.env.workstation -f docker/docker-compose.workstation.yml up -d
	@echo "✅ Workstation stack deployed"
	@echo "   View logs: make workstation-logs"

workstation-stop: ## Stop workstation stack
	@echo "🛑 Stopping MUTO-RS workstation stack..."
	@if [ -f "docker-compose.workstation.yml" ]; then \
		docker compose --env-file config/.env.workstation -f docker/docker-compose.workstation.yml down; \
	else \
		docker compose -f docker/docker-compose.workstation.yml down 2>/dev/null || echo "No running stack"; \
	fi
	@echo "✅ Workstation stack stopped"

workstation-logs: ## View workstation container logs
	@echo "📊 Workstation logs:"
	docker compose --env-file config/.env.workstation -f docker/docker-compose.workstation.yml logs -f --tail=50 muto-workstation

workstation-status: ## Check workstation container status
	@echo "📋 Workstation status:"
	docker compose --env-file config/.env.workstation -f docker/docker-compose.workstation.yml ps
	@echo ""
	@docker ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}" | grep muto-workstation || echo "Container not running"

workstation-shell: ## Open interactive shell in workstation container
	@docker compose --env-file config/.env.workstation -f docker/docker-compose.workstation.yml exec muto-workstation bash

workstation-rviz: ## Launch RViz2 in workstation container
	@echo "Starting RViz2..."
	@docker compose --env-file config/.env.workstation -f docker/docker-compose.workstation.yml exec muto-workstation ros2 launch nav2_bringup rviz_launch.py

.PHONY: provision-workstation workstation-build workstation-deploy workstation-stop workstation-logs workstation-status workstation-shell workstation-rviz
