# Dossier src/

## Vue d'Ensemble

Le dossier `src/` contient l'intégralité du code source des packages ROS 2 du projet MUTO-RS-CHOREOGRAPHY. Il s'agit du cœur fonctionnel du système, organisé en 7 packages distincts répondant à des responsabilités spécifiques.

## Structure Générale

```
src/
├── muto_rs_synchronization/         # 🏗️ Orchestration danse (Python)
├── muto_rs_nav_leader/              # 🚶 Navigation autonome leader (C++)
├── muto_rs_nav_leader_follower/     # 👥 Navigation follower (C++)
├── pkg_battery_level/               # 🔋 Monitoring batterie (C++)
├── pkg_get_position/                # 📍 Récupération position (C++)
├── pkg_leader_bridge/               # 🌉 Adaptation topics leader (C++)
└── pkg_new_position/                # 📐 Calcul position décalée (C++)
```

## Packages Détaillés

### 🎭 muto_rs_synchronization (Python)

**Rôle** : Orchestration centrale de la chorégraphie multi-robots synchronisée.

**Composants principaux** :
- `dance_leader.py` : Cerveau de la chorégraphie côté PC
- `dance_follower.py` : Exécuteur côté robot
- `launch/dance_choreography.launch.py` : Lanceur ROS 2

**Fonctionnalités** :
- Analyse musicale en temps réel
- Publication de commandes synchronisées sur `/dance_cmd`
- Compensation de latence audio (~100ms)
- Adaptation dynamique selon l'intensité musicale
- Support mode simulation (dry-run)

**Dépendances** : `rclpy`, `std_msgs`

---

### 🚶 muto_rs_nav_leader (C++)

**Rôle** : Navigation autonome du robot leader avec behavior tree intégré.

**Composants** :
- `behavior_trees/nav_leader.xml` : Behavior Tree (56 nœuds)
- `launch/nav2_bringup.launch.py` : Configuration Nav2
- `maps/electronics_room.*` : Cartes de navigation
- `params/nav2_params.yaml` : Paramètres Nav2

**Behavior Tree** :
```
Sequence: GetPosition → CheckBatteryLevel → SetNewPosition → Navigate
├── Recovery: clear costmaps, Spin, Wait, BackUp (6 tentatives)
└── Offset: +3m X, +3m Y par défaut
```

**Dépendances** : `rclcpp`, `nav2_bringup`, `behaviortree_cpp_v3`

---

### 👥 muto_rs_nav_leader_follower (C++)

**Rôle** : Navigation autonome du robot follower qui suit le leader.

**Composants** :
- `behavior_trees/nav_leader_follower.xml` : BT pour suivi
- `launch/nav2_bringup.launch.py` : Configuration Nav2
- Héritage des cartes/params du leader

**Behavior Tree** :
```
ReactiveSequence: CheckBatteryLevel → GetPosition → SetNewPosition → Navigate
└── Offset: -2m X, 0m Y (suivi derrière le leader)
```

**Particularités** :
- Mise à jour dynamique du goal chaque cycle
- ReactiveSequence pour adaptation continue
- Même infrastructure Nav2 que le leader

---

### 🔋 pkg_battery_level (C++)

**Rôle** : Nœud Behavior Tree pour surveillance du niveau de batterie.

**Architecture** :
- Classe `CheckBatteryLevel` dérivant de `BT::StatefulActionNode`
- Abonnement QoS transient_local + reliable
- Callback thread-safe pour réception des données

**Ports BT** :
- **Input** :
  - `robot_id` (string) : ID du robot à surveiller
  - `min_battery_level` (float) : Seuil minimum (défaut: 6.0V)
  - `wait_timeout_ms` (int) : Timeout attente données
- **Output** :
  - `battery_level` (float) : Niveau actuel

**Topic souscrit** : `/robot{id}/voltage` (std_msgs/Float32)

---

### 📍 pkg_get_position (C++)

**Rôle** : Nœud Behavior Tree pour récupération de la position robot.

**Architecture** :
- Classe `GetPosition` dérivant de `BT::StatefulActionNode`
- Abonnement aux données de localisation
- Support AMCL et odométrie

**Ports BT** :
- **Input** :
  - `robot_id` (string) : ID du robot
  - `wait_timeout_ms` (int) : Timeout (défaut: 5s)
- **Output** :
  - `position` (PoseStamped) : Position actuelle

**Topic souscrit** : `/robot{id}/pose` (geometry_msgs/PoseStamped)

---

### 🌉 pkg_leader_bridge (C++)

**Rôle** : Adaptateur de topics pour exposer l'état du leader aux BT.

**Fonctionnalités** :
- Bridge configurable pour topics de pose
- Bridge pour monitoring batterie
- Conversion PoseWithCovarianceStamped → PoseStamped
- Paramètres configurables via launch

**Topics gérés** :
- **Input** : `/amcl_pose`, `/voltage`
- **Output** : `/robot{id}/pose`, `/robot{id}/voltage`

**Paramètres de lancement** :
```yaml
robot_id: "1"
pose_source_topic: "/amcl_pose"
voltage_source_topic: "/voltage"
```

---

### 📐 pkg_new_position (C++)

**Rôle** : Nœud Behavior Tree pour calcul de position décalée.

**Algorithme** :
- Réception position de référence
- Application d'offsets (X, Y, Z)
- Utilisation tf2 pour manipulations quaternion
- Frame cible : "map"

**Ports BT** :
- **Input** :
  - `ref_position` (PoseStamped) : Position de base
  - `offset_x`, `offset_y`, `offset_z` (float) : Décalages
- **Output** :
  - `position` (PoseStamped) : Nouvelle position calculée

**Type** : `BT::SyncActionNode` (calcul synchrone)

---

## Architecture Technique

### Langages Utilisés

| Package | Langage | Framework |
|---------|---------|-----------|
| muto_rs_synchronization | Python 3 | rclpy |
| Navigation packages | C++17 | rclcpp |
| BT nodes | C++17 | BehaviorTree.CPP v3 |

### Patterns de Conception

- **StatefulActionNode** : Pour les opérations asynchrones (abonnements)
- **SyncActionNode** : Pour les calculs synchrones
- **Factory Pattern** : Enregistrement des nœuds BT
- **Observer Pattern** : Callbacks ROS 2

### Qualité de Service (QoS)

- **Reliability** : RELIABLE (livraison garantie)
- **Durability** : TRANSIENT_LOCAL (rétention pour nouveaux abonnés)
- **History** : Keep Last 1 (dernière valeur uniquement)

---

## Développement et Maintenance

### Ajout d'un Nouveau Package

1. Créer le dossier dans `src/`
2. Implémenter `CMakeLists.txt` et `package.xml`
3. Développer la logique métier
4. Tester l'intégration
5. Mettre à jour la documentation

### Tests et Validation

```bash
# Build spécifique
colcon build --packages-select <nom_package>

# Tests unitaires
colcon test --packages-select <nom_package>

# Linting
ament_cpplint <nom_package>
ament_cppcheck <nom_package>
```

### Debugging

```bash
# Logs ROS 2
ros2 topic echo /bt_debug
ros2 node info <nom_node>

# Behavior Tree Viewer
ros2 run behavior_tree_viewer behavior_tree_viewer
```

---

## Dépendances et Build

### Dépendances Système

- **ROS 2 Humble** : Base middleware
- **BehaviorTree.CPP v3** : Framework BT
- **Nav2** : Stack navigation
- **tf2** : Transformations géométriques

### Configuration Build

Chaque package contient :
- `CMakeLists.txt` : Règles de compilation C++
- `package.xml` : Métadonnées et dépendances ROS
- `setup.py` : Configuration Python (si applicable)

### Optimisations

- **Link symbolique** : `--symlink-install` pour développement
- **Compilation parallèle** : Utilisation de tous les cœurs
- **Cache CMake** : Accélération des rebuilds

---

## Intégration Continue

### Workflows GitHub Actions

- **Build** : Compilation sur Ubuntu 22.04
- **Tests** : Exécution des suites de test
- **Linting** : Vérification qualité code
- **Documentation** : Génération automatique

### Métriques Qualité

- **Coverage** : > 80% lignes de code
- **Complexity** : Cyclomatic < 10
- **Dependencies** : Analyse des vulnérabilités

---

## Évolution et Roadmap

### Améliorations Prévues

- **Multi-threading** : Parallélisation des calculs BT
- **Plugin Architecture** : Extensibilité des nœuds BT
- **Monitoring Avancé** : Métriques performance temps réel
- **Hot Reload** : Rechargement BT sans redémarrage

### Maintenance

- **Code Reviews** : Revue systématique des PR
- **Documentation** : Mise à jour automatique
- **Deprecation** : Gestion des API obsolètes
- **Migration** : Support ROS 2 Iron/Iron+Iron

---

*Ce dossier `src/` constitue le cœur technique du système MUTO-RS-CHOREOGRAPHY. Chaque package est conçu pour être modulaire, testable et maintenable, suivant les meilleures pratiques ROS 2 et C++ moderne.*</content>
<parameter name="filePath">/home/edwin/TEKBOT-ROBOTICS-BENIN/MUTO-RS-CHOREGRAGPHY/src/README.md