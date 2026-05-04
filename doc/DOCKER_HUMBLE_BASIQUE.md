# Docker ROS Humble basique

Ce workspace fournit un container Docker simple pour developper avec ROS 2 Humble sur Ubuntu 22.04.

## Contenu

- docker/Dockerfile: image de dev ROS Humble minimale
- docker/entrypoint.sh: source automatique de ROS
- docker-compose.yaml: service unique ros-humble-dev
- docker/run_docker.sh: build + demarrage + shell interactif

## Prerequis

- Docker
- Docker Compose v2 (commande: docker compose)

## Lancer le container

Depuis la racine du projet:

```bash
./docker/run_docker.sh
```

## Commandes utiles

Construire et lancer en arriere-plan:

```bash
docker compose up -d --build
```

Entrer dans le container:

```bash
docker exec -it ros-humble-dev bash
```

Arreter le container:

```bash
docker compose down
```

## Installer tes outils personnels

Dans le container:

```bash
sudo apt update
sudo apt install -y <ton-paquet>
```

Si tu veux conserver durablement tes outils, ajoute-les dans docker/Dockerfile puis rebuild:

```bash
docker compose up -d --build
```
