# Dossier assets/

## Vue d'Ensemble

Le dossier `assets/` contient toutes les ressources de données nécessaires au fonctionnement du système MUTO-RS-CHOREOGRAPHY. Ces ressources sont principalement des fichiers audio et leurs analyses temporelles (timelines) qui permettent la synchronisation musicale des robots.

## Structure Générale

```
assets/
├── audio/                    # 🎵 Pistes audio MP3
│   ├── 100¯roDanceVol.1--Petit Afro.mp3
│   ├── African Drums - African Percussion.mp3
│   ├── Aqua-BarbieGirl-OfficialDanceforKidsVideo.mp3
│   ├── Mark Ronson - Uptown Funk (Official Video) ft. Bruno Mars.mp3
│   └── Your Idol - Official Song Clip - KPop Demon Hunters - Sony Animation.mp3
└── json/                     # 📊 Timelines musicales analysées
    ├── choreo_himra_template.json
    ├── data.json
    ├── AMAZING-ROBOT-DANCE GROUP-TECHNICIANZ.json.json
    ├── bloody_brazil_ultra_slowed.json
    ├── tenzoo_bloody_brazil_beats.json
    └── Your Idol - Official Song Clip - KPop Demon Hunters - Sony Animation.json
```

## Contenu Détaillé

### 🎵 Dossier audio/

**Rôle** : Collection de pistes audio MP3 utilisées pour les chorégraphies robotiques.

**Formats supportés** :
- **MP3** : Format principal (compressé, bonne qualité)
- **WAV/FLAC** : Formats non compressés (optionnel)

**Sources des pistes** :
- **Musiques originales** : Créées spécifiquement pour la danse robotique
- **Adaptations** : Versions modifiées de chansons populaires
- **Genres variés** : Afrobeat, K-pop, Brazil, Dance, etc.

**Caractéristiques audio** :
- **Durée** : 2-5 minutes typiquement
- **BPM** : 80-140 (adapté aux capacités robotiques)
- **Structure** : Intro → Versets → Chorus → Outro
- **Qualité** : 320kbps minimum pour analyse précise

#### Utilisation

```bash
# Lecture avec ffplay (player par défaut)
ffplay assets/audio/song.mp3

# Analyse avec decodeur.py
python3 scripts/decodeur.py \
  --input assets/audio/song.mp3 \
  --output assets/json/song_timeline.json
```

---

### 📊 Dossier json/

**Rôle** : Fichiers JSON contenant les analyses temporelles détaillées des pistes audio.

**Génération** : Produits automatiquement par `scripts/decodeur.py`

**Structure JSON standard** :

```json
{
  "metadata": {
    "source_file": "song.mp3",
    "duration_s": 180.5,
    "bpm": 120.0,
    "sample_rate": 44100,
    "channels": 2
  },
  "beats": [
    {
      "time_s": 0.5,
      "position": 1,
      "downbeat": true,
      "intensity": 0.3
    }
  ],
  "onsets": [0.1, 0.45, 1.2, 2.1],
  "energy": [0.2, 0.3, 0.8, 0.6],
  "flux": [0.1, 0.25, 0.9, 0.4],
  "intensity": [0.15, 0.275, 0.85, 0.5],
  "sections": [
    {
      "label": "intro",
      "start_s": 0.0,
      "end_s": 15.2,
      "intensity_quartile": 1,
      "confidence": 0.85
    },
    {
      "label": "verse",
      "start_s": 15.2,
      "end_s": 45.8,
      "intensity_quartile": 2,
      "confidence": 0.92
    }
  ]
}
```

#### Champs Détaillés

##### metadata
Informations générales sur l'analyse :
- `source_file` : Nom du fichier audio source
- `duration_s` : Durée totale en secondes
- `bpm` : Battements par minute estimés
- `sample_rate` : Fréquence d'échantillonnage
- `channels` : Nombre de canaux audio

##### beats
Positions temporelles des battements :
- `time_s` : Position en secondes depuis le début
- `position` : Position dans la mesure (1-4 pour 4/4)
- `downbeat` : True si c'est un downbeat (premier temps)
- `intensity` : Niveau d'intensité musicale locale

##### onsets
Temps des attaques rythmiques détectées (en secondes).

##### energy
Profil d'énergie RMS lissé (normalisé 0-1).

##### flux
Flux spectral lissé (normalisé 0-1).

##### intensity
Score d'intensité composite : `0.5 × energy + 0.5 × flux`

##### sections
Segmentation structurelle automatique :
- `label` : Type de section (intro, verse, chorus, outro)
- `start_s` / `end_s` : Bornes temporelles
- `intensity_quartile` : Quartile d'intensité (1-4)
- `confidence` : Confiance de la classification

---

## Pipeline d'Analyse Audio

### Étapes du Traitement

1. **Chargement Audio**
   - Lecture MP3 avec `librosa`
   - Conversion en tableau numpy float32

2. **Séparation HPSS**
   - Harmonic/Percussive Source Separation
   - Focus sur le signal percussif pour le rythme

3. **Beat Tracking**
   - Utilisation `madmom` avec `tightness=100`
   - Détection précise des battements

4. **Downbeat Estimation**
   - Calcul des positions relatives (1-4)
   - Correction basée sur le tempo

5. **Détection d'Onsets**
   - Analyse du signal percussif
   - Filtrage des faux positifs

6. **Extraction de Features**
   - Énergie RMS (root mean square)
   - Flux spectral (spectral flux)
   - Lissage temporel

7. **Calcul d'Intensité**
   - Fusion énergie + flux
   - Normalisation 0-1

8. **Segmentation Structurelle**
   - Clustering MFCC (Mel-frequency cepstral coefficients)
   - Classification automatique des sections

9. **Étiquetage**
   - Mapping vers labels musicaux
   - Basé sur quartiles d'intensité

10. **Sérialisation JSON**
    - Export structuré pour `dance_leader.py`

### Algorithmes Utilisés

| Étape | Algorithme | Bibliothèque | Paramètres |
|-------|------------|--------------|------------|
| HPSS | Median filtering | librosa | power=2, margin=1.0 |
| Beat tracking | Dynamic programming | madmom | tightness=100 |
| Onset detection | Spectral flux | aubio | threshold=0.3 |
| Energy | RMS | essentia | frameSize=2048 |
| Flux | Spectral flux | essentia | frameSize=2048 |
| Segmentation | Agglomerative clustering | sklearn | n_clusters=4 |
| MFCC | Standard | librosa | n_mfcc=13 |

---

## Utilisation dans le Système

### Intégration avec dance_leader.py

```python
# Chargement de la timeline
with open('assets/json/song_timeline.json', 'r') as f:
    timeline = json.load(f)

# Accès aux données temporelles
beats = timeline['beats']
sections = timeline['sections']
intensity = timeline['intensity']

# Synchronisation musicale
for beat in beats:
    if beat['downbeat']:
        # Déclencher mouvement principal
        publish_dance_command('ACTION:8')  # Stride
```

### Adaptation Dynamique

Le système utilise l'`intensity` pour adapter l'agressivité :
- **Intensity < 0.3** : Mouvements calmes (niveau 0)
- **Intensity 0.3-0.5** : Mouvements modérés (niveau 1)
- **Intensity 0.5-0.7** : Mouvements actifs (niveau 2)
- **Intensity > 0.7** : Mouvements intenses (niveau 3)

### Sections Musicales

Mapping automatique vers palettes de mouvement :
- **Intro/Outro** : Mouvements d'approche/retraite
- **Verse** : Mouvements latéraux, tours
- **Chorus** : Mouvements verticaux, sauts
- **Bridge/Drop** : Mouvements complexes, enchaînements

---

## Gestion et Maintenance

### Organisation des Fichiers

**Conventions de nommage** :
- Audio : `descriptive_name.mp3`
- JSON : `descriptive_name.json` (même nom que l'audio)

**Structure par projet** :
```
assets/
├── audio/
│   ├── project_a/
│   │   ├── song1.mp3
│   │   └── song2.mp3
│   └── project_b/
│       └── song3.mp3
└── json/
    ├── project_a/
    │   ├── song1.json
    │   └── song2.json
    └── project_b/
        └── song3.json
```

### Outils de Gestion

#### Validation des Timelines

```bash
# Vérifier la cohérence
python3 -c "
import json
with open('assets/json/song.json') as f:
    data = json.load(f)
print(f'Duration: {data[\"metadata\"][\"duration_s\"]}s')
print(f'Beats: {len(data[\"beats\"])}')
print(f'Sections: {len(data[\"sections\"])}')
"
```

#### Nettoyage

```bash
# Supprimer timelines orphelines
find assets/json/ -name "*.json" | while read f; do
    audio="${f%.json}.mp3"
    if [ ! -f "assets/audio/$audio" ]; then
        echo "Orphan: $f"
        # rm "$f"
    fi
done
```

#### Statistiques

```bash
# Compter les fichiers
echo "Audio files: $(find assets/audio/ -name "*.mp3" | wc -l)"
echo "Timeline files: $(find assets/json/ -name "*.json" | wc -l)"

# Durée totale
find assets/audio/ -name "*.mp3" -exec ffprobe -v quiet -print_format json -show_format {} \; | \
jq -r '.format.duration' | paste -sd+ | bc
```

---

## Formats et Standards

### Spécifications Audio

- **Codec** : MP3 (MPEG-1 Audio Layer III)
- **Bitrate** : 320 kbps CBR recommandé
- **Sample Rate** : 44.1 kHz
- **Channels** : Stéréo (2 canaux)
- **Bit Depth** : 16-bit

### Métadonnées JSON

- **Version** : Sérialisation JSON standard (RFC 8259)
- **Encoding** : UTF-8
- **Indentation** : 2 espaces (lisible)
- **Flottants** : Précision 6 décimales
- **Tri** : Clés triées alphabétiquement

### Validation

```python
import jsonschema

schema = {
    "type": "object",
    "required": ["metadata", "beats", "sections"],
    "properties": {
        "metadata": {
            "type": "object",
            "required": ["source_file", "duration_s"]
        },
        "beats": {
            "type": "array",
            "items": {
                "type": "object",
                "required": ["time_s", "position"]
            }
        }
    }
}

# Validation
jsonschema.validate(timeline_data, schema)
```

---

## Évolution et Extension

### Nouvelles Features Audio

- **Tempo variable** : Support des changements de BPM
- **Signature rythmique** : Détection 3/4, 6/8, etc.
- **Harmony analysis** : Analyse des accords
- **Voice separation** : Isolation vocale/instrumentale

### Formats Supportés Additionnels

- **M4A/AAC** : Pour fichiers plus compacts
- **FLAC** : Pour analyse haute qualité
- **WAV 24-bit** : Pour production professionnelle

### Intégration Cloud

- **Stockage S3** : Assets volumineux
- **CDN** : Distribution rapide
- **Cache** : Téléchargement intelligent
- **Versioning** : Gestion des révisions

---

*Le dossier `assets/` est crucial pour la fonctionnalité musicale du système MUTO-RS-CHOREOGRAPHY. Il transforme la musique en données structurées exploitables par les algorithmes de danse synchronisée.*</content>
<parameter name="filePath">/home/edwin/TEKBOT-ROBOTICS-BENIN/MUTO-RS-CHOREGRAGPHY/assets/README.md