# Dossier docker/

## Vue d'Ensemble

Le dossier `docker/` contient toute la configuration nécessaire pour créer et gérer l'environnement de développement containerisé du projet MUTO-RS-CHOREOGRAPHY. Il utilise Docker et Docker Compose pour fournir un environnement ROS 2 Humble reproductible et isolé.

## Structure Générale

```
docker/
├── Dockerfile              # 🐳 Définition de l'image de base
├── entrypoint.sh          # 🚀 Script d'initialisation
├── run_docker.sh          # ⚠️ Script legacy (déprécié)
└── README.md              # 📖 Cette documentation
```

## Architecture Docker

### Image de Base

**Base** : `ros:humble-ros-base-jammy` (Ubuntu 22.04 + ROS 2 Humble)

**Tag** : `ros-humble-dev:latest`

**Architecture** :
- **Multi-stage build** : Optimisation de la taille finale
- **Layering intelligent** : Cache Docker efficace
- **Sécurité** : Utilisateur non-root par défaut

### Services Docker Compose

**Service principal** : `ros-humble-dev`

**Configuration** :
```yaml
services:
  ros-humble-dev:
    build:
      context: ..
      dockerfile: docker/Dockerfile
    image: ros-humble-dev:latest
    container_name: muto-rs-dev
    volumes:
      - ../:/workspace
      - ros-humble-history:/root/.bash_history
      - /tmp/.X11-unix:/tmp/.X11-unix:rw
      - /dev/dri:/dev/dri:rw
      - /tmp/pulse:/tmp/pulse:rw
    devices:
      - /dev/snd:/dev/snd
    environment:
      - ROS_DISTRO=humble
      - ROS_DOMAIN_ID=33
      - RMW_IMPLEMENTATION=rmw_fastrtps_cpp
      - FASTDDS_BUILTIN_TRANSPORTS=UDPv4
    network_mode: host
    stdin_open: true
    tty: true
```

---

## Dockerfile Détaillé

### Étapes de Construction

#### 1. Image de Base
```dockerfile
FROM ros:humble-ros-base-jammy
```

**Pourquoi cette base ?**
- ROS 2 Humble Hawksbill officiel
- Ubuntu 22.04 LTS (support long terme)
- Paquets ROS 2 préinstallés
- Taille optimisée

#### 2. Métadonnées
```dockerfile
LABEL maintainer="Edwin Diren Noukpo <diren.noukpo@epitech.eu>"
LABEL description="Environnement de développement ROS 2 pour MUTO-RS-CHOREOGRAPHY"
LABEL version="2.0"
```

#### 3. Variables d'Environnement
```dockerfile
ENV DEBIAN_FRONTEND=noninteractive
ENV ROS_DISTRO=humble
ENV RMW_IMPLEMENTATION=rmw_fastrtps_cpp
```

**Explications** :
- `DEBIAN_FRONTEND=noninteractive` : Installation silencieuse
- `ROS_DISTRO=humble` : Version ROS 2
- `RMW_IMPLEMENTATION=rmw_fastrtps_cpp` : Middleware DDS

#### 4. Installation Système
```dockerfile
RUN apt-get update && apt-get install -y \
    # Build tools
    cmake \
    build-essential \
    git \
    # Audio processing
    ffmpeg \
    libfftw3-dev \
    libavcodec-dev \
    libsamplerate0-dev \
    libsndfile1-dev \
    # Math libraries
    libblas-dev \
    liblapack-dev \
    gfortran \
    # Python development
    python3-dev \
    python3-pip \
    # ROS 2 GUI
    ros-humble-rviz2 \
    ros-humble-navigation2 \
    ros-humble-behaviortree-cpp-v3 \
 && rm -rf /var/lib/apt/lists/*
```

**Catégories d'outils** :
- **Build** : Compilation C++/Python
- **Audio** : Traitement MP3, analyse spectrale
- **Math** : Algèbre linéaire, FFT
- **Python** : Développement et dépendances
- **ROS 2** : Interfaces graphiques et navigation

#### 5. Installation Python
```dockerfile
RUN pip3 install --no-cache-dir \
    # Audio analysis
    librosa \
    soundfile \
    numba \
    scikit-learn \
    # Advanced audio processing
    madmom \
    aubio \
    essentia \
    # Development tools
    numpy \
    scipy \
    matplotlib
```

**Bibliothèques spécialisées** :
- `librosa` : Analyse audio générale
- `madmom` : Beat tracking professionnel
- `aubio` : Onset detection temps réel
- `essentia` : DSP haute performance

#### 6. Utilisateur et Permissions
```dockerfile
ARG USERNAME=ros
ARG USER_UID=1000
ARG USER_GID=1000

RUN groupadd -g $USER_GID $USERNAME \
 && useradd -m -s /bin/bash -u $USER_UID -g $USER_GID $USERNAME \
 && echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers.d/$USERNAME \
 && chmod 0440 /etc/sudoers.d/$USERNAME

USER $USERNAME
WORKDIR /home/$USERNAME
```

**Sécurité** :
- Utilisateur non-root par défaut
- Sudo disponible si nécessaire
- UID/GID configurables depuis l'hôte

#### 7. Entrypoint
```dockerfile
COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
```

---

## Script d'Entrypoint

### Fonctionnalités

**Fichier** : `docker/entrypoint.sh`

**Rôles** :
- Configuration de l'environnement ROS 2
- Initialisation des variables d'affichage
- Configuration audio PulseAudio
- Gestion des permissions GPU
- Exécution de la commande utilisateur

### Contenu Détaillé

```bash
#!/bin/bash
set -e

# Fonction de logging
log() {
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] ENTRYPOINT: $*" >&2
}

# Configuration ROS 2
log "Configuration ROS 2..."
source /opt/ros/humble/setup.bash

# Variables d'environnement
export ROS_DOMAIN_ID=${ROS_DOMAIN_ID:-33}
export RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION:-rmw_fastrtps_cpp}

# Configuration affichage X11
if [ -n "$DISPLAY" ]; then
    log "Configuration X11 display: $DISPLAY"
    # Configuration X11 pour GUI
fi

# Configuration audio
if [ -d "/tmp/pulse" ]; then
    log "Configuration PulseAudio..."
    # Configuration audio
fi

# Permissions GPU
if [ -d "/dev/dri" ]; then
    log "Configuration GPU access..."
fi

# Exécution de la commande
log "Starting command: $@"
exec "$@"
```

---

## Utilisation avec Makefile

### Workflow Standard

#### 1. Configuration Environnement
```bash
make env
```
**Action** : Copie `config/.env.example` → `config/.env`

**Contenu .env** :
```bash
HOST_UID=1000
HOST_GID=1000
HOST_USER=edwin
HOST_GROUP=edwin
```

#### 2. Construction Image
```bash
make build
```
**Commande Docker** :
```bash
docker build \
  --build-arg USERNAME=${HOST_USER} \
  --build-arg USER_UID=${HOST_UID} \
  --build-arg USER_GID=${HOST_GID} \
  -t ros-humble-dev:latest \
  -f docker/Dockerfile \
  .
```

**Optimisations** :
- Build en parallèle
- Cache des layers
- Arguments build pour personnalisation

#### 3. Démarrage Services
```bash
make up
```
**Options Docker Compose** :
```bash
docker compose up -d --force-recreate
```

#### 4. Accès Shell
```bash
make shell
```
**Commande** :
```bash
docker compose exec ros-humble-dev bash
```

#### 5. Arrêt et Nettoyage
```bash
make down
```
**Actions** :
- Arrêt des containers
- Suppression des volumes temporaires
- Nettoyage des réseaux

---

## Volumes et Montages

### Volumes Persistents

#### Historique Bash
```yaml
volumes:
  - ros-humble-history:/root/.bash_history
```
**Objectif** : Persister l'historique des commandes entre sessions

#### Code Source
```yaml
volumes:
  - ../:/workspace
```
**Montage** : Racine du projet → `/workspace` dans le container

### Montages Runtime

#### Affichage X11
```yaml
volumes:
  - /tmp/.X11-unix:/tmp/.X11-unix:rw
```
**Utilisation** : Interfaces graphiques ROS 2 (RViz, Gazebo)

#### Accès GPU
```yaml
volumes:
  - /dev/dri:/dev/dri:rw
devices:
  - /dev/snd:/dev/snd
```
**Utilisation** : Accélération graphique et audio

#### Audio PulseAudio
```yaml
volumes:
  - /tmp/pulse:/tmp/pulse:rw
```
**Utilisation** : Sortie audio depuis le container

---

## Configuration Réseau

### Mode Réseau

**Configuration** : `network_mode: host`

**Avantages** :
- Accès direct aux interfaces réseau de l'hôte
- Communication ROS 2 transparente
- Pas de translation de ports nécessaire

**Utilisation ROS 2** :
- **Leader** : `ROS_DOMAIN_ID=33`
- **Followers** : `ROS_DOMAIN_ID=42`
- Communication via UDP multicast

### Sécurité Réseau

**Considérations** :
- Isolation réseau entre containers
- Pas d'exposition de ports par défaut
- Communication uniquement via ROS 2 DDS

---

## Optimisations et Bonnes Pratiques

### Cache Docker

#### Layers Optimisés
```dockerfile
# Installation système d'abord (change rarement)
RUN apt-get update && apt-get install -y \
    cmake build-essential git \
 && rm -rf /var/lib/apt/lists/*

# Puis dépendances Python (change plus souvent)
RUN pip3 install --no-cache-dir numpy scipy
```

#### Multi-stage Build
```dockerfile
# Stage de build
FROM ros:humble-ros-base-jammy AS builder
# Compilation lourde ici

# Stage final
FROM ros:humble-ros-base-jammy
# Copie seulement les artefacts nécessaires
```

### Performance

#### Build
- Utilisation de `--no-cache` pour rebuilds complets
- Build en parallèle avec `DOCKER_BUILDKIT=1`

#### Runtime
- Volumes pour éviter la recopie
- `network_mode: host` pour faible latence
- GPU passthrough pour calculs accélérés

### Sécurité

#### Utilisateur Non-root
```dockerfile
USER $USERNAME
```
**Bénéfices** :
- Réduction de la surface d'attaque
- Permissions limitées
- Compatibilité avec outils de développement

#### Sudo Contrôlé
```dockerfile
RUN echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers.d/$USERNAME
```
**Utilisation** : Installation de paquets si nécessaire

---

## Dépannage

### Problèmes Courants

#### Build Lent
```bash
# Forcer rebuild sans cache
make build BUILD_ARGS="--no-cache"
```

#### Permissions Utilisateur
```bash
# Vérifier UID/GID
id -u && id -g

# Reconstruire avec bons IDs
make env
make build
make up
```

#### Affichage X11
```bash
# Autoriser connexions X11
xhost +local:docker

# Puis lancer
make shell
```

#### Audio Non Fonctionnel
```bash
# Vérifier PulseAudio
pactl list sinks

# Redémarrer container
make down && make up
```

### Logs et Debug

#### Logs Docker
```bash
# Logs du container
docker compose logs ros-humble-dev

# Logs avec suivi
docker compose logs -f ros-humble-dev
```

#### Debug Interactif
```bash
# Shell debug
docker compose run --rm ros-humble-dev bash

# Inspection
docker inspect muto-rs-dev
```

### Métriques

#### Taille Image
```bash
docker images ros-humble-dev:latest
```

#### Performance Build
```bash
time make build
```

---

## Évolution et Maintenance

### Mises à Jour

#### ROS 2 Versions
```dockerfile
# Passage à Iron
FROM ros:iron-ros-base-jammy
ENV ROS_DISTRO=iron
```

#### Nouvelles Dépendances
```dockerfile
RUN apt-get install -y \
    ros-humble-new-package \
    additional-tools
```

### CI/CD Integration

#### GitHub Actions
```yaml
- name: Build Docker Image
  run: make build

- name: Run Tests
  run: |
    make up
    docker compose exec -T ros-humble-dev bash -c "
      source /opt/ros/humble/setup.bash
      colcon test
    "
```

#### Registry Privé
```yaml
services:
  ros-humble-dev:
    image: registry.company.com/ros-humble-dev:latest
```

---

## Alternatives et Comparaisons

### vs. Installation Native

**Docker** :
- ✅ Environnement reproductible
- ✅ Isolation complète
- ✅ Facilité de partage
- ❌ Overhead de virtualisation

**Native** :
- ✅ Performance maximale
- ✅ Intégration système
- ❌ Configuration complexe
- ❌ Conflits de dépendances

### vs. Autres Outils

#### Docker Compose vs. Docker Run
- **Compose** : Gestion multi-services, configuration déclarative
- **Run** : Plus simple pour cas d'usage unique

#### Docker vs. Podman
- **Podman** : Alternative sans daemon, compatibilité OCI
- **Docker** : Écosystème mature, outils complets

---

*Le dossier `docker/` fournit l'infrastructure essentielle pour un développement ROS 2 cohérent et efficace dans le projet MUTO-RS-CHOREOGRAPHY.*</content>
<parameter name="filePath">/home/edwin/TEKBOT-ROBOTICS-BENIN/MUTO-RS-CHOREGRAGPHY/docker/README.md

## Fichiers du dossier

- Dockerfile: construction de l'image de dev ROS 2
- entrypoint.sh: preparation de l'environnement au demarrage
- run_docker.sh: script de lancement historique

## Notes utiles

- Le service Docker Compose est nomme `ros-humble-dev`.
- Le workspace local est monte dans le container sur `/workspace`.
- Les UID/GID utilisateur hote sont passes automatiquement via le Makefile.
- La variable `ROS_DOMAIN_ID` se configure dans `config/.env`.