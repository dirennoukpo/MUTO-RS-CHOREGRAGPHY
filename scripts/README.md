# Dossier scripts/

## Vue d'Ensemble

Le dossier `scripts/` contient tous les scripts d'automatisation et d'outils utilitaires du projet MUTO-RS-CHOREOGRAPHY. Ces scripts facilitent le développement, le déploiement et la maintenance du système multi-robots.

## Structure Générale

```
scripts/
├── decodeur.py              # 🎵 Analyseur audio MP3 → JSON
├── setup_leader_muto_rs.sh  # 🏗️ Configuration leader danse
└── setup_follower_muto_rs.sh # 🤖 Configuration follower danse
```

## Scripts Détaillés

### 🎵 decodeur.py (Python)

**Rôle** : Pipeline complet d'analyse audio pour génération de timelines musicales.

**Fonctionnalités principales** :
- Analyse spectrale avancée des fichiers MP3
- Extraction de features temporelles (battements, énergie, flux)
- Segmentation automatique en sections musicales
- Export JSON structuré pour synchronisation danse

**Pipeline de traitement** :
1. **Séparation HPSS** : Harmonic/Percussive Source Separation
2. **Beat tracking** : Détection précise des battements
3. **Onset detection** : Attaques rythmiques
4. **Feature extraction** : Énergie RMS, flux spectral
5. **Segmentation** : Clustering MFCC pour sections
6. **Classification** : Intro/verse/chorus/outro

**Utilisation** :

```bash
# Analyse basique
python3 scripts/decodeur.py \
  --input assets/audio/song.mp3 \
  --output assets/json/song_timeline.json

# Avec paramètres avancés
python3 scripts/decodeur.py \
  --input assets/audio/song.mp3 \
  --output assets/json/song_timeline.json \
  --bpm 128 \
  --sections intro,verse,chorus,outro,bridge
```

**Paramètres** :
- `--input` : Fichier audio source (MP3 requis)
- `--output` : Fichier JSON de destination
- `--bpm` : BPM forcé (optionnel, auto-détection sinon)
- `--sections` : Labels de sections personnalisés

**Dépendances** : `librosa`, `madmom`, `aubio`, `essentia`, `scikit-learn`

---

### 🏗️ setup_leader_muto_rs.sh (Bash)

**Rôle** : Script de configuration et lancement du mode leader danse.

**Actions effectuées** :
1. Vérification de l'environnement ROS 2
2. Build des packages si nécessaire
3. Configuration des variables d'environnement
4. Lancement du launcher ROS 2 avec paramètres appropriés

**Contenu du script** :

```bash
#!/bin/bash

# Configuration ROS 2
source /opt/ros/humble/setup.bash
cd /workspace
colcon build --symlink-install
source install/setup.bash

# Variables d'environnement
export MUTO_RS_REPO=/workspace
export ROS_DOMAIN_ID=33

# Lancement du leader
ros2 launch muto_rs_synchronization dance_choreography.launch.py \
  mode:=leader \
  timeline:=${MUTO_RS_REPO}/assets/json/data.json \
  audio_file:=${MUTO_RS_REPO}/assets/audio/song.mp3 \
  loops:=1 \
  beat:=1.0 \
  speed:=3 \
  audio_player:=ffplay
```

**Utilisation** :
```bash
# Depuis le container Docker
./scripts/setup_leader_muto_rs.sh

# Ou directement
bash scripts/setup_leader_muto_rs.sh
```

**Prérequis** :
- Environnement ROS 2 configuré
- Packages compilés (`colcon build`)
- Fichiers assets présents

---

### 🤖 setup_follower_muto_rs.sh (Bash)

**Rôle** : Script de configuration et lancement du mode follower danse.

**Particularités** :
- Configuration du ROS_DOMAIN_ID pour communication avec le leader
- Paramètres optimisés pour l'exécution robot
- Support du mode dry-run pour tests

**Contenu du script** :

```bash
#!/bin/bash

# Configuration pour follower
export ROS_DOMAIN_ID=42  # Différent du leader (33)
export MUTO_RS_REPO=/workspace

# Lancement du follower
ros2 launch muto_rs_synchronization dance_choreography.launch.py \
  mode:=follower \
  step_width:=16 \
  dry_run:=false
```

**Utilisation** :
```bash
# Sur chaque robot MUTO
./scripts/setup_follower_muto_rs.sh

# Mode simulation
DRY_RUN=true ./scripts/setup_follower_muto_rs.sh
```

**Configuration réseau** :
- **Leader** : ROS_DOMAIN_ID=33
- **Followers** : ROS_DOMAIN_ID=42 (ou autre valeur unique)
- Communication via UDP multicast FastDDS

---

## Architecture des Scripts

### Design Patterns

#### Script Wrapper Pattern

```bash
#!/bin/bash
set -e  # Exit on error

# Fonction de logging
log() {
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] $*" >&2
}

# Fonction principale
main() {
    log "Starting setup..."
    # Logique principale
    log "Setup completed successfully"
}

# Gestion des signaux
trap 'log "Script interrupted"' INT TERM

# Exécution
main "$@"
```

#### Configuration Environment

```bash
# Chargement configuration
if [ -f "config/.env" ]; then
    source config/.env
fi

# Variables par défaut
ROS_DOMAIN_ID=${ROS_DOMAIN_ID:-33}
MUTO_RS_REPO=${MUTO_RS_REPO:-/workspace}
```

### Gestion d'Erreurs

```bash
# Vérification prérequis
check_prerequisites() {
    if ! command -v ros2 &> /dev/null; then
        echo "ERROR: ROS 2 not found"
        exit 1
    fi

    if [ ! -d "install" ]; then
        echo "ERROR: Packages not built. Run 'colcon build' first"
        exit 1
    fi
}
```

### Logging et Debug

```bash
# Niveaux de verbosité
VERBOSE=${VERBOSE:-false}

debug() {
    if [ "$VERBOSE" = true ]; then
        echo "DEBUG: $*" >&2
    fi
}

info() {
    echo "INFO: $*" >&2
}

error() {
    echo "ERROR: $*" >&2
}
```

---

## Utilisation Avancée

### Automatisation CI/CD

#### GitHub Actions

```yaml
- name: Setup ROS 2
  run: |
    source /opt/ros/humble/setup.bash
    ./scripts/setup_leader_muto_rs.sh --dry-run

- name: Run Tests
  run: |
    colcon test
    colcon test-result --verbose
```

#### Docker Integration

```dockerfile
# Copie des scripts
COPY scripts/ /workspace/scripts/

# Configuration des permissions
RUN chmod +x /workspace/scripts/*.sh

# Point d'entrée
ENTRYPOINT ["/workspace/scripts/setup_leader_muto_rs.sh"]
```

### Personnalisation

#### Variables d'Environnement

```bash
# Configuration personnalisée
export ROS_DOMAIN_ID=100
export AUDIO_FILE=/custom/path/song.mp3
export TIMELINE_FILE=/custom/path/timeline.json
export STEP_WIDTH=20
export DANCE_SPEED=4

# Lancement avec configuration
./scripts/setup_follower_muto_rs.sh
```

#### Paramètres Dynamiques

```bash
# Script paramétrable
TIMELINE=${1:-data.json}
AUDIO=${2:-song.mp3}
SPEED=${3:-3}

ros2 launch muto_rs_synchronization dance_choreography.launch.py \
  mode:=leader \
  timeline:=assets/json/${TIMELINE} \
  audio_file:=assets/audio/${AUDIO} \
  speed:=${SPEED}
```

---

## Maintenance et Évolution

### Tests des Scripts

#### Tests Unitaires

```bash
# Test decodeur.py
python3 -m pytest scripts/test_decodeur.py -v

# Test des scripts Bash
bats scripts/test_setup_scripts.bats
```

#### Tests d'Intégration

```bash
# Test complet pipeline
./scripts/test_integration.sh

# Validation des outputs
python3 scripts/validate_outputs.py \
  --timeline assets/json/test.json \
  --audio assets/audio/test.mp3
```

### Documentation

#### Génération Automatique

```bash
# Documentation des scripts
python3 scripts/generate_docs.py

# Mise à jour README
./scripts/update_readme.sh
```

#### Standards de Code

- **ShellCheck** : Linting Bash
- **Black** : Formatage Python
- **Pylint** : Analyse qualité Python
- **Documentation** : Docstrings complètes

### Versioning

#### Gestion des Versions

```bash
# Version des scripts
SCRIPT_VERSION="2.1.0"
echo "MUTO-RS Scripts v${SCRIPT_VERSION}"

# Compatibilité ROS 2
ROS_VERSION=$(ros2 --version | grep -oP 'ROS \K[^\s]+')
if [[ "$ROS_VERSION" != "Humble"* ]]; then
    echo "WARNING: Tested with ROS 2 Humble"
fi
```

---

## Sécurité et Bonnes Pratiques

### Validation des Entrées

```bash
# Validation des fichiers
validate_file() {
    local file=$1
    if [ ! -f "$file" ]; then
        error "File not found: $file"
        exit 1
    fi

    if [ ! -r "$file" ]; then
        error "File not readable: $file"
        exit 1
    fi
}

# Utilisation
validate_file "$TIMELINE_FILE"
validate_file "$AUDIO_FILE"
```

### Gestion des Permissions

```bash
# Permissions appropriées
chmod 755 scripts/*.sh
chmod 644 scripts/*.py

# Vérification
if [ ! -x "scripts/setup_leader_muto_rs.sh" ]; then
    error "Script not executable"
    exit 1
fi
```

### Logging Sécurisé

```bash
# Éviter les logs sensibles
log "Starting setup for robot ${ROBOT_ID}"

# Ne pas logger les mots de passe ou tokens
# debug "Using token: ${SENSITIVE_TOKEN}"  # ❌ DANGEREUX
debug "Token configured: yes"  # ✅ SÉCURISÉ
```

---

## Débogage et Troubleshooting

### Debug Mode

```bash
# Activation debug
export DEBUG=true
export VERBOSE=true

# Lancement avec debug
./scripts/setup_leader_muto_rs.sh 2>&1 | tee debug.log
```

### Analyse des Logs

```bash
# Recherche d'erreurs
grep -i "error" debug.log

# Analyse temporelle
grep -E "[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}" debug.log | head -20
```

### Problèmes Courants

#### ROS 2 Non Trouvé
```bash
# Vérifier l'installation
which ros2
ros2 --version

# Source correct
source /opt/ros/humble/setup.bash
```

#### Packages Non Compilés
```bash
# Build manuel
colcon build --symlink-install

# Vérification
ls -la install/setup.bash
```

#### Permissions Fichiers
```bash
# Correction permissions
chmod +x scripts/*.sh
sudo chown -R $USER:$USER /workspace
```

---

## Évolution Future

### Nouvelles Fonctionnalités

- **Interface Web** : Configuration via navigateur
- **Monitoring** : Dashboard temps réel
- **Auto-déploiement** : Installation automatique sur robots
- **Tests de Charge** : Validation performance
- **Rollback** : Retour version précédente

### Améliorations Techniques

- **Parallélisation** : Traitement multi-core
- **Cache** : Accélération analyses répétées
- **Streaming** : Analyse audio temps réel
- **Machine Learning** : Amélioration classification sections

---

*Le dossier `scripts/` est essentiel pour l'automatisation et la reproductibilité du système MUTO-RS-CHOREOGRAPHY. Il transforme des tâches complexes en commandes simples et fiables.*</content>
<parameter name="filePath">/home/edwin/TEKBOT-ROBOTICS-BENIN/MUTO-RS-CHOREGRAGPHY/scripts/README.md