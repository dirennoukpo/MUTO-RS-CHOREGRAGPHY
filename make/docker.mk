##
## docker.mk for MUTO-RS-CHOREGRAGPHY [WSL: Ubuntu] in /home/edwin/MUTO-RS-CHOREGRAGPHY/make
##
## Made by dirennoukpo
## Login   <diren.noukpo@epitech.eu>
##
## Started on  Thu Apr 16 12:48:09 PM 2026 dirennoukpo
## Last update Fri Apr 16 1:03:53 PM 2026 dirennoukpo
##

include config/.env.muto_rs
export

build: check-docker ## Build Docker image for MUTO_RS [ENV=base|dev|prod] [PLATFORM=linux/xxx] [CACHE=0]
ifeq ($(ENV),base)
    @echo "🛠 Building base image for $(PLATFORM)..."
    docker buildx build --platform $(PLATFORM) \
        -f docker/Dockerfile.base \
        -t $(IMAGE_NAME_MUTO_RS_BASE) \
        --build-arg ROS_DISTRO=$(ROS_DISTRO) \
        --load \
        $(CACHE_FLAG) .
else ifeq ($(ENV),prod)
    @echo "🛠 Building prod image for $(PLATFORM)..."
    @REQUESTED_ARCH=$$(echo "$(PLATFORM)" | cut -d'/' -f2); \
    if ! docker image inspect $(IMAGE_NAME_MUTO_RS_BASE) >/dev/null 2>&1; then \
        echo "Base image not found. Building base first..."; \
        $(MAKE) build ENV=base PLATFORM=$(PLATFORM) CACHE=$(CACHE); \
    elif [ "$$(docker image inspect $(IMAGE_NAME_MUTO_RS_BASE) --format='{{.Architecture}}')" != "$$REQUESTED_ARCH" ]; then \
        echo "Base image exists but wrong platform (need $$REQUESTED_ARCH). Rebuilding base..."; \
        $(MAKE) build ENV=base PLATFORM=$(PLATFORM) CACHE=$(CACHE); \
    fi
    docker buildx build --platform $(PLATFORM) \
        -f docker/Dockerfile.prod \
        -t $(IMAGE_NAME_MUTO_RS_PROD) \
        --build-arg ROS_DISTRO=$(ROS_DISTRO) \
        --build-arg PRODUCER_TAG=$(PRODUCER_TAG) \
        --build-arg MUTO_RS_IMAGE=$(MUTO_RS_IMAGE) \
        --build-arg BASE_IMAGE=$(IMAGE_NAME_MUTO_RS_BASE) \
        --load \
        $(CACHE_FLAG) .
else
    @echo "🛠 Building dev image for $(PLATFORM)..."
    @REQUESTED_ARCH=$$(echo "$(PLATFORM)" | cut -d'/' -f2); \
    if ! docker image inspect $(IMAGE_NAME_MUTO_RS_PROD) >/dev/null 2>&1; then \
        echo "Prod image not found. Building prod first..."; \
        $(MAKE) build ENV=prod PLATFORM=$(PLATFORM) CACHE=$(CACHE); \
    elif [ "$$(docker image inspect $(IMAGE_NAME_MUTO_RS_PROD) --format='{{.Architecture}}')" != "$$REQUESTED_ARCH" ]; then \
        echo "Prod image exists but wrong platform (need $$REQUESTED_ARCH). Rebuilding prod..."; \
        $(MAKE) build ENV=prod PLATFORM=$(PLATFORM) CACHE=$(CACHE); \
    fi
    docker buildx build --platform $(PLATFORM) \
        -f docker/Dockerfile.dev \
        -t $(IMAGE_NAME_MUTO_RS_DEV) \
        --build-arg ROS_DISTRO=$(ROS_DISTRO) \
        --build-arg PRODUCER_TAG=$(PRODUCER_TAG) \
        --build-arg MUTO_RS_IMAGE=$(MUTO_RS_IMAGE) \
        --build-arg PROD_IMAGE=$(IMAGE_NAME_MUTO_RS_PROD) \
        --load \
        $(CACHE_FLAG) .
endif
    @echo "✅ $(ENV) image built successfully for MUTO_RS"

send-image-all: check-docker ## Send image to all robots in ROBOTS list [ENV=dev|prod]
ifndef ENV
    $(error ❌ ENV required. Usage: make send-image-all ENV=prod)
endif
    @IMAGE=$(IMAGE_NAME_$(shell echo $(ENV) | tr a-z A-Z)); \
    echo "📦 Using image: $$IMAGE"; \
    docker image inspect $$IMAGE >/dev/null 2>&1 || \
        (echo "❌ Image not found. Run 'make build ENV=$(ENV)' first." && exit 1); \
    for host in $(ROBOTS); do \
        echo "🚀 Sending $(ENV) image to $$host..."; \
        if command -v pv >/dev/null 2>&1; then \
            SIZE=$$(docker image inspect $$IMAGE --format='{{.Size}}'); \
            docker save $$IMAGE | pv -s $$SIZE | ssh $$host 'docker load'; \
        else \
            echo "💡 Tip: Install 'pv' for progress display"; \
            docker save $$IMAGE | ssh $$host 'docker load'; \
        fi; \
        echo "✅ Image sent successfully to $$host"; \
    done

send-image: check-docker ## Send image to remote device [SSH_HOST=user@host] [ENV=dev|prod]
ifndef SSH_HOST
	$(error ❌ SSH_HOST required. Usage: make send-image SSH_HOST=user@host ENV=prod)
endif
	@echo "🚀 Sending $(ENV) image to $(SSH_HOST)..."
	@IMAGE=$(IMAGE_NAME_$(shell echo $(ENV) | tr a-z A-Z)); \
	echo "📦 Using image: $$IMAGE"; \
	docker image inspect $$IMAGE >/dev/null 2>&1 || \
		(echo "❌ Image not found. Run 'make build ENV=$(ENV)' first." && exit 1); \
	echo "📤 Transferring image..."; \
	if command -v pv >/dev/null 2>&1; then \
		SIZE=$$(docker image inspect $$IMAGE --format='{{.Size}}'); \
		docker save $$IMAGE | pv -s $$SIZE | ssh $(SSH_HOST) 'docker load'; \
	else \
		echo "💡 Tip: Install 'pv' for progress: brew install pv"; \
		docker save $$IMAGE | ssh $(SSH_HOST) 'docker load'; \
	fi && \
	echo "✅ Image sent successfully"
