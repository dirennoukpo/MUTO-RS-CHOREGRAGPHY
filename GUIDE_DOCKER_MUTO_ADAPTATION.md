# Guide detaille Docker MUTO-RS et adaptation a un autre projet

## Objectif

Ce document explique de maniere pratique tout ce qui a ete fait autour du container Docker dans le contexte MUTO-RS, puis montre comment reproduire la meme logique pour un autre projet.

Important: dans MUTO-RS, la methode principale n'est pas de construire une image locale avec un Dockerfile. La methode principale est:

1. tirer une image deja prete,
2. lancer un container avec un script de demarrage,
3. monter les peripheriques et dossiers necessaires,
4. reutiliser ce container pour le developpement ROS2.

## 1) Ce qui a ete fait sur MUTO-RS (vue d'ensemble)

### 1.1 Image de base utilisee

- Image officielle: yahboomtechnology/ros-foxy-muto
- Version observee dans la doc: 2.5.0 (d'autres tags existent selon mise a jour)
- Base logicielle: Ubuntu 20.04 + ROS2 Foxy + workspace robot

### 1.2 Cycle reel utilise

Le flux concret dans la doc est:

1. verifier/installer Docker sur l'hote,
2. tirer l'image,
3. creer un script run_docker.sh,
4. lancer docker run avec options reseau/GUI/devices/ports/volumes,
5. sortir du container,
6. revenir plus tard avec docker start puis docker exec,
7. mettre a jour l'image quand un nouveau tag est disponible.

## 2) Etapes detaillees: creation et execution du container

## 2.1 Preparation machine hote

### A. Permissions Docker (probleme frequent)

Si message de type permission denied sur ~/.docker/config.json:

    sudo chown "$USER":"$USER" /home/"$USER"/.docker -R
    sudo chmod g+rwx "/home/$USER/.docker" -R

### B. Verifications utiles

    docker info
    docker --help

## 2.2 Recuperation image

Exemples documentes:

    docker pull ubuntu
    docker pull yahboomtechnology/ros-foxy-muto:2.5.0

Puis verifier les tags disponibles localement:

    docker images

Regle pratique retenue dans la doc MUTO:

- choisir le tag le plus recent de yahboomtechnology/ros-foxy-muto.

## 2.3 Premier lancement du container (creation)

La creation est faite avec docker run (pas avec docker create):

    docker run -it IMAGE /bin/bash

Dans MUTO-RS, le run est encapsule dans un script run_docker.sh pour eviter de retaper une longue commande.

## 2.4 Script de demarrage type run_docker.sh

Version complete issue des docs (a adapter selon materiel branche):

    #!/bin/bash
    xhost +

    docker run -it \
      --net=host \
      --env="DISPLAY" \
      --env="QT_X11_NO_MITSHM=1" \
      -v /tmp/.X11-unix:/tmp/.X11-unix \
      -v ~/temp:/root/yahboomcar_ros2_ws/temp \
      -v /dev/bus/usb/001/010:/dev/bus/usb/001/010 \
      -v /dev/bus/usb/001/011:/dev/bus/usb/001/011 \
      --device=/dev/astradepth \
      --device=/dev/astrauvc \
      --device=/dev/video0 \
      --device=/dev/myserial \
      --device=/dev/rplidar \
      --device=/dev/input \
      -p 9090:9090 \
      -p 8888:8888 \
      yahboomtechnology/ros-foxy-muto:2.5.0 /bin/bash

Option parfois necessaire sur certaines plateformes (ex: souci GUI/AppArmor):

    --security-opt apparmor:unconfined

Extension voice control documentee:

    --device=/dev/myspeech

Point tres important:

- si un device n'existe pas sur l'hote, retirer la ligne correspondante avant lancement.

## 2.5 Explication fonctionnelle des options Docker

- -it: terminal interactif.
- --net=host: partage du reseau hote, utile ROS2/Jupyter/decouverte reseau.
- --env DISPLAY + montage /tmp/.X11-unix: affichage GUI (rviz2, outils graphiques).
- -v host:container: partage de fichiers persistant.
- --device=/dev/...: exposition de peripheriques reels au container (Lidar, camera, serie, etc.).
- -p 9090:9090, -p 8888:8888: publication de ports (ex: web tools, notebook).

## 2.6 Revenir dans un container deja cree

Une fois le container cree puis quitte (etat Exited):

    docker ps -a
    docker start <container_id_ou_nom>
    docker exec -it <container_id_ou_nom> /bin/bash

Pourquoi cette methode:

- elle conserve l'etat du container tel qu'il a ete laisse.

## 2.7 Arret et nettoyage

    docker stop <container>
    docker rm -f <container>
    docker rmi <image>

## 2.8 Variables d'environnement ROS dans le container

Variables notees dans l'environnement MUTO:

    export ROS_DOMAIN_ID=26
    export my_lidar=a1
    export my_camera=astra
    source /opt/ros/foxy/setup.bash
    source /root/yahboomcar_ros2_ws/yahboomcar_ws/install/setup.bash

Utilite:

- rendre les commandes ROS2 directement operationnelles a l'ouverture du shell container.

## 2.9 Mise a jour image MUTO

Processus observe:

1. verifier version locale via docker images,
2. tirer nouveau tag:

    docker pull yahboomtechnology/ros-foxy-muto:<nouveau_tag>

3. relancer un nouveau container base sur la nouvelle image.

## 2.10 Sauvegarder des modifications

Deux strategies presentes:

### A. Recommandee: volumes

- les donnees importantes restent sur l'hote meme si le container est supprime.

Exemple:

    docker run -it -v /home/jetson/temp:/root/temp yahboomtechnology/ros-foxy:3.4.0 /bin/bash

### B. Ponctuelle: docker commit

Exemple:

    docker commit <container_id> yahboomtechnology/ros-foxy-muto:1.1

Limite:

- pratique pour un snapshot rapide, moins robuste qu'un Dockerfile versionne.

## 3) Ce qu'il faut reprendre pour un autre projet

## 3.1 Methode de migration MUTO -> Nouveau projet

### Etape 1: definir besoins

Faire une matrice simple:

- GUI requise ou non,
- devices necessaires,
- ports necessaires,
- dossiers a persister,
- reseau host obligatoire ou bridge possible.

### Etape 2: choisir l'image de base

Cas A (proche MUTO/ROS): partir d'une image existante officielle.

Cas B (projet standard): creer votre propre image via Dockerfile.

### Etape 3: ecrire un script de lancement parametre

Creer un run_docker.sh generique, avec blocs optionnels commentes.

### Etape 4: ajouter seulement ce qui est necessaire

- retirer les --device inutiles,
- garder seulement les -p utiles,
- conserver un ou deux volumes clairs.

### Etape 5: valider

Checklist rapide:

1. le container demarre,
2. les peripheriques apparaissent dans /dev,
3. GUI fonctionne si necessaire,
4. les fichiers dans le volume se voient des deux cotes,
5. l'application demarre sans erreur.

## 3.2 Template run_docker.sh adaptable

    #!/bin/bash
    set -e

    IMAGE="monorg/monprojet:1.0.0"
    NAME="monprojet_dev"

    # Option GUI Linux (decommenter si besoin)
    # xhost +local:docker

    docker run -it --rm \
      --name "$NAME" \
      --net=host \
      -e DISPLAY="$DISPLAY" \
      -e QT_X11_NO_MITSHM=1 \
      -v /tmp/.X11-unix:/tmp/.X11-unix \
      -v "$HOME"/monprojet-data:/workspace/data \
      --device=/dev/video0 \
      -p 8888:8888 \
      "$IMAGE" \
      /bin/bash

Notes:

- retirer --net=host si inutile.
- retirer GUI si projet sans interface.
- remplacer --rm par un container persistant si besoin d'etat local.

## 3.3 Template Dockerfile minimal (si vous voulez construire votre image)

    FROM ubuntu:22.04

    ENV DEBIAN_FRONTEND=noninteractive

    RUN apt-get update && apt-get install -y \
        python3 python3-pip \
        git curl ca-certificates \
        && rm -rf /var/lib/apt/lists/*

    WORKDIR /workspace

    COPY requirements.txt /tmp/requirements.txt
    RUN pip3 install --no-cache-dir -r /tmp/requirements.txt

    COPY . /workspace

    CMD ["bash"]

Build et run associes:

    docker build -t monorg/monprojet:1.0.0 .
    docker run -it --name monprojet_dev monorg/monprojet:1.0.0

## 4) Bonnes pratiques a retenir de MUTO-RS

1. Toujours partir d'une commande run stable centralisee dans un script versionne.
2. Monter seulement les devices necessaires (principe de moindre privilege).
3. Privilegier les volumes pour la persistance des donnees de travail.
4. Garder une logique simple:
   - creation: docker run,
   - reutilisation: docker start + docker exec.
5. Mettre un controle de version d'image clair (tag explicite).
6. Eviter de dependre de docker commit comme strategie principale.
7. Ajouter une checklist de validation apres chaque changement de script.

## 5) Procedure courte reutilisable (copier-coller mental)

1. Choisir image et tag.
2. Ecrire run_docker.sh.
3. Ajouter reseau/GUI/ports/devices/volumes utiles uniquement.
4. Lancer, tester, simplifier.
5. Documenter la version d'image et la commande finale.
6. Automatiser ensuite avec Makefile ou docker compose si le projet grossit.

## 6) Points d'attention pour adapter a un autre projet

- ROS2 et middleware reseau: host networking souvent plus simple.
- Projet web/API standard: bridge + ports explicites suffisent.
- Materiel USB/serie/camera: preparer les regles udev et verifier les chemins /dev.
- GUI Linux: DISPLAY + xhost + X11 socket.
- En environnement securise: eviter xhost + global et limiter les devices montes.

## 7) Conclusion

Le workflow MUTO-RS repose surtout sur un container de dev lance par script (docker run riche en options), pas sur une construction d'image locale systematique. Pour ton nouveau projet, la bonne strategie est de reprendre cette architecture en la simplifiant au strict necessaire, puis de n'ajouter options/device/ports que lorsque le besoin est prouve.