# MUTO-RS-CHOREGRAGPHY

README minimal pour lancer rapidement.

## 1) Demarrage Docker (machine hote)

Depuis la racine du repo:

```bash
chmod +x scripts/setup_container_launcher_deps.sh docker/run_docker.sh
./scripts/setup_container_launcher_deps.sh
./docker/run_docker.sh
```

## 2) Build (dans le container)

```bash
source /opt/ros/humble/setup.bash
cd /workspace
colcon build
source install/setup.bash
export MUTO_RS_REPO=/workspace
```

## 3) Lancer Nav2 leader (BT leader integre)

Sans robot (TF de secours):

```bash
ros2 launch /workspace/src/muto_rs_nav_leader/launch/nav2_bringup.launch.py use_fake_map_tf:=True use_fake_odom_tf:=True
```

Avec robot reel:

```bash
ros2 launch /workspace/src/muto_rs_nav_leader/launch/nav2_bringup.launch.py use_fake_map_tf:=False use_fake_odom_tf:=False
```

Ce mode suppose que le robot publie deja `odom -> base_link`.
Si `odom` n'existe pas encore, utiliser temporairement:

```bash
ros2 launch /workspace/src/muto_rs_nav_leader/launch/nav2_bringup.launch.py use_fake_map_tf:=False use_fake_odom_tf:=True
```

## 4) Lancer Nav2 choreography

```bash
ros2 launch /workspace/src/muto_rs_nav_choregraphy/launch/nav2_bringup.launch.py use_fake_map_tf:=True use_fake_odom_tf:=True
```

## 5) Verif rapide

Dans un autre terminal du container:

```bash
source /opt/ros/humble/setup.bash
cd /workspace
source install/setup.bash
ros2 node list
```
