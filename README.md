# MUTO-RS-CHOREGRAGPHY - Commandes rapides

## 1) Installer les dependances Docker (machine hote)

Depuis la racine du repo:

```bash
chmod +x scripts/setup_container_launcher_deps.sh
./scripts/setup_container_launcher_deps.sh
newgrp docker
```

Verifier Docker/Compose:

```bash
docker --version
docker compose version
```

## 2) Lancer le container

Depuis la racine du repo:

```bash
chmod +x docker/run_docker.sh
./docker/run_docker.sh
```

## 3) Build du workspace (dans le container)

```bash
source /opt/ros/humble/setup.bash
cd /workspace
colcon build
source install/setup.bash
export MUTO_RS_REPO=/workspace
```

## 4) Lancer Nav2 (mode test SANS robot)

Ce mode sert a valider le bringup Nav2 meme si aucun robot n'est lance.
Le launch active des TF de secours (`map->odom` et `odom->base_link`) pour eviter les blocages au demarrage.

```bash
source /opt/ros/humble/setup.bash
cd /workspace
source install/setup.bash
ros2 launch /workspace/src/muto_rs_nav/launch/nav2_bringup.launch.py use_fake_map_tf:=True use_fake_odom_tf:=True
```

## 5) Lancer Nav2 (mode AVEC robot reel)

Quand ton robot publie deja les TF reelles, desactive les TF de secours:

```bash
source /opt/ros/humble/setup.bash
cd /workspace
source install/setup.bash
ros2 launch /workspace/src/muto_rs_nav/launch/nav2_bringup.launch.py use_fake_map_tf:=False use_fake_odom_tf:=False
```

## 6) Tests immediats (autre terminal dans le container)

Preparation du terminal de test:

```bash
source /opt/ros/humble/setup.bash
cd /workspace
source install/setup.bash
```

Verifier rapidement que Nav2 est bien lance:

```bash
ros2 node list
ros2 topic list
```

Publier la batterie robot 1:

```bash
ros2 topic pub /robot1/voltage std_msgs/msg/Float32 "{data: 12.0}" -r 2
```

Publier une position de reference robot 1 (Twist):

```bash
ros2 topic pub /robot1/cmd_vel geometry_msgs/msg/Twist "{linear: {x: 1.0, y: 2.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.3}}" -r 2
```

## 7) Depannage rapide des erreurs connues

Erreur `parameter 'yaml_filename' is not initialized`:
- verifier que le fichier de params charge contient bien `map_server.ros__parameters.yaml_filename`.

Erreur `No critics defined for FollowPath`:
- verifier la section `controller_server.ros__parameters.FollowPath.critics` dans les params Nav2.

Erreur `Could not load library: libnav2_sequence_bt_node.so`:
- retirer `nav2_sequence_bt_node` de `bt_navigator.ros__parameters.plugin_lib_names`.

Erreurs TF `frame does not exist` sur `map` ou `odom` en mode sans robot:
- relancer avec `use_fake_map_tf:=True use_fake_odom_tf:=True`.

## 8) Si ./docker/run_docker.sh renvoie 126

```bash
chmod +x docker/run_docker.sh docker/entrypoint.sh scripts/*.sh
```

Puis relancer:

```bash
./docker/run_docker.sh
```
