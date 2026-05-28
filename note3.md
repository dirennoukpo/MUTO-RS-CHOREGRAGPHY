## Résumé total du projet MUTO — Torque Control

---

### Contexte général
Robot hexapode MUTO, 18 servos, carte baseboard STM32F103. Le Raspberry Pi communique via ROS2 + ros2_control. L'objectif final est de déployer une **politique RL entraînée dans Isaac Lab / Isaac Sim** pour la locomotion et l'équilibre. La fonctionnalité `torque_enabled` a été développée pour permettre la calibration manuelle avant le déploiement de la politique.

---

### Architecture matérielle établie

- **Port unique** : `/dev/ttyUSB0` à 115200 baud — sert à tout (servos + IMU + batterie)
- **Protocole MUTO** : `55 00 [LEN] [INS] [ADDR] [DATA...] [CHK] 00 AA`
  - `INS=0x02` READ → répond toujours ✅
  - `INS=0x01` WRITE → fonctionne pour positions ET torque ✅
- **Checksum** : `255 - (LEN + INS + ADDR + SUM(DATA)) % 256`
- **Port** `/dev/ttyAMA10` existe mais n'est pas utilisé par le système

---

### Découvertes critiques par reverse-engineering

| # | Découverte | Impact |
|---|---|---|
| 1 | Le broadcast `0xFE` est **ignoré** par le firmware STM32 pour les commandes torque | Toutes les premières tentatives ont échoué |
| 2 | Il faut envoyer **une trame par servo** (IDs 1→18) avec délai inter-trame | Solution finale qui fonctionne |
| 3 | Délai minimal : **5ms** entre chaque servo (certains récalcitrants nécessitent plusieurs passes) | `kTorqueDelayMs=5` |
| 4 | Registres : `0x26`=TORQUE_ON, `0x27`=TORQUE_OFF, **data=servo_id** (pas 0x00 ou 0x01) | Confirmé par doc officielle + code source Python officiel |
| 5 | Une commande position `0x40` **ne réactive pas** le torque | `write()` peut fonctionner indépendamment du torque |
| 6 | Lecture angles `0x50` fonctionne correctement en READ | `read_state_from_hardware()` opérationnel |
| 7 | Le "retour en position" observé = comportement **normal et attendu** pour la politique RL | Pas un bug — c'est l'objectif final |

---

### Chronologie des tentatives torque (toutes échouées avant la découverte #1-4)

| # | Tentative | Pourquoi ça a échoué |
|---|---|---|
| 1 | `torqueOff()` broadcast au démarrage | Broadcast 0xFE ignoré par firmware |
| 2 | Thread dédié 100Hz répétant `torqueOff()` | Même raison |
| 3 | `write()` bloqué + `torqueOff()` au démarrage | Même raison |
| 4 | Trame brute Python `55 00 09 01 27 00 CE 00 AA` | data=0x00 au lieu de servo_id |
| 5 | Test sur `ttyAMA10` tous baud rates | Port non utilisé par la carte |
| 6 | Registres `0x26/0x27/0x28` via driver Python | A corrompu un servo (reset effectué ✅) |
| 7 | Servo par servo avec 1ms de délai | Fonctionne partiellement (3 récalcitrants) |
| 8 | Servo par servo avec 5ms + plusieurs passes | **Fonctionne** ✅ |

---

### État final des fichiers

**`Muto_complet_urdf.xacro`** ✅
```xml
<!-- Contrôle du torque -->
<param name="torque_enabled">true</param>
<!-- false = calibration manuelle, true = politique RL active -->

<!-- Lecture hardware (debug uniquement) -->
<param name="update_state_from_hardware">false</param>
<!-- true = lecture série ~26ms, ≤20Hz | false = state=command, 50Hz -->
```

---

**`driver.hpp` / `driver.cpp`** ✅
```
torqueOnServo(id)   → trame individuelle TORQUE_ON  (0x26, data=id)
torqueOffServo(id)  → trame individuelle TORQUE_OFF (0x27, data=id)
torqueOn()          → boucle 1→18, 5ms inter-servo (init/shutdown)
torqueOff()         → boucle 1→18, 5ms inter-servo (init/shutdown)
writeRaw(batch)     → batch 18 trames position en un seul write système
```

---

**`muto_hardware.cpp`** ✅

Comportement selon les paramètres :

| `torque_enabled` | `update_state_from_hardware` | `write()` | `read()` | Rate max |
|---|---|---|---|---|
| `true` | `false` | batch direct ~2ms | state = command 0ms | **50Hz** |
| `true` | `true` | batch direct ~2ms | lecture hardware ~26ms | **20Hz** |
| `false` | `false` ou `true` | **bloqué** | lecture hardware ~26ms | **10Hz** |

---

### Workflow concret

```
MAINTENANT (sans politique)
  torque_enabled = false
       ↓
  on_activate → torqueOff() servo par servo (5ms × 18 = 90ms, une seule fois)
       ↓
  write() bloqué → servos libres, déplacement à la main possible
  read()  → lecture angles réels → /joint_states suit les positions manuelles
       ↓
  RViz / Isaac Lab reçoit les angles réels → collecte de données, calibration

PLUS TARD (politique RL prête)
  torque_enabled = true
       ↓
  on_activate → torqueOn() servo par servo
       ↓
  write() → batch 18 servos ~2ms à 50Hz
  La politique envoie ses consignes → robot maintient l'équilibre
  Perturbation externe → robot résiste et revient en position (comportement attendu)
```

---

### Ce qui reste à faire côté robot
1. Entraîner la politique dans **Isaac Lab / Isaac Sim** (locomotion + équilibre)
2. Déployer le modèle ONNX dans `/muto_ws/install/muto_description/share/muto_description/config/`
3. Passer `torque_enabled=true` et lancer `bringup.launch.py`
4. Le nœud `muto_policy_node` (déjà présent) chargera le modèle ONNX et enverra les consignes via `/leg_controller/commands`