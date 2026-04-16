##
## docker.mk - Docker image building and deployment for MUTO-RS
##
## Made by dirennoukpo
## Login   <diren.noukpo@epitech.eu>
##
## Targets:
##   make build ENV=muto-rs|workstation [PLATFORM=linux/arm64|linux/amd64] [CACHE=0]
##   make send-image SSH_HOST=user@host ENV=muto-rs|workstation
##

.PHONY: build send-image

## Load environment variables (non-critical, may not exist yet)
-include config/.env.muto_rs
-include config/.env.workstation
export

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

.PHONY: build send-image
