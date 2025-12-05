# 📦 Robot Position Extraction Toolkit - Index des Fichiers

Ce document liste tous les fichiers créés pour l'extraction des positions Robot de Phantasmagoria.

## 🎯 Objectif

Extraire les coordonnées X/Y exactes où ScummVM positionne les vidéos Robot lors de la lecture de Phantasmagoria, afin de reproduire le positionnement exact lors de l'extraction.

---

## 📁 Fichiers Créés

### 🔧 Scripts d'Extraction

#### `scummvm_robot_patch.diff`
- **Type:** Patch Unix diff
- **Description:** Patch pour ScummVM qui ajoute un `warning()` dans la fonction `kRobotOpen` pour logger les coordonnées Robot
- **Utilisation:**
  ```bash
  cd ~/scummvm
  patch -p1 < scummvm_robot_patch.diff
  ```
- **Sortie:** Logs type `ROBOT_DEBUG: Robot 1000 at position X=150 Y=143 ...`

#### `extract_robot_positions.sh`
- **Type:** Bash script
- **Description:** Script automatisé complet qui clone ScummVM, applique le patch, compile, lance le jeu et extrait les positions
- **Utilisation:**
  ```bash
  chmod +x extract_robot_positions.sh
  ./extract_robot_positions.sh
  ```
- **Prérequis:** Git, build-essential, libsdl2-dev

#### `parse_robot_logs.py`
- **Type:** Python 3 script
- **Description:** Parser qui analyse les logs ScummVM et extrait les coordonnées Robot
- **Utilisation:**
  ```bash
  python3 parse_robot_logs.py scummvm_logs.txt robot_positions.txt
  ```
- **Input:** Fichier log ScummVM avec lignes `ROBOT_DEBUG: ...`
- **Output:** Fichier `robot_positions.txt` au format `robotId X Y`

---

### 📚 Documentation

#### `README_ROBOT_POSITIONS.md`
- **Type:** Markdown (guide de démarrage rapide)
- **Description:** Guide étape par étape pour extraire les positions Robot
- **Contenu:**
  - Démarrage rapide (méthode recommandée)
  - 3 méthodes alternatives
  - Intégration avec l'extracteur
  - Dépannage
  - Checklist

#### `ROBOT_POSITION_EXTRACTION_GUIDE.md`
- **Type:** Markdown (guide complet)
- **Description:** Guide détaillé avec 4 méthodes d'extraction
- **Contenu:**
  - Méthode 1: ScummVM patché (automatique)
  - Méthode 2: Analyse HEAP (avancée)
  - Méthode 3: Extraction manuelle (simple)
  - Méthode 4: Valeurs par défaut (rapide)
  - Résumé des découvertes techniques

#### `ROBOT_POSITION_INVESTIGATION_SUMMARY.md`
- **Type:** Markdown (rapport technique)
- **Description:** Résumé complet de l'investigation technique
- **Contenu:**
  - Découvertes clés (architecture SCI2.1)
  - Fonction ScummVM `kRobotOpen`
  - Headers RBT décodés
  - Stockage des coordonnées (HEAP)
  - Solutions disponibles
  - Tests effectués
  - Prochaines étapes

#### `FILE_INDEX.md`
- **Type:** Markdown (ce fichier)
- **Description:** Index de tous les fichiers créés avec descriptions

---

### 🧪 Fichiers de Test

#### `test_scummvm_log.txt`
- **Type:** Fichier texte
- **Description:** Exemple de log ScummVM avec sorties `ROBOT_DEBUG`
- **Utilisation:** Test du parser sans lancer ScummVM
- **Contenu:** Logs simulés pour Robots 1000, 230, 91

#### `test_robot_positions.txt`
- **Type:** Fichier texte
- **Description:** Exemple de sortie du parser
- **Format:**
  ```
  # Robot Positions for Phantasmagoria
  # Format: robot_id X Y
  91 175 150
  230 180 160
  1000 150 143
  ```

---

### 💻 Code Exemple

#### `robot_position_loader_example.cpp`
- **Type:** C++ source code
- **Description:** Exemple complet de code C++ pour charger et utiliser les positions Robot
- **Contenu:**
  - Classe `RobotPositionManager`
  - Fonction `loadFromFile()`
  - Fonction `getPosition()` avec fallback
  - Exemple d'utilisation dans `extractRobotVideo()`
  - Exemple d'intégration dans `main()`

---

### 🔬 Code de Recherche (Archive)

#### `src/sci_robot_positions.cpp`
- **Type:** C++ source code (archivé)
- **Description:** Parser SCI pour chercher les coordonnées Robot dans les scripts décompressés
- **Résultat:** Aucune coordonnée trouvée (toutes dynamiques via propriétés d'objets)
- **Utilité:** Preuve que les coordonnées ne sont PAS hardcodées dans les scripts

---

## 📊 Résumé des Découvertes

### ✅ Confirmé
1. **Coordonnées PAS dans fichiers RBT**
   - Headers RBT: `xResolution=0`, `yResolution=0` → "use game coordinates"
   
2. **Coordonnées passées via kernel call**
   - `kRobotOpen(robotId, plane, priority, x, y, scale)`
   - `argv[3]` = X, `argv[4]` = Y
   
3. **Stockage dynamique**
   - Propriétés d'objets dans section HEAP des scripts SCI
   - Pas de constantes hardcodées (opcode SEND, pas pushi)

### 📏 Spécifications

| Paramètre | Valeur |
|-----------|--------|
| Résolution du jeu | 630×450 pixels |
| Origine | Coin supérieur gauche (0,0) |
| Format RBT | Version 5/6 (header byte = 22) |
| Moteur SCI | SCI2.1 Early |
| Kernel ID kRobot | 0x7B (123 decimal) |

---

## 🎬 Workflow Complet

### Phase 1: Extraction des Positions
```
scummvm_robot_patch.diff
        ↓
  ScummVM patché
        ↓
  Lancer Phantasmagoria
        ↓
  scummvm_logs.txt
        ↓
  parse_robot_logs.py
        ↓
  robot_positions.txt
```

### Phase 2: Utilisation dans l'Extracteur
```
robot_positions.txt
        ↓
robot_position_loader_example.cpp
        ↓
  Intégration dans src/main.cpp
        ↓
  Extraction avec positionnement exact
```

---

## 🛠️ Utilisation Rapide

### Extraction Automatique Complète
```bash
# Méthode 1: Script tout-en-un
./extract_robot_positions.sh

# Méthode 2: Étape par étape
cd ~/scummvm
patch -p1 < /workspaces/extractor_sierra/scummvm_robot_patch.diff
./configure --enable-debug --disable-all-engines --enable-engine=sci
make -j$(nproc)

./scummvm --debugflags=all --debuglevel=1 \
  /workspaces/extractor_sierra/phantasmagoria_game 2>&1 | tee robot_logs.txt

python3 /workspaces/extractor_sierra/parse_robot_logs.py \
  robot_logs.txt robot_positions.txt
```

### Test du Parser
```bash
# Tester avec log exemple
python3 parse_robot_logs.py test_scummvm_log.txt test_positions.txt

# Vérifier la sortie
cat test_positions.txt
```

---

## 📖 Documentation Recommandée

**Pour commencer:**
1. Lire `README_ROBOT_POSITIONS.md` (guide rapide)
2. Choisir une méthode d'extraction
3. Suivre les étapes

**Pour approfondir:**
1. Lire `ROBOT_POSITION_EXTRACTION_GUIDE.md` (4 méthodes)
2. Consulter `ROBOT_POSITION_INVESTIGATION_SUMMARY.md` (technique)
3. Étudier `robot_position_loader_example.cpp` (intégration)

**Pour développer:**
1. Examiner `scummvm_robot_patch.diff` (patch source)
2. Analyser `parse_robot_logs.py` (parser)
3. Adapter `robot_position_loader_example.cpp` (code)

---

## 🎯 Fichier Cible Final

**robot_positions.txt**
```
# Robot Positions for Phantasmagoria
# Extracted from ScummVM debug logs
# Format: robot_id X Y
# Game resolution: 630x450

1000 150 143
230 180 160
91 175 150
# ... autres Robots
```

Ce fichier est ensuite utilisé par l'extracteur pour positionner les vidéos Robot exactement comme dans ScummVM.

---

## ✅ Validation

Pour valider l'extraction:
1. Extraire une vidéo Robot avec les coordonnées
2. Comparer visuellement avec ScummVM
3. Vérifier que la position est identique
4. Ajuster si nécessaire dans `robot_positions.txt`

---

## 📞 Ressources Externes

- **ScummVM Source:** https://github.com/scummvm/scummvm
- **SCI Wiki:** https://wiki.scummvm.org/index.php/SCI
- **kRobotOpen Code:** `engines/sci/engine/kvideo.cpp:266-276`

---

Créé le: 2024
Projet: extractor_sierra
Objectif: Extraction parfaite des vidéos Robot de Phantasmagoria

