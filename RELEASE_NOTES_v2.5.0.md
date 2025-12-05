# Release Notes v2.5.0 - ScummVM Canvas Auto-Detect

**Date** : 2024-12-04

## 🎯 Nouveautés majeures

### 1. Auto-détection du canvas
- **Résolutions standard** détectées automatiquement : 640×480, 640×400, 320×240, 320×200
- Choix intelligent de la plus petite résolution englobant le contenu
- Exemple : Contenu 390×461 → Canvas 640×480 (VGA)

### 2. Option `--canvas` pour forcer la résolution
```bash
./export_robot_mkv h264 --canvas 640x480
./export_robot_mkv vp9 --canvas 800x600
```

### 3. Positions ScummVM préservées
- **Bug corrigé** : Les frames n'étaient pas centrées correctement
- **Solution** : Positions absolues `celX`, `celY` du format RBT respectées
- **Résultat** : Compatibilité totale avec coordonnées ScummVM

## 🔧 Corrections

- ✅ Suppression du recentrage artificiel des frames
- ✅ Padding transparent à droite/bas uniquement (pas de centrage)
- ✅ Messages console améliorés (Content Resolution, Canvas auto-détecté)

## 📦 Fichiers

- **Linux** : `build/export_robot_mkv` (923 KB)
- **Windows** : `extractor_sierra_windows.zip` (8.2 MB)
  - MD5: `1c6e0d06da2cfd589644164294b81557`

## 🧪 Tests validés

```bash
# Test auto-détection
./export_robot_mkv h264
# → Canvas: 640×480, Position: (248, 136)

# Test canvas forcé
./export_robot_mkv h264 --canvas 800x600
# → Canvas: 800×600, Position: (248, 136) (identique ✓)
```

## 📖 Documentation

- README.md : Mis à jour avec option `--canvas`
- CHANGELOG.md : Version 2.5.0 ajoutée
- README_WINDOWS.txt : Instructions complètes

## ⚙️ Utilisation

```bash
# Auto-détection (recommandé)
./export_robot_mkv h264

# Canvas personnalisé
./export_robot_mkv h264 --canvas 640x480
./export_robot_mkv h264 --canvas 800x600

# Autres codecs
./export_robot_mkv vp9 --canvas 640x480
./export_robot_mkv ffv1
```

## 🎮 Compatibilité ScummVM

- ✅ Positions absolues `celX`, `celY` préservées
- ✅ Canvas normalisé pour FFmpeg (dimensions fixes)
- ✅ Transparence alpha native (ProRes 4444)
- ✅ Compatible réimport dans ScummVM
