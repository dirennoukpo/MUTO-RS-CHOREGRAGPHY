Les deux observations sont liées. Quand `torque_enabled=false`, `write()` fait `torqueOnRt → batch → torqueOffRt` — ce qui envoie les `command_positions_` aux servos avant de relâcher. Si les `command_positions_` sont à 0°, les servos reviennent à 0° à chaque cycle. C'est le comportement normal du cycle RT.

**La vraie question :** dans ce mode, veux-tu que les servos puissent être déplacés librement SANS recevoir de consigne de position ? Si oui, `write()` doit être bloqué — et les commandes de position ignorées. Si tu veux quand même envoyer des positions (téléopération manuelle douce), alors le retour en position est inévitable.

---

## Résumé complet de la situation actuelle

### Objectif initial
Ajouter `torque_enabled=false` dans le xacro MUTO pour libérer les servos manuellement.

---

### Architecture matérielle établie
- **Robot** : hexapode MUTO, 18 servos, carte baseboard STM32F103
- **Port** : `/dev/ttyUSB0` à 115200 baud — unique port pour tout
- **Protocole** : `55 00 [LEN] [INS] [ADDR] [DATA...] [CHK] 00 AA`
- **READ `0x02`** → fonctionne, répond
- **WRITE `0x01`** → fonctionne pour positions ET torque (si envoyé correctement)

---

### Découvertes critiques par reverse-engineering

| Découverte | Impact |
|---|---|
| `torqueOff` broadcast `0xFE` ignoré par le firmware STM32 | Toutes les premières tentatives ont échoué |
| Il faut envoyer servo par servo (IDs 1→18) avec délai inter-trame | Solution finale qui fonctionne |
| 5ms inter-trame pour init, 1ms pour cycle RT (comme code Python officiel) | `kTorqueDelayMs=5`, `kTorqueDelayRtMs=1` |
| Registres : `0x26`=TORQUE_ON, `0x27`=TORQUE_OFF (data=servo_id) | Confirmé par doc + code source officiel |
| Une commande position `0x40` ne réactive PAS le torque | `write()` peut fonctionner sans re-locker le torque |
| Lecture angles `0x50` fonctionne correctement | `read_state_from_hardware()` opérationnel |

---

### État des fichiers livrés

**`Muto_complet_urdf.xacro`** ✅
```xml
<param name="torque_enabled">true</param>   <!-- false = mode passif -->
<param name="update_state_from_hardware">false</param>
```

**`driver.hpp` / `driver.cpp`** ✅
- `torqueOnServo(id)` / `torqueOffServo(id)` — unitaires
- `torqueOn()` / `torqueOff()` — boucle 1→18, délai 5ms, pour init/shutdown
- `torqueOnRt()` / `torqueOffRt()` — boucle 1→18, délai 1ms, pour cycle RT
- Registres corrigés : `kRegTorqueOn=0x26`, `kRegTorqueOff=0x27`

**`muto_hardware.cpp`** ✅ (partiellement)
- Paramètre `torque_enabled_` parsé, logué, opérationnel
- Thread `torqueOff` continu supprimé (inutile)
- `read()` : lecture hardware forcée si `torque_enabled=false`
- `write()` : `torqueOnRt → batch → torqueOffRt` si `torque_enabled=false`

---

### Problèmes restants ❌

**1. Rate trop élevé quand `torque_enabled=false`**
Le cycle complet en mode passif :
- `read()` : ~26ms (lecture hardware)
- `write()` : ~18ms (torqueOnRt) + ~2ms (batch) + ~18ms (torqueOffRt) = ~38ms
- Total : ~64ms → **10Hz max**, pas 25Hz comme estimé

**2. Retour en position quand `torque_enabled=false`**
`write()` envoie les `command_positions_` (positions commandées, souvent 0°) avant de relâcher le torque → les servos reviennent systématiquement à la dernière consigne reçue à chaque cycle.

---

### Décision à prendre

Le mode `torque_enabled=false` a deux usages distincts qui sont incompatibles entre eux :

**Usage A — Déplacement manuel libre (calibration, pose à la main)**
- Les servos doivent être totalement libres, aucune consigne envoyée
- `write()` doit être **bloqué**
- `read()` lit les angles réels → `/joint_states` suit les positions manuelles
- Rate : 10Hz (lecture seule ~26ms + overhead)

**Usage B — Contrôle souple avec relâchement entre commandes**
- Les servos reçoivent une consigne puis se relâchent
- `write()` actif avec `torqueOnRt → batch → torqueOffRt`
- Mais les servos reviennent à la consigne → comportement "ressort"
- Rate : 10Hz

---

### Recommandation

Séparer les deux usages avec un second paramètre dans le xacro :

```xml
<param name="torque_enabled">false</param>
<!-- write_when_passive=true  → envoie les positions puis relâche (comportement ressort) -->
<!-- write_when_passive=false → aucune commande envoyée, déplacement manuel libre     -->
<param name="write_when_passive">false</param>
```