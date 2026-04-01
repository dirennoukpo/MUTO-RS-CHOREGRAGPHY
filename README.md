# MUTO-RS-CHOREOGRAPHY

## Vue d'Ensemble

**MUTO-RS-CHOREOGRAPHY** est un projet ROS 2 spécialisé dans l'orchestration de chorégraphies multi-robots synchronisées. Le système combine analyse audio en temps réel, génération de trajectoires de danse et navigation autonome pour permettre à plusieurs robots MUTO d'exécuter des danses parfaitement synchronisées avec la musique.

### Architecture Générale

Le projet suit une architecture **leader-follower** où :
- Une **machine centrale (PC)** analyse la musique et orchestre la chorégraphie
- Plusieurs **robots MUTO** exécutent les mouvements en synchronisation parfaite
- La communication se fait via **ROS 2** avec middleware **FastDDS**

### Fonctionnalités Clés

- **Synchronisation musicale** : Analyse audio avancée pour adapter la danse au rythme
- **Navigation autonome** : Behavior Trees intégrés pour le déplacement intelligent
- **Monitoring robot** : Surveillance batterie et position en temps réel
- **Modes opérationnels** : Chorégraphie musicale ou navigation autonome
- **Infrastructure Docker** : Environnement de développement containerisé

### Technologies Utilisées

- **ROS 2 Humble** : Middleware de communication robotique
- **BehaviorTree.CPP v3** : Orchestration logique des tâches
- **Nav2** : Navigation autonome et planification de trajectoire
- **MutoLib** : Abstraction du matériel robotique
- **Bibliothèques audio** : librosa, madmom, essentia pour l'analyse musicale

---

## Structure du Projet

```
MUTO-RS-CHOREOGRAPHY/
├── assets/           # Données audio et timelines musicales
├── build/            # Artefacts de compilation (généré)
├── config/           # Configuration environnement
├── docker/           # Configuration Docker
├── doc/              # Documentation technique
├── install/          # Packages ROS installés (généré)
├── log/              # Logs de build et exécution
├── scripts/          # Scripts d'automatisation
└── src/              # Code source des packages ROS 2
```

---

## Installation et Configuration

### Prérequis Système

- **Docker** >= 20.0
- **Docker Compose** >= 2.0
- **Ubuntu 22.04** (recommandé) ou compatible
- **4GB RAM** minimum, 8GB recommandé

### Workflow Docker avec Makefile

Depuis la racine du dépôt :

```bash
# 1. Configuration de l'environnement
make env

# 2. Construction de l'image Docker
make build

# 3. Démarrage des services
make up

# 4. Accès au shell du container
make shell
```

#### Description des Commandes Makefile

- `make env` : Crée `config/.env` à partir de `config/.env.example`
- `make build` : Construit l'image Docker de développement `ros-humble-dev`
- `make up` : Démarre les services Docker en arrière-plan
- `make shell` : Ouvre un shell Bash dans le container ROS
- `make down` : Arrête et supprime les containers/services
- `make logs` : Affiche les logs Docker en temps réel
- `make ps` : Liste les services actifs

#### Variables d'Environnement

Le fichier `config/.env` définit :
```bash
ROS_DOMAIN_ID=33
DISPLAY=:0
HOST_UID=1000
HOST_GID=1000
```

---

## Compilation et Build

### Dans le Container Docker

```bash
# Chargement de l'environnement ROS 2
source /opt/ros/humble/setup.bash

# Navigation vers le workspace
cd /workspace

# Compilation des packages ROS 2
colcon build --symlink-install

# Chargement des artefacts compilés
source install/setup.bash

# Variable utile pour les scripts
export MUTO_RS_REPO=/workspace
```

#### Options de Build

- `--symlink-install` : Liens symboliques pour développement rapide
- `--packages-select <pkg>` : Compiler uniquement un package spécifique
- `--cmake-args -DCMAKE_BUILD_TYPE=Release` : Build optimisé

---

## Utilisation

### Mode Chorégraphie Musicale

#### Lancement du Leader (Machine Centrale)

```bash
# Avec robot réel
ros2 launch muto_rs_synchronization dance_choreography.launch.py \
  mode:=leader \
  timeline:=/workspace/assets/json/data.json \
  audio_file:=/workspace/assets/audio/song.mp3 \
  loops:=1 \
  beat:=1.0 \
  speed:=3

# Mode simulation (sans audio)
ros2 launch muto_rs_synchronization dance_choreography.launch.py \
  mode:=leader \
  timeline:=/workspace/assets/json/data.json \
  play_audio:=false
```

#### Lancement des Followers (Robots)

Sur chaque robot MUTO (dans leur container Docker) :

```bash
# Configuration du domaine ROS
export ROS_DOMAIN_ID=42

# Lancement du follower
ros2 launch muto_rs_synchronization dance_choreography.launch.py \
  mode:=follower \
  step_width:=16 \
  dry_run:=false
```

### Mode Navigation Autonome

#### Navigation Leader

```bash
ros2 launch muto_rs_nav_leader/launch/nav2_bringup.launch.py \
  use_fake_map_tf:=false \
  use_fake_odom_tf:=false \
  use_fake_base_link_tf:=false
```

#### Navigation Leader-Follower

```bash
ros2 launch muto_rs_nav_leader_follower/launch/nav2_bringup.launch.py \
  use_fake_map_tf:=false
```

---

## Génération de Timelines Musicales

Utilisez le script `decodeur.py` pour analyser des fichiers audio MP3 :

```bash
cd /workspace

# Analyse d'un fichier audio
python3 scripts/decodeur.py \
  --input assets/audio/song.mp3 \
  --output assets/json/song_beats.json

# Avec paramètres personnalisés
python3 scripts/decodeur.py \
  --input assets/audio/song.mp3 \
  --output assets/json/song_beats.json \
  --bpm 120 \
  --sections intro,verse,chorus,outro
```

Le script génère automatiquement :
- Positions des battements et downbeats
- Détection d'onsets rythmiques
- Features d'énergie et flux spectral
- Segmentation structurelle (intro/verse/chorus/outro)
- Timeline JSON exploitable par `dance_leader.py`

---

## Packages ROS 2

### Core Packages

- **`muto_rs_synchronization`** : Orchestration danse leader/follower
- **`muto_rs_nav_leader`** : Navigation autonome du leader
- **`muto_rs_nav_leader_follower`** : Navigation du follower

### Behavior Tree Nodes

- **`pkg_battery_level`** : Monitoring niveau batterie
- **`pkg_get_position`** : Récupération position robot
- **`pkg_leader_bridge`** : Adaptation topics leader
- **`pkg_new_position`** : Calcul position décalée

---

## Dépannage

### Problèmes Courants

#### Erreur de Build
```bash
# Nettoyer et rebuild
rm -rf build/ install/ log/
colcon build
```

#### Problème de Permissions Docker
```bash
# Ajuster UID/GID dans .env
echo "HOST_UID=$(id -u)" >> config/.env
echo "HOST_GID=$(id -g)" >> config/.env
make build
```

#### Timeout Communication ROS 2
```bash
# Vérifier le domaine ROS
echo $ROS_DOMAIN_ID
# Doit être identique sur tous les robots
```

#### Audio Non Synchronisé
- Vérifier la latence du système audio
- Ajuster `AUDIO_PLAYER_LATENCY_S` dans `dance_leader.py`
- Tester avec `play_audio:=false`

### Logs et Debug

```bash
# Logs Docker
make logs

# Logs ROS 2
ros2 topic echo /dance_cmd
ros2 topic echo /robot1/voltage

# Liste des topics actifs
ros2 topic list

# État des nœuds
ros2 node list
```

---

## Développement

### Ajout d'un Nouveau Mouvement

1. Modifier `SECTION_AGGRESSION` dans `dance_leader.py`
2. Ajouter une palette dans `_PALETTES`
3. Tester avec `dry_run:=true`

### Extension Behavior Tree

1. Créer un nouveau package dans `src/`
2. Implémenter la classe dérivant de `BT::StatefulActionNode`
3. Définir les ports dans `providedPorts()`
4. Modifier le fichier `.xml` du BT

### Tests

```bash
# Tests unitaires
colcon test
colcon test-result --verbose

# Test spécifique
colcon test --packages-select muto_rs_synchronization
```

---

## Licence et Contribution

**Licence** : MIT (synchronisation), Apache 2.0 (packages techniques)

**Auteur** : Edwin Diren Noukpo (diren.noukpo@epitech.eu)

**Date** : Mars 2026

---

## Ressources Supplémentaires

- [Documentation ROS 2 Humble](https://docs.ros.org/en/humble/)
- [BehaviorTree.CPP](https://www.behaviortree.dev/)
- [Nav2 Documentation](https://navigation.ros.org/)
- [Docker pour ROS](https://hub.docker.com/_/ros/)

---

*Ce README fournit une vue d'ensemble complète du projet MUTO-RS-CHOREOGRAPHY. Pour des détails spécifiques sur chaque composant, consultez les README individuels dans chaque dossier.*
ros2 launch /workspace/src/muto_rs_nav_leader/launch/nav2_bringup.launch.py use_fake_map_tf:=True use_fake_odom_tf:=True use_fake_base_link_tf:=True
```

Attention: ce mode sert surtout au bringup, au debug TF et a l'affichage dans RViz.
Avec des TF statiques `odom -> base_footprint -> base_link`, le robot ne bougera pas vraiment pour Nav2.
Pour qu'une navigation aboutisse, il faut une odometrie dynamique et un robot ou simulateur qui consomme `cmd_vel`.

Avec robot reel:

```bash
ros2 launch /workspace/src/muto_rs_nav_leader/launch/nav2_bringup.launch.py use_fake_map_tf:=False use_fake_odom_tf:=False use_fake_base_link_tf:=False
```

Ce mode suppose que le robot publie deja `odom -> base_link`.
Si `odom` n'existe pas encore, utiliser temporairement:

```bash
ros2 launch /workspace/src/muto_rs_nav_leader/launch/nav2_bringup.launch.py use_fake_map_tf:=False use_fake_odom_tf:=True use_fake_base_link_tf:=True
```

Ce mode reste un mode de secours pour demarrer Nav2. Il ne remplace pas une vraie transform dynamique `odom -> base_link` pour faire avancer le robot.

## 4) Lancer Nav2 choreography

```bash
ros2 launch /workspace/src/muto_rs_nav_choregraphy/launch/nav2_bringup.launch.py use_fake_map_tf:=True use_fake_odom_tf:=True use_fake_base_link_tf:=True
```

Attention: avec `use_fake_odom_tf:=True` et `use_fake_base_link_tf:=True`, la chaine TF du robot est statique.
Nav2 peut alors se lancer, recevoir des goals et calculer un chemin, mais il ne peut pas confirmer un deplacement reel sans odometrie dynamique.
Ce mode est utile pour verifier la carte, AMCL et les topics, pas pour valider une navigation complete.

Avec robot reel (TF deja publiees par le robot):

```bash
ros2 launch /workspace/src/muto_rs_nav_choregraphy/launch/nav2_bringup.launch.py use_fake_map_tf:=False use_fake_odom_tf:=False use_fake_base_link_tf:=False
```

Simulation Gazebo (horloge /clock):

```bash
ros2 launch /workspace/src/muto_rs_nav_choregraphy/launch/nav2_bringup.launch.py use_sim_time:=True
```

En simulation complete, il faut aussi un robot simule qui publie une odometrie dynamique, la TF associee et qui applique `cmd_vel`.

## 5) Verif rapide

Dans un autre terminal du container:

```bash
source /opt/ros/humble/setup.bash
cd /workspace
source install/setup.bash
ros2 node list
```

Description des commandes:

- `source /opt/ros/humble/setup.bash`: charge l'environnement ROS de base.
- `cd /workspace`: va dans le workspace du projet.
- `source install/setup.bash`: charge les paquets construits localement.
- `ros2 node list`: verifie les noeuds ROS 2 actifs.
