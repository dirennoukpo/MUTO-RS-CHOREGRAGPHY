FROM ros:humble-ros-base-jammy

ENV DEBIAN_FRONTEND=noninteractive

# -------------------------------------------------
# SYSTEM DEPENDENCIES (CRITICAL FOR AUDIO ANALYSIS)
# -------------------------------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
    bash-completion \
    build-essential \
    cmake \
    git \
    curl \
    wget \
    nano \
    vim \
    unzip \
    ffmpeg \
    libfftw3-dev \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswresample-dev \
    libsamplerate0-dev \
    libsndfile1-dev \
    libblas-dev \
    liblapack-dev \
    gfortran \
    python3-dev \
    python3-pip \
    python3-setuptools \
    python3-wheel \
    python3-colcon-common-extensions \
    python3-rosdep \
    python3-vcstool \
    ros-humble-rviz2 \
    ros-humble-navigation2 \
    ros-humble-nav2-bringup \
    ros-humble-behaviortree-cpp-v3 \
    ros-humble-rmw-fastrtps-cpp \
    && rm -rf /var/lib/apt/lists/*

# -------------------------------------------------
# PYTHON CORE UPGRADE (IMPORTANT)
# -------------------------------------------------
RUN pip install --upgrade pip setuptools wheel

# -------------------------------------------------
# REQUIRED FOR MADMOM (FIX YOUR ERROR)
# -------------------------------------------------
RUN pip install cython numpy scipy

# -------------------------------------------------
# AUDIO ANALYSIS LIBRARIES
# -------------------------------------------------
RUN pip install \
    librosa \
    soundfile \
    numba \
    scikit-learn

# -------------------------------------------------
# AUBIO (FAST ONSETS)
# -------------------------------------------------
RUN pip install aubio

# -------------------------------------------------
# FIX MADMOM (IMPORTANT)
# -------------------------------------------------

# 1. Installer dépendances AVANT build
RUN pip install --upgrade pip setuptools wheel

RUN pip install \
    cython==0.29.36 \
    numpy \
    scipy

# 2. Installer madmom SANS isolation (CRUCIAL)
RUN pip install --no-build-isolation git+https://github.com/CPJKU/madmom.git

# -------------------------------------------------
# ESSENTIA (BEST ENERGY / SEGMENTS)
# NOTE: wheel exists for Linux → faster install
# -------------------------------------------------
RUN pip install essentia

# -------------------------------------------------
# OPTIONAL USER (GOOD PRACTICE)
# -------------------------------------------------
ARG USERNAME=ros
ARG GROUPNAME=ros
ARG USER_UID=1000
ARG USER_GID=1000

RUN groupadd --gid ${USER_GID} ${GROUPNAME} \
    && useradd --uid ${USER_UID} --gid ${USER_GID} -m ${USERNAME} \
    && echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} \
    && chmod 0440 /etc/sudoers.d/${USERNAME}

# -------------------------------------------------
# ROS ENV + HISTORY
# -------------------------------------------------
RUN mkdir -p /commandhistory \
    && chown ${USER_UID}:${USER_GID} /commandhistory \
    && printf '\nsource /opt/ros/humble/setup.bash\nif [ -f /workspace/install/setup.bash ]; then\n  source /workspace/install/setup.bash\nfi\nexport HISTFILE=/commandhistory/.bash_history\nexport HISTSIZE=5000\nexport HISTFILESIZE=10000\nshopt -s histappend\nPROMPT_COMMAND="history -a${PROMPT_COMMAND:+;$PROMPT_COMMAND}"\n' >> /home/${USERNAME}/.bashrc \
    && chown ${USER_UID}:${USER_GID} /home/${USERNAME}/.bashrc

WORKDIR /workspace

COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

RUN rosdep init || true

USER ${USERNAME}

ENTRYPOINT ["/entrypoint.sh"]
CMD ["bash"]