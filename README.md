# MUTO-RS-CHOREGRAGPHY

README minimal pour lancer rapidement.

## Workflow Docker avec Makefile

Depuis la racine du repo:

```bash
make env
make build
make up
make shell
```

Description des commandes:

- `make env`: cree `config/.env` a partir de `config/.env.example`.
- `make build`: construit l'image Docker de developpement.
- `make up`: demarre les services Docker en arriere-plan.
- `make shell`: ouvre un shell Bash dans le container `ros-humble-dev`.

Arreter l'environnement:

```bash
make down
```

- `make down`: arrete et supprime les containers/services du projet.

## 1) Demarrage Docker (machine hote)

Depuis la racine du repo:

```bash
chmod +x scripts/setup_container_launcher_deps.sh docker/run_docker.sh
./scripts/setup_container_launcher_deps.sh
./docker/run_docker.sh
```

Description des commandes:

- `chmod +x ...`: rend les scripts executables.
- `./scripts/setup_container_launcher_deps.sh`: installe les dependances cote machine hote pour lancer Docker facilement.
- `./docker/run_docker.sh`: lance (ou reconstruit) le container et ouvre un shell dedans.

## 2) Build (dans le container)

```bash
source /opt/ros/humble/setup.bash
cd /workspace
colcon build
source install/setup.bash
export MUTO_RS_REPO=/workspace
```

Description des commandes:

- `source /opt/ros/humble/setup.bash`: charge l'environnement ROS 2 Humble.
- `cd /workspace`: se place dans le dossier monte du projet.
- `colcon build`: compile les packages ROS 2 du workspace.
- `source install/setup.bash`: charge les artefacts du build.
- `export MUTO_RS_REPO=/workspace`: definit une variable utile pour les scripts/launchers du projet.

## 3) Lancer Nav2 leader (BT leader integre)

Sans robot (TF de secours):

```bash
ros2 launch /workspace/src/muto_rs_nav_leader/launch/nav2_bringup.launch.py use_fake_map_tf:=True use_fake_odom_tf:=True use_fake_base_link_tf:=True
```

Avec robot reel:

```bash
ros2 launch /workspace/src/muto_rs_nav_leader/launch/nav2_bringup.launch.py use_fake_map_tf:=False use_fake_odom_tf:=False use_fake_base_link_tf:=False
```

Ce mode suppose que le robot publie deja `odom -> base_link`.
Si `odom` n'existe pas encore, utiliser temporairement:

```bash
ros2 launch /workspace/src/muto_rs_nav_leader/launch/nav2_bringup.launch.py use_fake_map_tf:=False use_fake_odom_tf:=True use_fake_base_link_tf:=True
```

## 4) Lancer Nav2 choreography

```bash
ros2 launch /workspace/src/muto_rs_nav_choregraphy/launch/nav2_bringup.launch.py use_fake_map_tf:=True use_fake_odom_tf:=True use_fake_base_link_tf:=True
```

Avec robot reel (TF deja publiees par le robot):

```bash
ros2 launch /workspace/src/muto_rs_nav_choregraphy/launch/nav2_bringup.launch.py use_fake_map_tf:=False use_fake_odom_tf:=False use_fake_base_link_tf:=False
```

Simulation Gazebo (horloge /clock):

```bash
ros2 launch /workspace/src/muto_rs_nav_choregraphy/launch/nav2_bringup.launch.py use_sim_time:=True
```

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
