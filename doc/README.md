# Dossier doc/

## Vue d'Ensemble

Le dossier `doc/` contient toute la documentation technique du projet MUTO-RS-CHOREOGRAPHY. Cette documentation couvre l'architecture, les API, les guides d'utilisation et les spécifications techniques nécessaires à la compréhension et à la maintenance du système.

## Structure Générale

```
doc/
└── DOCKER_HUMBLE_BASIQUE.md    # 🐳 Guide Docker de base
```

---

## Documentation Existante

### DOCKER_HUMBLE_BASIQUE.md

**Rôle** : Guide d'introduction à l'utilisation de Docker avec ROS 2 Humble

**Contenu** :
- Installation de Docker
- Configuration de l'environnement
- Utilisation basique des conteneurs ROS 2
- Commandes essentielles
- Dépannage courant

**Public cible** : Nouveaux développeurs rejoignant le projet

**Mise à jour** : Nécessaire lors de changements majeurs dans l'infrastructure Docker

---

## Structure Recommandée

### Documentation par Audience

```
doc/
├── user/                      # 👤 Guides utilisateur
│   ├── installation.md        # Installation pas à pas
│   ├── quickstart.md          # Démarrage rapide
│   ├── configuration.md       # Configuration avancée
│   └── troubleshooting.md     # Dépannage
├── developer/                 # 👨‍💻 Guides développeur
│   ├── architecture.md        # Architecture système
│   ├── api/                   # Documentation API
│   │   ├── dance_leader.md    # API dance_leader.py
│   │   ├── behavior_trees.md  # API BT nodes
│   │   └── ros_interfaces.md  # Topics et services
│   ├── contributing.md        # Guide contribution
│   └── testing.md             # Guide tests
├── admin/                     # 👨‍🔧 Guides administrateur
│   ├── deployment.md          # Déploiement production
│   ├── monitoring.md          # Monitoring et logging
│   └── security.md            # Sécurité
└── design/                    # 🎨 Documents design
    ├── requirements.md        # Spécifications fonctionnelles
    ├── system_design.md       # Architecture détaillée
    └── performance.md         # Analyse performance
```

### Documentation par Composant

```
doc/
├── components/
│   ├── synchronization/       # Orchestration danse
│   ├── navigation/            # Navigation autonome
│   ├── audio/                 # Traitement audio
│   └── docker/                # Infrastructure
├── protocols/                 # Protocoles communication
│   ├── dance_protocol.md      # Protocole danse
│   ├── ros_topics.md          # Topics ROS 2
│   └── bt_interfaces.md       # Interfaces BT
└── tools/                     # Outils et utilitaires
    ├── decodeur.md            # Guide analyseur audio
    ├── build_system.md        # Système de build
    └── ci_cd.md               # Intégration continue
```

---

## Formats de Documentation

### Markdown (.md)

**Avantages** :
- ✅ Lecture facile sur GitHub/GitLab
- ✅ Support natif dans VS Code
- ✅ Conversion possible en HTML/PDF
- ✅ Syntaxe simple

**Outils** :
- **GitHub Pages** : Publication automatique
- **MkDocs** : Génération site statique
- **Docusaurus** : Documentation interactive

### Autres Formats

#### reStructuredText (.rst)
- Utilisé pour documentation Python officielle
- Support Sphinx avancé
- Moins lisible nativement

#### AsciiDoc (.adoc)
- Alternative moderne au Markdown
- Support tableaux complexes
- Meilleure pour documentation technique

#### Documents Binaires
- **PDF** : Pour archivage et impression
- **Google Docs** : Collaboration en temps réel
- **Draw.io** : Diagrammes d'architecture

---

## Outils de Génération

### MkDocs

**Installation** :
```bash
pip install mkdocs mkdocs-material
```

**Configuration** (mkdocs.yml) :
```yaml
site_name: MUTO-RS Documentation
theme:
  name: material
  palette:
    primary: blue
    accent: light-blue

nav:
  - Home: index.md
  - User Guide: user/
  - Developer Guide: developer/
  - API Reference: api/

plugins:
  - search
  - mkdocstrings
```

**Utilisation** :
```bash
# Serveur local
mkdocs serve

# Build site
mkdocs build

# Déploiement
mkdocs gh-deploy
```

### Sphinx

**Installation** :
```bash
pip install sphinx sphinx-rtd-theme
```

**Configuration** (conf.py) :
```python
project = 'MUTO-RS-CHOREOGRAPHY'
copyright = '2026, Edwin Diren Noukpo'
author = 'Edwin Diren Noukpo'

extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.viewcode',
    'sphinx.ext.napoleon',
]

html_theme = 'sphinx_rtd_theme'
```

### Docusaurus

**Installation** :
```bash
npx create-docusaurus@latest doc-site classic
```

**Avantages** :
- React-based (moderne)
- Recherche intégrée
- Versioning automatique
- Internationalisation

---

## Génération Automatique

### Extraction de Code

#### Docstrings Python
```python
def analyze_audio(file_path: str) -> dict:
    """
    Analyse un fichier audio pour extraire les caractéristiques musicales.

    Cette fonction utilise librosa et madmom pour analyser le fichier
    audio et extraire les battements, onsets et autres features.

    Args:
        file_path (str): Chemin vers le fichier audio MP3

    Returns:
        dict: Dictionnaire contenant les features extraites

    Raises:
        FileNotFoundError: Si le fichier n'existe pas
        ValueError: Si le format audio n'est pas supporté

    Example:
        >>> features = analyze_audio('song.mp3')
        >>> print(features['bpm'])
        120.5
    """
    # Code...
```

#### Commentaires Doxygen (C++)
```cpp
/**
 * @brief Nœud Behavior Tree pour monitoring batterie
 *
 * Cette classe implémente un nœud StatefulActionNode qui surveille
 * le niveau de batterie des robots via ROS 2 topics.
 *
 * @section usage Exemple d'utilisation
 * @code{.xml}
 * <CheckBatteryLevel robot_id="1" min_battery_level="6.0"/>
 * @endcode
 */
class CheckBatteryLevel : public BT::StatefulActionNode {
public:
    /**
     * Constructeur du nœud
     * @param name Nom du nœud dans l'arbre
     * @param config Configuration BT
     */
    CheckBatteryLevel(const std::string &name, const BT::NodeConfiguration &config);
};
```

### Génération API Docs

#### Python avec pdoc
```bash
pip install pdoc
pdoc --html muto_rs_synchronization
```

#### C++ avec Doxygen
```bash
# Doxyfile
doxygen Doxyfile
```

#### ROS 2 avec rosdoc2
```bash
rosdoc2 build --package-path src/
```

---

## Standards et Conventions

### Structure des Documents

#### En-tête Standard
```markdown
# Titre du Document

## Vue d'Ensemble

[Description brève du contenu]

## Table des Matières

- [Section 1](#section-1)
- [Section 2](#section-2)

## Section 1

[Contenu détaillé]

## Section 2

[Contenu détaillé]

---

## Références

- [Lien 1](url)
- [Lien 2](url)

## Historique des Modifications

| Date | Auteur | Description |
|------|--------|-------------|
| 2026-03-27 | Edwin | Création initiale |
```

### Conventions de Nommage

#### Fichiers
- `installation.md` : Guides d'installation
- `api_reference.md` : Références API
- `troubleshooting.md` : Guides dépannage
- `architecture.md` : Documents architecture

#### Sections
- Utiliser des titres hiérarchiques (`# ## ###`)
- Ancres automatiques pour navigation
- Liens internes cohérents

### Style et Format

#### Code
```markdown
# Bon
```python
def function():
    return True
```

# Éviter
```
def function():
return True
```
```

#### Tableaux
```markdown
| Colonne 1 | Colonne 2 | Description |
|-----------|-----------|-------------|
| Valeur 1  | Valeur 2  | Description |
```

#### Liens et Références
```markdown
[Voir guide installation](user/installation.md)
[API dance_leader](api/dance_leader.md)
```

---

## Publication et Distribution

### GitHub Pages

**Configuration** :
```yaml
# .github/workflows/docs.yml
- name: Deploy Docs
  run: |
    mkdocs gh-deploy --force
```

**URL** : `https://username.github.io/MUTO-RS-CHOREOGRAPHY/`

### Site Web Statique

**Options** :
- **Netlify** : Déploiement automatique
- **Vercel** : Performance optimisée
- **GitLab Pages** : Intégré à GitLab

### Documentation Interne

**Outils entreprise** :
- **Confluence** : Collaboration
- **Notion** : Organisation
- **GitBook** : Publication

---

## Maintenance

### Mise à Jour Régulière

#### Checklist
- [ ] Vérifier liens cassés
- [ ] Mettre à jour captures d'écran
- [ ] Valider exemples de code
- [ ] Vérifier compatibilité versions

#### Processus
1. **Review** : Revue des changements
2. **Build** : Génération documentation
3. **Test** : Validation liens et exemples
4. **Deploy** : Publication

### Gouvernance

#### Rôles
- **Maintainers** : Responsables contenu
- **Reviewers** : Validation changements
- **Contributors** : Apports communautaires

#### Guidelines
- **Pull Requests** : Revue obligatoire
- **Issues** : Tracking améliorations
- **Templates** : Standardisation contributions

---

## Métriques et Analytics

### Outils de Mesure

#### Page Views
- Google Analytics intégré
- Métriques GitHub (si Pages)

#### Qualité
```bash
# Liens cassés
find doc/ -name "*.md" -exec markdown-link-check {} \;

# Conformité style
markdownlint doc/*.md
```

#### Couverture
- Documents par composant
- Mise à jour régulière
- Feedback utilisateurs

---

## Évolution Future

### Améliorations Prévues

- **Documentation interactive** : Exemples exécutables
- **Multilingue** : Support français/anglais
- **Recherche avancée** : Filtrage par tags
- **Versioning** : Documentation par version
- **Feedback intégré** : Commentaires utilisateurs

### Nouvelles Technologies

- **Docs as Code** : Intégration CI/CD
- **Static Analysis** : Validation automatique
- **AI Assistance** : Génération automatique
- **Video Tutorials** : Complément écrit

---

*Le dossier `doc/` est le centre de connaissance du projet. Une documentation complète et à jour est essentielle pour la réussite et la maintenabilité de MUTO-RS-CHOREOGRAPHY.*</content>
<parameter name="filePath">/home/edwin/TEKBOT-ROBOTICS-BENIN/MUTO-RS-CHOREGRAGPHY/doc/README.md