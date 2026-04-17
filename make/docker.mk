##
## docker.mk - Docker image building and deployment for MUTO-RS
##
## Made by dirennoukpo
## Login   <diren.noukpo@epitech.eu>
##
## Targets:
##   make build ENV=muto-rs|workstation [PLATFORM=linux/arm64|linux/amd64] [CACHE=0]
##   make send-image SSH_HOST=user@host ENV=muto-rs|workstation
##   make send-image-all ENV=muto-rs [ROBOTS="ip1 ip2"] [SSH_USER=muto]
##

.PHONY: build send-image send-image-all

## Load environment variables (non-critical, may not exist yet)
-include config/.env.muto_rs
-include config/.env.workstation
export

ROBOTS ?= $(strip $(subst ",,$(ROBOT_LIST)))
SSH_USER ?= muto

## ─────────────────────────────────────────────────────────────
## BUILD VARIABLES
## ─────────────────────────────────────────────────────────────

BUILD_DATE:=$(shell date -u +'%Y-%m-%dT%H:%M:%SZ')
GIT_COMMIT:=$(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
ROS_DISTRO?=humble
PRODUCER_TAG?=muto-dev
PLATFORM?=linux/amd64

## Derived from ENV parameter
ifeq ($(ENV),muto-rs)
DOCKERFILE:=docker/Dockerfile.muto_rs
IMAGE_NAME:=$(IMAGE_NAME_MUTO_RS)
BUILD_ARCH:=linux/arm64
else ifeq ($(ENV),workstation)
DOCKERFILE:=docker/Dockerfile.workstation
IMAGE_NAME:=$(IMAGE_NAME_WORKSTATION)
BUILD_ARCH:=linux/amd64
else
DOCKERFILE:=
IMAGE_NAME:=
BUILD_ARCH:=$(PLATFORM)
endif

## ─────────────────────────────────────────────────────────────
## BUILD TARGET
## ─────────────────────────────────────────────────────────────

build: check-docker ## Build Docker image [ENV=muto-rs|workstation] [PLATFORM=linux/xxx] [CACHE=0]
ifndef ENV
	@echo "❌ ENV required. Usage: make build ENV=muto-rs|workstation [PLATFORM=linux/arm64|linux/amd64]"
	@echo ""
	@echo "Examples:"
	@echo "  make build ENV=muto-rs PLATFORM=linux/arm64    # Build for Raspberry Pi"
	@echo "  make build ENV=workstation PLATFORM=linux/amd64 # Build for local machine"
	@exit 1
endif
ifeq ($(DOCKERFILE),)
	@echo "❌ Unknown ENV: $(ENV)"
	@echo "   Valid options: muto-rs, workstation"
	@exit 1
endif

	@echo "🛠 Building $(ENV) image for $(PLATFORM)..."
	@echo "   Dockerfile: $(DOCKERFILE)"
	@echo "   Image:      $(IMAGE_NAME)"
	@echo "   Platform:   $(PLATFORM)"
	@echo "   ROS_DISTRO: $(ROS_DISTRO)"
	@echo "   Git:        $(GIT_COMMIT)"
	@HOST_ARCH="$$(uname -m)"; \
	TARGET_ARCH="$$(echo "$(PLATFORM)" | cut -d/ -f2)"; \
	if [ "$$HOST_ARCH" = "x86_64" ]; then HOST_ARCH="amd64"; fi; \
	if [ "$$HOST_ARCH" = "aarch64" ]; then HOST_ARCH="arm64"; fi; \
	if [ "$$HOST_ARCH" != "$$TARGET_ARCH" ]; then \
		echo "🔧 Cross-build detected ($$HOST_ARCH -> $$TARGET_ARCH), enabling QEMU/binfmt..."; \
		OK=0; \
		for i in 1 2 3; do \
			echo "   Attempt $$i/3: installing binfmt handlers"; \
			docker pull tonistiigi/binfmt:latest >/dev/null 2>&1 || true; \
			if docker run --privileged --rm tonistiigi/binfmt --install all >/dev/null 2>&1; then \
				OK=1; \
				break; \
			fi; \
		done; \
		if [ "$$OK" -ne 1 ]; then \
			echo "❌ Failed to install QEMU/binfmt after 3 attempts."; \
			echo "   This is usually a transient network issue (e.g. TLS bad record MAC)."; \
			echo "   Retry the build or pre-pull manually: docker pull tonistiigi/binfmt:latest"; \
			exit 1; \
		fi; \
	fi; \
	if ! docker buildx inspect muto-builder >/dev/null 2>&1; then \
		echo "🔧 Creating dedicated buildx builder: muto-builder"; \
		docker buildx create --name muto-builder --driver docker-container --use >/dev/null; \
	else \
		docker buildx use muto-builder >/dev/null; \
	fi; \
	docker buildx inspect --bootstrap >/dev/null

	docker buildx build \
		--platform $(PLATFORM) \
		-f $(DOCKERFILE) \
		-t $(IMAGE_NAME) \
		--build-arg ROS_DISTRO=$(ROS_DISTRO) \
		--build-arg PRODUCER_TAG=$(PRODUCER_TAG) \
		--build-arg BUILD_DATE=$(BUILD_DATE) \
		--build-arg GIT_COMMIT=$(GIT_COMMIT) \
		--load \
		$(CACHE_FLAG) .

	@echo "✅ $(ENV) image built successfully"
	@echo "   Run with: docker run -it --rm $(IMAGE_NAME) bash"

## ─────────────────────────────────────────────────────────────
## SEND IMAGE TO REMOTE HOST
## ─────────────────────────────────────────────────────────────

send-image: check-docker check-ssh ## Send image to remote device [SSH_HOST=user@host] [ENV=muto-rs|workstation]
	@if [ -z "$(ENV)" ]; then \
		echo "❌ ENV required. Usage: make send-image SSH_HOST=user@host ENV=muto-rs|workstation"; \
		exit 1; \
	fi
	@case "$(ENV)" in \
		muto-rs) IMAGE_TO_SEND="$(IMAGE_NAME_MUTO_RS)" ;; \
		workstation) IMAGE_TO_SEND="$(IMAGE_NAME_WORKSTATION)" ;; \
		*) echo "❌ Unknown ENV: $(ENV)"; exit 1 ;; \
	esac; \
	echo "🚀 Sending $(ENV) image to $(SSH_HOST)..."; \
	echo "   Image: $$IMAGE_TO_SEND"; \
	docker image inspect "$$IMAGE_TO_SEND" >/dev/null 2>&1 || \
		(echo "❌ Image not found: $$IMAGE_TO_SEND" && \
		 echo "   Run: make build ENV=$(ENV)" && exit 1); \
	echo "📤 Transferring image (this may take a few minutes)..."; \
	if command -v pv >/dev/null 2>&1; then \
		SIZE=$$(docker image inspect "$$IMAGE_TO_SEND" --format='{{.Size}}'); \
		docker save "$$IMAGE_TO_SEND" | pv -s $$SIZE --width 50 | ssh $(SSH_HOST) 'docker load'; \
	else \
		docker save "$$IMAGE_TO_SEND" | ssh $(SSH_HOST) 'docker load'; \
		echo "💡 Install 'pv' for progress: sudo apt-get install pv"; \
	fi; \
	echo "✅ Image sent successfully to $(SSH_HOST)"; \
	echo "   Load on remote: ssh $(SSH_HOST) 'docker images | grep $(ENV)'"

send-image-all: check-docker ## Send image to all robots listed in ROBOTS [ENV=muto-rs] [ROBOTS="ip1 ip2"]
	@if [ -z "$(ENV)" ]; then \
		echo "❌ ENV required. Usage: make send-image-all ENV=muto-rs|workstation"; \
		exit 1; \
	fi
	@if [ -z "$(ROBOTS)" ]; then echo "❌ ROBOTS not set"; exit 1; fi
	@failed_hosts=""; \
	for host in $(ROBOTS); do \
		ssh_host="$$host"; \
		case "$$host" in *@*) ;; *) ssh_host="$(SSH_USER)@$$host" ;; esac; \
		echo "📤 Sending $(ENV) image to $$ssh_host"; \
		if ! $(MAKE) send-image SSH_HOST=$$ssh_host ENV=$(ENV); then \
			failed_hosts="$$failed_hosts $$ssh_host"; \
		fi; \
	done; \
	if [ -n "$$failed_hosts" ]; then \
		echo "❌ Image transfer failed for:$$failed_hosts"; \
		exit 1; \
	fi

.PHONY: build send-image send-image-all
