# Documentation Détaillée : MUTO-RS-CHOREOGRAPHY

## Vue d'Ensemble du Projet

**MUTO-RS-CHOREOGRAPHY** est une branche ROS 2 spécialisée en **orchestration de chorégraphies multi-robots synchronisées**, combinant analyse audio en temps réel, génération de trajectoires de danse et navigation autonome. Le projet suit une architecture leader-follower où une machine centrale (PC) orchestre plusieurs robots qui exécutent la chorégraphie en synchronisation musicale.

### Objectifs Clés
- Synchroniser plusieurs robots MUTO pour exécuter des danses ensemble
- Adapter la chorégraphie en fonction de la structure et de la dynamique musicale (intro/verso/chorus/outro)
- Fournir une abstraction pour MutoLib (contrôle du matériel)
- Intégrer Nav2 pour la navigation autonome avec des behavior trees personnalisés
- Monitorer l'état des robots (batterie, position)

### Dates et Contexte
- Projet maintenu par **Edwin** (diren.noukpo@epitech.eu)
- Dernières modifications : Mars 2026
- Licence : MIT (synchronization), Apache 2.0 (packages techniques)

---

## Architecture Générale

Le système fonctionne en deux modes principaux :

1. **Mode Chorégraphie** : Orchestration musicale synchronisée
2. **Mode Navigation** : Suivi autonome avec behavior trees

### Communication ROS 2
- **Middleware** : FastDDS (UDP multicast)
- **Domain ID** : 33 pour le leader, configurable pour followers
- **Topics principaux** :
  - `/dance_cmd` : Commandes de danse synchronisées
  - `/robot{id}/pose` : Position des robots
  - `/robot{id}/voltage` : Niveau de batterie

---

## Packages ROS 2 Détaillés

### A) muto_rs_synchronization (Python)

#### dance_leader.py
Orchestre la chorégraphie côté PC (machine leader).

**Fonctionnalités principales :**
- Publication sur `/dance_cmd` avec protocole de commandes
- Compensation de latence audio (100ms empirique)
- Adaptation de la palette de mouvements selon l'intensité musicale
- Lecture audio synchronisée

**Paramètres :**
- `loops` : Nombre de répétitions (défaut: 1)
- `beat` : Facteur tempo (0.8=ralenti, 1.2=accéléré)
- `speed` : Vitesse nominale (1-5)
- `step_width` : Largeur des pas (8-25)
- `timeline` : Fichier JSON de timing musical
- `audio_file` : Piste audio MP3
- `audio_player` : Player audio (ffplay/mpg123)
- `play_audio` : Contrôle de lecture audio

#### dance_follower.py
Exécuteur côté robot qui reçoit les commandes du leader.

**Modes :**
- Normal : Utilise MutoLib pour contrôler le hardware
- Dry-run : Simulation sans hardware

---

### B) muto_rs_nav_leader (C++)

Navigation autonome du leader avec behavior tree intégré.

**Behavior Tree (nav_leader.xml) :**
Séquence de 56 nœuds incluant récupération de position, vérification batterie, calcul de nouvelle position et navigation avec récupération.

**Paramètres de navigation :**
- Utilise Nav2 stack complet
- Cartes : electronics_room.yaml/.pgm
- Paramètres : nav2_params.yaml

---

### C) muto_rs_nav_leader_follower (C++)

Navigation du follower qui suit le leader.

**Behavior Tree (nav_leader_follower.xml) :**
ReactiveSequence pour suivi continu avec offset (-2m sur X).

---

### D) pkg_battery_level (C++)

Nœud Behavior Tree pour monitoring de la batterie.

**Ports BT :**
- Input : robot_id, min_battery_level, wait_timeout_ms
- Output : battery_level

**QoS :** Transient local + reliable

---

### E) pkg_get_position (C++)

Nœud BT pour récupérer la position du robot.

**Ports BT :**
- Input : robot_id, wait_timeout_ms
- Output : position (PoseStamped)

---

### F) pkg_leader_bridge (C++)

Adaptateur de topics pour exposer l'état du leader.

**Paramètres :**
- robot_id : "1"
- pose_source_topic : "/amcl_pose"
- voltage_source_topic : "/voltage"

---

### G) pkg_new_position (C++)

Nœud BT pour calculer une position décalée.

**Ports BT :**
- Input : ref_position, offset_x, offset_y, offset_z
- Output : position

---

## Configuration et Infrastructure

### Docker Compose
Service ros-humble-dev basé sur ros:humble-ros-base-jammy.

**Volumes :**
- Code source, historique bash, X11, GPU, PulseAudio

**Environnement :**
- ROS_DISTRO=humble
- ROS_DOMAIN_ID=33
- RMW_IMPLEMENTATION=rmw_fastrtps_cpp

### Makefile
Commandes d'automatisation :
- `make env` : Configuration environnement
- `make build` : Construction image Docker
- `make up` : Démarrage services
- `make shell` : Accès bash dans container

### Scripts d'Automatisation

#### decodeur.py
Convertit MP3 → JSON timeline musicale.

**Pipeline :**
1. Séparation HPSS (Harmonic/Percussive)
2. Beat tracking avec Madmom
3. Détection d'onsets
4. Calcul d'énergie RMS et flux spectral
5. Segmentation structurelle par clustering MFCC
6. Étiquetage automatique (intro/verse/chorus/outro)

#### Scripts Setup
- `setup_leader_muto_rs.sh` : Lancement leader danse
- `setup_follower_muto_rs.sh` : Lancement follower danse

---

## Fichiers de Données

### Assets JSON
Timelines musicales pour différentes chansons avec structure :
```json
{
  "beats": [...],
  "onsets": [...],
  "energy": [...],
  "flux": [...],
  "intensity": [...],
  "sections": [...]
}
```

### Assets Audio
Pistes MP3 associées aux timelines.

---

## Technologies et Dépendances

### Langages
- Python 3 : Orchestration, analyse audio
- C++ : Nœuds BT, intégration Nav2
- XML : Définitions behavior trees
- YAML : Configurations
- Bash : Automatisation

### Frameworks
- ROS 2 Humble : Communication
- Nav2 : Navigation
- BehaviorTree.CPP v3 : Logique état-machine
- MutoLib : Abstraction hardware

### Bibliothèques Audio
- librosa : Analyse temps-fréquence
- madmom : Beat tracking
- aubio : Détection onsets
- essentia : Analyse spectrale
- soundfile : Lecture/écriture audio

---

## Build et Exécution

### Construction
```bash
make env
make build
make up
make shell
# Dans le container :
colcon build
source install/setup.bash
```

### Lancement Chorégraphie
**Leader :**
```bash
ros2 launch muto_rs_synchronization dance_choreography.launch.py mode:=leader timeline:=... audio_file:=...
```

**Follower :**
```bash
ros2 launch muto_rs_synchronization dance_choreography.launch.py mode:=follower step_width:=16
```

### Lancement Navigation
```bash
ros2 launch muto_rs_nav_leader/launch/nav2_bringup.launch.py use_fake_map_tf:=True ...
```

---

## Structure de Répertoires

```
MUTO-RS-CHOREOGRAPHY/
├── src/                          # Packages ROS 2
│   ├── muto_rs_synchronization/  # Leader/Follower danse (Python)
│   ├── muto_rs_nav_leader/       # Nav2 leader (C++)
│   ├── muto_rs_nav_leader_follower/  # Nav2 follower (C++)
│   ├── pkg_battery_level/        # BT node batterie (C++)
│   ├── pkg_get_position/         # BT node position (C++)
│   ├── pkg_leader_bridge/        # BT node adapter leader (C++)
│   └── pkg_new_position/         # BT node offset position (C++)
├── docker/                       # Configuration Docker
├── config/                       # Environnement
├── scripts/                      # Automatisation
├── assets/                       # Données (JSON, audio)
├── build/                        # Artefacts compilation
├── install/                      # Packages installés
└── log/                          # Logs
```

---

## Résumé Technique

| Aspect | Détail |
|--------|--------|
| Distro ROS | Humble (22.04 LTS) |
| Packages ROS | 7 (1 Python, 6 C++) |
| Behavior Trees | 3 personnalisés |
| Nœuds BT | 4 personnalisés |
| Libs Audio | 5 principales |
| Communication | ROS 2 DDS (FastDDS) |
| Architecture | Leader-Follower multi-robot |
| Sync Musicale | Latence ~100ms |

Cette documentation couvre tous les aspects du projet MUTO-RS-CHOREOGRAPHY.</content>
<parameter name="filePath">/home/edwin/TEKBOT-ROBOTICS-BENIN/MUTO-RS-CHOREGRAGPHY/DOCUMENTATION.md