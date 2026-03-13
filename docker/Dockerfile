FROM ros:humble-ros-base-jammy

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash-completion \
    build-essential \
    curl \
    git \
    iputils-ping \
    nano \
    python3-colcon-common-extensions \
    python3-pip \
    python3-rosdep \
    python3-vcstool \
    vim \
    && rm -rf /var/lib/apt/lists/*

# Optional non-root user for easier file permissions on host.
ARG USERNAME=ros
ARG GROUPNAME=ros
ARG USER_UID=1000
ARG USER_GID=1000
RUN groupadd --gid ${USER_GID} ${GROUPNAME} \
    && useradd --uid ${USER_UID} --gid ${USER_GID} -m ${USERNAME} \
    && echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} \
    && chmod 0440 /etc/sudoers.d/${USERNAME} \
    && printf '\nsource /opt/ros/humble/setup.bash\nif [ -f /workspace/install/setup.bash ]; then\n  source /workspace/install/setup.bash\nfi\nparse_git_branch() {\n  git rev-parse --abbrev-ref HEAD 2>/dev/null | sed "s/.*/ (git:&)/"\n}\nexport PS1="\\[\\e[1;33m\\][\\h]\\[\\e[0m\\] \\\[\\e[1;32m\\][ros:\\${ROS_DISTRO}]\\[\\e[0m\\] \\\[\\e[1;34m\\]\\w\\[\\e[0m\\]\\[\\e[1;35m\\]\\$(parse_git_branch)\\[\\e[0m\\]\\$ "\n' >> /home/${USERNAME}/.bashrc \
    && chown ${USER_UID}:${USER_GID} /home/${USERNAME}/.bashrc

WORKDIR /workspace

COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

RUN rosdep init || true

USER ${USERNAME}

ENTRYPOINT ["/entrypoint.sh"]
CMD ["bash"]
