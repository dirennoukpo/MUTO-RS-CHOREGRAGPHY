##
## muto_rs.mk - MUTO-RS robot deployment targets
##
## Made by dirennoukpo
## Login   <diren.noukpo@epitech.eu>
##

-include config/.env.muto_rs

ROBOTS ?= $(ROBOT_LIST)

.PHONY: provision-all provision-muto-rs muto-rs-deploy muto-rs-stop muto-rs-logs muto-rs-status

provision-all: ## Provision all robots listed in ROBOTS
	@if [ -z "$(ROBOTS)" ]; then echo "❌ ROBOTS not set"; exit 1; fi
	@for host in $(ROBOTS); do echo "🤖 Provisioning $$host"; done

provision-muto-rs: ## Provision a MUTO-RS robot remotely [SSH_HOST=user@host]
	@if [ -z "$(SSH_HOST)" ]; then echo "❌ SSH_HOST required"; exit 1; fi
	@echo "🤖 Provisioning MUTO-RS at $(SSH_HOST)..."
	@test -f scripts/provision_muto_rs.sh || exit 1
	@test -f scripts/utils.sh || exit 1
	@ssh $(SSH_HOST) 'mkdir -p /tmp/muto_provision'
	@scp -q scripts/utils.sh $(SSH_HOST):/tmp/muto_provision/
	@scp -q scripts/provision_muto_rs.sh $(SSH_HOST):/tmp/muto_provision/
	@scp -q docker/docker-compose.muto_rs.yml $(SSH_HOST):/tmp/muto_provision/
	@scp -q config/.env.muto_rs.example $(SSH_HOST):/tmp/muto_provision/
	@if [ -f config/.env.muto_rs ]; then \
		scp -q config/.env.muto_rs $(SSH_HOST):/tmp/muto_provision/; \
	fi
	@scp -q config/dds_config.xml $(SSH_HOST):/tmp/muto_provision/
	@ssh -t $(SSH_HOST) 'cd /tmp/muto_provision && bash provision_muto_rs.sh'
	@ssh $(SSH_HOST) 'rm -rf /tmp/muto_provision'
	@echo "✅ Remote provisioning complete"

muto-rs-deploy: ## Deploy the MUTO-RS stack [SSH_HOST=user@host]
	@if [ -z "$(SSH_HOST)" ]; then echo "❌ SSH_HOST required"; exit 1; fi
	@echo "🚀 Deploying MUTO-RS stack at $(SSH_HOST)..."
	@ssh $(SSH_HOST) 'docker image inspect $(MUTO_RS_IMAGE):$(IMAGE_ENV) >/dev/null 2>&1 || (echo "❌ Image missing on robot: $(MUTO_RS_IMAGE):$(IMAGE_ENV)"; echo "   Run from workstation: make send-image SSH_HOST=$(SSH_HOST) ENV=muto-rs"; exit 1)'
	@ssh $(SSH_HOST) 'cd ~/muto_rs && MUTO_RS_IMAGE=$(MUTO_RS_IMAGE) IMAGE_ENV=$(IMAGE_ENV) docker compose --env-file config/.env.muto_rs -f docker/docker-compose.muto_rs.yml up -d'

muto-rs-stop: ## Stop the MUTO-RS stack [SSH_HOST=user@host]
	@if [ -z "$(SSH_HOST)" ]; then echo "❌ SSH_HOST required"; exit 1; fi
	@echo "🛑 Stopping MUTO-RS stack at $(SSH_HOST)..."
	@ssh $(SSH_HOST) 'cd ~/muto_rs && MUTO_RS_IMAGE=$(MUTO_RS_IMAGE) IMAGE_ENV=$(IMAGE_ENV) docker compose --env-file config/.env.muto_rs -f docker/docker-compose.muto_rs.yml down'

muto-rs-logs: ## Show MUTO-RS stack logs [SSH_HOST=user@host]
	@if [ -z "$(SSH_HOST)" ]; then echo "❌ SSH_HOST required"; exit 1; fi
	@ssh $(SSH_HOST) 'cd ~/muto_rs && MUTO_RS_IMAGE=$(MUTO_RS_IMAGE) IMAGE_ENV=$(IMAGE_ENV) docker compose --env-file config/.env.muto_rs -f docker/docker-compose.muto_rs.yml logs -f --tail=100'

muto-rs-status: ## Show MUTO-RS stack status [SSH_HOST=user@host]
	@if [ -z "$(SSH_HOST)" ]; then echo "❌ SSH_HOST required"; exit 1; fi
	@ssh $(SSH_HOST) 'cd ~/muto_rs && MUTO_RS_IMAGE=$(MUTO_RS_IMAGE) IMAGE_ENV=$(IMAGE_ENV) docker compose --env-file config/.env.muto_rs -f docker/docker-compose.muto_rs.yml ps'
