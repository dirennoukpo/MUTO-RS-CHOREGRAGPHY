# Docker - Prise en main

Ce dossier contient les fichiers Docker utilises pour l'environnement de dev ROS 2 Humble.

## Methode unique recommandee (depuis la racine du repo)

1. Initialiser le fichier d'environnement:

```bash
make env
```

Description: copie le template `config/.env.example` vers `config/.env` (sans ecraser un fichier deja present).

2. Builder l'image:

```bash
make build
```

Description: construit l'image Docker `ros-humble-dev:latest` avec les variables utilisateur de la machine hote.

3. Demarrer l'environnement:

```bash
make up
```

Description: demarre le service en arriere-plan via Docker Compose.

4. Ouvrir un shell dans le container:

```bash
make shell
```

Description: ouvre une session Bash interactive dans le service `ros-humble-dev`.

5. Arreter les services:

```bash
make down
```

Description: stoppe et supprime les services/containers du projet.

## Option legacy (si necessaire)

`./docker/run_docker.sh` est conserve pour compatibilite, mais le flux standard doit rester le Makefile.

## Fichiers du dossier

- Dockerfile: construction de l'image de dev ROS 2
- entrypoint.sh: preparation de l'environnement au demarrage
- run_docker.sh: script de lancement historique

## Notes utiles

- Le service Docker Compose est nomme `ros-humble-dev`.
- Le workspace local est monte dans le container sur `/workspace`.
- Les UID/GID utilisateur hote sont passes automatiquement via le Makefile.
- La variable `ROS_DOMAIN_ID` se configure dans `config/.env`.