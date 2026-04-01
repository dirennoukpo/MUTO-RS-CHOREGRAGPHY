# Dossier config/

## Vue d'Ensemble

Le dossier `config/` contient tous les fichiers de configuration du projet MUTO-RS-CHOREOGRAPHY. Ces configurations permettent de personnaliser le comportement du système selon l'environnement de déploiement et les besoins spécifiques.

## Structure Générale

```
config/
├── .env.example          # 📋 Template variables environnement
└── .env                  # 🔧 Variables environnement (généré)
```

## Fichiers de Configuration

### 📋 .env.example (Template)

**Rôle** : Template des variables d'environnement pour Docker.

**Contenu standard** :
```bash
# Configuration Docker et utilisateur
HOST_UID=1000
HOST_GID=1000
HOST_USER=username
HOST_GROUP=groupname

# Configuration ROS 2
ROS_DOMAIN_ID=33
DISPLAY=:0

# Configuration audio (optionnel)
PULSE_SERVER=unix:/tmp/pulse/native

# Configuration GPU (optionnel)
LIBGL_ALWAYS_SOFTWARE=0
```

**Variables détaillées** :

#### Variables Utilisateur
- `HOST_UID` : UID de l'utilisateur hôte (pour permissions fichiers)
- `HOST_GID` : GID de l'utilisateur hôte
- `HOST_USER` : Nom d'utilisateur hôte
- `HOST_GROUP` : Nom du groupe hôte

#### Variables ROS 2
- `ROS_DOMAIN_ID` : Domaine DDS pour isolation réseau (défaut: 33)
- `RMW_IMPLEMENTATION` : Middleware ROS 2 (défaut: rmw_fastrtps_cpp)

#### Variables Affichage
- `DISPLAY` : Display X11 pour interfaces graphiques

#### Variables Audio
- `PULSE_SERVER` : Serveur PulseAudio pour sortie audio

#### Variables GPU
- `LIBGL_ALWAYS_SOFTWARE` : Forcer rendu logiciel (0=hardware, 1=software)

### 🔧 .env (Généré)

**Rôle** : Fichier de configuration actif utilisé par Docker Compose.

**Génération** :
```bash
make env
# Copie .env.example → .env avec substitution automatique
```

**Contenu personnalisé** :
```bash
HOST_UID=1001
HOST_GID=1001
HOST_USER=edwin
HOST_GROUP=developers
ROS_DOMAIN_ID=42
DISPLAY=:1
```

---

## Utilisation des Configurations

### Workflow Standard

#### 1. Initialisation
```bash
make env
```
**Action** : Crée `.env` depuis `.env.example`

#### 2. Personnalisation
```bash
# Éditer .env selon les besoins
nano config/.env
```

#### 3. Application
```bash
# Reconstruire l'image avec nouvelles variables
make build
make up
```

### Configuration par Environnement

#### Développement Local
```bash
# config/.env
HOST_UID=1000
HOST_GID=1000
ROS_DOMAIN_ID=33
DISPLAY=:0
```

#### Serveur CI/CD
```bash
# config/.env.ci
HOST_UID=1001
HOST_GID=1001
ROS_DOMAIN_ID=0
DISPLAY=
LIBGL_ALWAYS_SOFTWARE=1
```

#### Robot de Production
```bash
# config/.env.prod
HOST_UID=1002
HOST_GID=1002
ROS_DOMAIN_ID=42
DISPLAY=:1
PULSE_SERVER=tcp:localhost:4713
```

---

## Configuration ROS 2

### Domain ID

**Pourquoi important** :
- Isolation réseau entre déploiements
- Évite conflits de topics entre robots
- Permet communication sélective

**Recommandations** :
- **Développement** : 33
- **Tests** : 0 (domaine par défaut)
- **Production** : 42, 100, etc. (unique par déploiement)

### Middleware DDS

**Configuration** :
```bash
RMW_IMPLEMENTATION=rmw_fastrtps_cpp
FASTDDS_BUILTIN_TRANSPORTS=UDPv4
```

**Transports disponibles** :
- **UDPv4** : Multicast, performant pour LAN
- **TCP** : Fiable, pour WAN
- **SHM** : Mémoire partagée, très rapide local

---

## Configuration Matérielle

### Affichage Graphique

#### X11 Forwarding
```bash
DISPLAY=:0
```

**Configuration hôte** :
```bash
# Autoriser Docker à accéder à X11
xhost +local:docker
```

#### Wayland (Alternative)
```bash
WAYLAND_DISPLAY=wayland-0
XDG_RUNTIME_DIR=/run/user/1000
```

### Audio

#### PulseAudio
```bash
PULSE_SERVER=unix:/tmp/pulse/native
```

**Configuration hôte** :
```bash
# Créer cookie PulseAudio
pactl load-module module-native-protocol-unix auth-anonymous=1
```

#### ALSA (Fallback)
```bash
ALSA_DEVICE=hw:0,0
```

### GPU

#### NVIDIA
```bash
NVIDIA_VISIBLE_DEVICES=all
NVIDIA_DRIVER_CAPABILITIES=compute,utility,graphics
```

#### Intel/AMD
```bash
LIBGL_ALWAYS_SOFTWARE=0
```

---

## Configuration Avancée

### Variables de Performance

```bash
# ROS 2 Performance
ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
ROS_STATIC_PEERS=192.168.1.100:11811

# Behavior Tree
BTCPP_LOG_LEVEL=INFO
BTCPP_GROOT_SERVER_PORT=1666

# Navigation
NAV2_PLANNER_FREQUENCY=10.0
NAV2_CONTROLLER_FREQUENCY=20.0
```

### Variables de Debug

```bash
# Logging étendu
ROS_LOG_LEVEL=DEBUG
RMW_FASTRTPS_USE_QOS_FROM_XML=1

# Profiling
ROS2_PERF_TEST=1
LD_PRELOAD=/usr/lib/libtcmalloc.so

# Behavior Tree Debug
BTCPP_ENABLE_GROOT_LOGGING=1
```

### Variables de Sécurité

```bash
# Isolation réseau
ROS_DOMAIN_ID=1001
FASTDDS_DEFAULT_PROFILES_FILE=/workspace/config/fastrtps_profile.xml

# Permissions
DOCKER_USER_UID=1000
DOCKER_USER_GID=1000
```

---

## Gestion des Configurations

### Versioning

#### Git Ignore
```gitignore
# Fichiers générés
config/.env

# Mais garder le template
!config/.env.example
```

#### Commits
```bash
# Commiter seulement les changements importants
git add config/.env.example
git commit -m "feat: Add new configuration variable"
```

### Validation

#### Script de Validation
```bash
#!/bin/bash
# validate_config.sh

source config/.env

# Vérifications
if [ -z "$HOST_UID" ]; then
    echo "ERROR: HOST_UID not set"
    exit 1
fi

if [ "$ROS_DOMAIN_ID" -lt 0 ] || [ "$ROS_DOMAIN_ID" -gt 232 ]; then
    echo "ERROR: ROS_DOMAIN_ID out of range [0-232]"
    exit 1
fi

echo "Configuration valid"
```

#### Tests Automatisés
```yaml
# .github/workflows/validate-config.yml
- name: Validate Configuration
  run: ./scripts/validate_config.sh
```

---

## Configurations Spécialisées

### Multi-Robot

#### Leader Configuration
```bash
# config/.env.leader
ROS_DOMAIN_ID=33
ROBOT_ROLE=leader
BT_TREE_FILE=nav_leader.xml
```

#### Follower Configuration
```bash
# config/.env.follower
ROS_DOMAIN_ID=42
ROBOT_ROLE=follower
BT_TREE_FILE=nav_leader_follower.xml
FOLLOW_DISTANCE=2.0
```

### Environnements

#### Simulation
```bash
# config/.env.sim
USE_SIM_TIME=true
GAZEBO_MODEL_PATH=/usr/share/gazebo/models
PHYSICS_ENGINE=ode
```

#### Hardware Réel
```bash
# config/.env.hw
USE_SIM_TIME=false
SENSOR_FUSION=true
LIDAR_DEVICE=/dev/ttyUSB0
IMU_DEVICE=/dev/ttyACM0
```

---

## Dépannage Configuration

### Problèmes Courants

#### Permissions Utilisateur
```bash
# Vérifier UID/GID
echo "Host UID/GID: $(id -u)/$(id -g)"
echo "Config UID/GID: $HOST_UID/$HOST_GID"

# Régénérer configuration
rm config/.env
make env
make build
```

#### Conflits ROS_DOMAIN_ID
```bash
# Scanner domaines actifs
ros2 multicast --scan

# Changer domaine
export ROS_DOMAIN_ID=100
```

#### Problèmes Audio
```bash
# Tester PulseAudio
pactl info

# Variables manquantes
echo "PULSE_SERVER: $PULSE_SERVER"
```

#### Problèmes Affichage
```bash
# Tester X11
xeyes  # Devrait s'ouvrir

# Variables d'environnement
echo "DISPLAY: $DISPLAY"
echo "WAYLAND_DISPLAY: $WAYLAND_DISPLAY"
```

### Debug Complet

#### Script de Diagnostic
```bash
#!/bin/bash
# diagnose_config.sh

echo "=== Configuration Diagnostic ==="
echo "Host: $(uname -a)"
echo "User: $(id)"
echo ""

echo "=== Docker Configuration ==="
if [ -f "config/.env" ]; then
    source config/.env
    echo "HOST_UID: $HOST_UID"
    echo "HOST_GID: $HOST_GID"
    echo "ROS_DOMAIN_ID: $ROS_DOMAIN_ID"
else
    echo "ERROR: config/.env not found"
fi
echo ""

echo "=== ROS 2 Status ==="
if command -v ros2 &> /dev/null; then
    ros2 --version
else
    echo "ROS 2 not found"
fi
```

---

## Évolution et Maintenance

### Nouvelles Variables

#### Processus d'Ajout
1. **Identifier le besoin** : Quel paramètre configurable ?
2. **Ajouter au template** : Modifier `.env.example`
3. **Documenter** : Mettre à jour ce README
4. **Valider** : Tester dans tous les environnements
5. **Migrer** : Script de migration pour déploiements existants

#### Exemple
```bash
# Nouvelle variable pour timeout
BT_NODE_TIMEOUT_MS=5000

# Ajouter à .env.example
echo "BT_NODE_TIMEOUT_MS=5000" >> config/.env.example
```

### Migration

#### Script de Migration
```bash
#!/bin/bash
# migrate_config.sh

BACKUP_FILE="config/.env.backup.$(date +%Y%m%d_%H%M%S)"

# Backup
cp config/.env "$BACKUP_FILE"

# Add new variables with defaults
if ! grep -q "BT_NODE_TIMEOUT_MS" config/.env; then
    echo "BT_NODE_TIMEOUT_MS=5000" >> config/.env
fi

echo "Migration completed. Backup: $BACKUP_FILE"
```

### Audit et Conformité

#### Vérifications Automatisées
- **Sécurité** : Pas de secrets en clair
- **Cohérence** : Variables utilisées dans le code
- **Documentation** : Toutes les variables documentées
- **Tests** : Configurations testées automatiquement

---

*Le dossier `config/` centralise toute la configuration du système MUTO-RS-CHOREOGRAPHY, permettant une adaptation flexible aux différents environnements de déploiement.*</content>
<parameter name="filePath">/home/edwin/TEKBOT-ROBOTICS-BENIN/MUTO-RS-CHOREGRAGPHY/config/README.md