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

## 4) Lancer Nav2 avec ton BT

```bash
ros2 launch /workspace/src/muto_rs_nav/launch/nav2_bringup.launch.py
```

## 5) Tests immediats (autre terminal dans le container)

Publier la batterie robot 1:

```bash
source /opt/ros/humble/setup.bash
cd /workspace
source install/setup.bash
ros2 topic pub /robot1/voltage std_msgs/msg/Float32 "{data: 12.0}" -r 2
```

Publier une position de reference robot 1 (Twist):

```bash
source /opt/ros/humble/setup.bash
cd /workspace
source install/setup.bash
ros2 topic pub /robot1/cmd_vel geometry_msgs/msg/Twist "{linear: {x: 1.0, y: 2.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.3}}" -r 2
```

Verifier rapidement:

```bash
ros2 node list
ros2 topic list
```

## 6) Si ./docker/run_docker.sh renvoie 126

```bash
chmod +x docker/run_docker.sh docker/entrypoint.sh scripts/*.sh
```

Puis relancer:

```bash
./docker/run_docker.sh
```
