## Résumé complet final — mis à jour

### Objectif initial
Ajouter `torque_enabled=false` dans le xacro MUTO pour libérer les servos manuellement (déplacer les pattes à la main sans résistance).

---

### Architecture matérielle établie
- **Robot** : hexapode MUTO, 18 servos, carte baseboard MUTO
- **Communication** : un seul port série `/dev/ttyUSB0` à 115200 baud
- **Protocole** : `55 00 [LEN] [INS] [ADDR] [DATA...] [CHK] 00 AA`
  - `INS=0x02` → READ ✅ fonctionne, répond
  - `INS=0x01` → WRITE ⚠️ fonctionne pour les positions `0x40`, ignoré pour `0x26`/`0x27`
- **Port** `/dev/ttyAMA10` existe mais ne répond à rien

---

### Travail réalisé côté code ✅

**`Muto_complet_urdf.xacro`** — paramètre ajouté :
```xml
<param name="torque_enabled">true</param>  <!-- false = servos libres -->
```

**`muto_hardware.cpp`** — 6 modifications :
1. Membre `bool torque_enabled_{true}` ajouté
2. Parsing du paramètre dans `on_init`
3. `on_activate` → `torqueOn()` ou `torqueOff()` selon le flag
4. `release_driver` → `torqueOff()` conditionnel
5. `write()` bloqué quand `torque_enabled=false`
6. Thread `torqueOff` dédié à 100Hz quand `torque_enabled=false`

**`driver.cpp` / `driver.hpp`** — inchangés, `0x26`/`0x27` confirmés corrects par la doc officielle.

---

### Chronologie des tentatives ❌

| # | Tentative | Résultat |
|---|-----------|----------|
| 1 | `torqueOff()` dans `on_activate` uniquement | Ignoré |
| 2 | `torqueOff()` après chaque `writeRaw()` | Ignoré |
| 3 | `write()` bloqué + `torqueOff()` au démarrage | Ignoré |
| 4 | Thread dédié 100Hz répétant `torqueOff()` | Ignoré |
| 5 | Trame brute Python directe `55 00 09 01 27 00 CE 00 AA` | Pas de réponse |
| 6 | Test sur `ttyAMA10` tous baud rates | Pas de réponse |
| 7 | Écriture registres `0x26/0x27/0x28` via driver Python | 1 servo corrompu → **reset effectué** ✅ |

---

### Diagnostic final établi

**Les commandes READ (`0x02`) fonctionnent parfaitement** — batterie, IMU, angles répondent.

**Les commandes WRITE (`0x01`) ont un comportement asymétrique** :
- Trames position `0x40` via cycle RT ROS2 à 50Hz → **servos bougent** ✅
- Trame `torque_off` `0x27` standalone → **aucun effet** ❌
- Trame `torque_on` `0x26` standalone → **aucun effet observable** ❌

**Conclusion** : la commande `torque_off` `0x27` existe dans la documentation officielle MUTO mais **n'est pas implémentée dans le firmware** de cette version de la carte, ou nécessite une condition préalable non documentée. Les commandes de mouvement fonctionnent car elles sont envoyées en continu à 50Hz dans le format exact attendu par le firmware.

---

### État actuel des fichiers livrés

| Fichier | État |
|---------|------|
| `Muto_complet_urdf.xacro` | ✅ Paramètre `torque_enabled` ajouté, prêt |
| `muto_hardware.cpp` | ✅ Logique complète, fonctionnel — torque_off inefficace côté hardware |
| `driver.cpp` / `driver.hpp` | ✅ Inchangés, `0x26`/`0x27` corrects |
| `test_torque.py` | ✅ Script de diagnostic disponible |

---

### Prochaines étapes recommandées

**Court terme — alternatives logicielles :**
- Tester `torque_off` (`0x27`) immédiatement après power-on avant tout autre échange série
- Tester la commande `0x10` (Squat) qui selon la doc "réduit la consommation des servos" — pas un vrai torque off mais peut réduire la résistance passive
- Contacter le support MUTO ou chercher une mise à jour firmware

**Moyen terme — alternative hardware :**
- Couper l'alimentation des servos via un GPIO ou relais commandé par ROS2 — solution radicale mais garantie à 100%

**À ne plus faire :**
- ⛔ Écrire sur des registres inconnus via le driver Python directement — risque de corruption EEPROM servo