# Changelog

## Version 3.1.1 (2025-12-09)

### 🐛 Corrections critiques

- **Extraction coordonnées Robot** : Correction de l'ordre des paramètres kRobotOpen
  - Bug: Utilisait params[1] et params[2] (plane et priority) au lieu de params[3] et params[4] (x et y)
  - Fix: Ordre correct selon ScummVM : `kRobotOpen(robotId, plane, priority, x, y, scale)`
  - Impact: Robot 162 passe de (179, 182) à (160, 8) - position correcte
  - Impact: Robot 170 passe de (169, 179) à (250, 331)
  - Impact: Robot 260 reste à (309, 130)
  
- **Canvas positioning** : Application correcte des coordonnées ScummVM
  - Formule haute résolution : `screenX = celPosition.x + _position.x`
  - Formule haute résolution : `screenY = celPosition.y + _position.y`
  - Les coordonnées `_canvasX/_canvasY` sont maintenant correctement ajoutées aux `celX/celY`

## Version 3.1.0 (2025-12-08)

### ✨ Nouveautés

- **Extraction automatique des coordonnées** depuis scripts SCI32
  - Parse opcode 0x76 (CALLK Robot) dans bytecode SCI
  - Filtre kernel IDs {57, 67, 74, 84}
  - Génère `robot_positions_final.txt` avec tous les robots trouvés
  - 157 robots uniques extraits depuis RESSCI.001/002

- **Fichier de coordonnées unique** : `robot_positions_default.txt`
  - Format simplifié: `robotId X Y`
  - Coordonnées extraites pour robots 170, 162, 260
  - Valeurs par défaut pour robots 90, 91, 161

### 🗑️ Nettoyage

**Fichiers supprimés :**
- `src/extract_coordinates.cpp` (obsolète)
- `src/analyze_scripts.cpp` (obsolète)
- `src/sci_robot_positions.cpp` (obsolète)
- `src/sci_script_analyzer.cpp` (obsolète)
- `scan_opcodes.cpp` (test temporaire)
- `test_*.cpp` (tous les fichiers de test)
- `*.log` (fichiers de log)
- `robot_coordinates.json` (format obsolète)

**CMakeLists.txt :**
- Suppression des targets `extract_coordinates` et `analyze_scripts`
- Suppression des sections BUILD_TESTS (commentées)
- Nettoyage des commentaires obsolètes

**README.md :**
- Mise à jour version 3.1.0
- Documentation extraction coordonnées
- Suppression références Python
- Correction exemples et formats
- Ajout section "Extraction coordonnées"

### 🔧 Améliorations

- **ressci_parser.cpp** : Filtre strict sur robots connus (1-9999)
- **Validation coordonnées** : x/y entre -100 et 740/580
- **Format de sortie** : Simplifié, sans doublons

### 📊 Résultats

- **3 robots extraits** : 170, 162, 260 (coordonnées réelles)
- **3 robots manquants** : 90, 91, 161 (scripts sur autres CDs)
- **Format final** : 1 ligne par robot, sans doublons

## Version 3.0.0 (2025-12-07)

### Fonctionnalités initiales

- Extraction RBT vers MKV 4 pistes
- Export MOV ProRes 4444 RGBA
- Frames PNG avec transparence
- Audio DPCM vers WAV
- Tight crop automatique
