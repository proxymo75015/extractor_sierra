# 📦 Release Notes - Version 3.1.0

**Date de sortie :** 7 décembre 2024  
**Nom de code :** "Multi-RESMAP Scanner"

---

## 🎯 Nouveautés

### 📋 Catalogue automatique des ressources RESSCI

Le programme scanne maintenant **tous les fichiers RESMAP.00X** disponibles (pas seulement RESMAP.001) et génère automatiquement un fichier `output/resources_list.txt` contenant le catalogue complet de toutes les ressources Sierra SCI indexées.

**Fichier généré :** `output/resources_list.txt`

**Contenu :**
- Total des ressources indexées (tous RESMAP combinés)
- Résumé par type de ressource :
  - Scripts (0x80)
  - Bitmaps (0x8a)
  - Audio (0x8d)
  - Palettes (0x8b)
  - Fonts (0x84)
  - Views (0x88)
  - etc.
- Liste détaillée pour chaque ressource :
  - Numéro de ressource
  - Offset dans le volume (hexadécimal et décimal)
  - Numéro du volume RESSCI

**Exemple :**
```
=================================================================
LISTE DES RESSOURCES SIERRA SCI - RESMAP/RESSCI
=================================================================
Total ressources indexées: 235
Volumes RESSCI chargés: 2
=================================================================

RÉSUMÉ PAR TYPE DE RESSOURCE:
-----------------------------------------------------------------
Script (0x80): 8 ressource(s)
Bitmap (0x8a): 9 ressource(s)
Audio (0x8d): 12 ressource(s)
...

-----------------------------------------------------------------
Script (0x80)
-----------------------------------------------------------------
  1889 -> Offset: 1280 (0x500), Volume: 1
  10692 -> Offset: 2327044 (0x238204), Volume: 1
  ...
```

### 🔍 Utilité du fichier resources_list.txt

1. **Debugging** : Vérifier quelles ressources sont présentes sur chaque CD
2. **Analyse du contenu** : Comprendre la répartition des ressources entre volumes
3. **Extraction manuelle** : Localiser précisément une ressource spécifique
4. **Documentation** : Cataloguer le contenu complet du jeu
5. **Recherche** : Identifier rapidement les offsets et volumes pour analyse

---

## ⚙️ Améliorations techniques

### Scan multi-RESMAP
- ✅ Chargement automatique de **RESMAP.001 à RESMAP.009**
- ✅ Fusion de toutes les ressources indexées dans un index global
- ✅ Mapping ressource → volume pour extraction optimisée
- ✅ Support CD multi-disques (Phantasmagoria CD1-CD7)

### Export RESSCI complet
- ✅ Nouvelle méthode `RESSCIParser::exportResourcesList()`
- ✅ Format texte lisible avec sections par type
- ✅ Offsets en hexadécimal et décimal
- ✅ Indication du volume RESSCI pour chaque ressource

### Intégration dans le flux d'extraction
- Le scan RESMAP/RESSCI est effectué **avant** le traitement des fichiers RBT
- Le fichier `resources_list.txt` est créé dans `output/` dès le premier scan
- Pas d'impact sur les performances (scan unique au démarrage)

---

## 📊 Statistiques

### Exemple Phantasmagoria (2 volumes testés)

**RESMAP chargés :**
- RESMAP.001 : 11 524 octets (1920 entrées, 151 ressources)
- RESMAP.002 : 12 064 octets (2010 entrées, 84 nouvelles ressources)

**RESSCI chargés :**
- RESSCI.001 : 69.9 MB
- RESSCI.002 : 74.5 MB

**Ressources indexées :** 235 (combiné)

**Coordonnées Robot extraites :** 78 305 positions

**Fichier resources_list.txt :** 16 KB (361 lignes)

---

## 🔄 Compatibilité

- ✅ **Linux** : Compilation GCC/Clang validée
- ✅ **Windows** : MinGW-w64 cross-compilation avec linking statique
- ✅ **Format RESMAP** : Support 6 octets (SCI1.1/variantes) et 9 octets (SCI32)
- ✅ **Multi-CD** : Phantasmagoria CD1-CD7 supporté

---

## 📦 Package Windows

**Fichier :** `extractor_sierra_windows_v3.1.0.zip` (2.3 MB)

**Contenu :**
- `export_robot_mkv.exe` (3.5 MB) - Programme principal
- `robot_extractor.exe` (3.4 MB) - Extracteur legacy
- `extract_coordinates.exe` (2.9 MB) - Extracteur coordonnées
- `README_WINDOWS.txt` - Documentation Windows
- `run_extraction.bat` - Script batch automatique
- `docs/` - Documentation complète
- `LICENSE` - Licence MIT

**Nouveautés package :**
- ✅ Génération automatique de `output/resources_list.txt`
- ✅ Scan multi-RESMAP (RESMAP.001-009)
- ✅ Support multi-CD intégré

---

## 🛠️ Changements internes

### Fichiers modifiés

**src/core/ressci_parser.h**
- Ajout méthode `exportResourcesList(const std::string& outputPath)`
- Ajout getter `getResourceIndex()` pour accès index ressources

**src/core/ressci_parser.cpp**
- Implémentation `exportResourcesList()` avec formatage texte
- Support export détaillé par type de ressource
- Format hexadécimal + décimal pour offsets

**src/export_robot_mkv.cpp**
- Modification `loadRobotPositionsFromRESSCI()` pour accepter paramètre `outputDir`
- Scan automatique RESMAP.001-009 (au lieu de seulement .001)
- Appel `parser.exportResourcesList(outputDir + "/resources_list.txt")`
- Génération fichier **avant** extraction coordonnées Robot

**README.md**
- Documentation de `resources_list.txt`
- Exemples de contenu et d'utilisation
- Section "Fichiers générés" mise à jour

---

## 🐛 Corrections

- ✅ Scan RESMAP limité à .001 → Scan complet .001-.009
- ✅ Pas de catalogue ressources → Génération automatique `resources_list.txt`
- ✅ Pas d'info volumes → Mapping ressource → volume RESSCI

---

## 📚 Documentation

### Fichiers mis à jour
- `README.md` - Ajout section resources_list.txt
- `RELEASE_NOTES_v3.1.0.md` - Ce fichier
- `docs/README.md` - Exemples d'utilisation mis à jour

### Commande d'extraction

```bash
# Linux
./build/export_robot_mkv RBT/ Resource/ output/

# Windows
run_extraction.bat
```

**Fichiers générés :**
```
output/
├── resources_list.txt         # ← NOUVEAU : Catalogue ressources RESSCI
├── 1000/
│   ├── 1000_video.mkv
│   ├── 1000_video_composite.mov
│   ├── 1000_audio.wav
│   └── 1000_frames/
└── ...
```

---

## 🔗 Liens

- **Repository :** https://github.com/proxymo75015/robot_extract
- **Documentation :** `docs/README.md`
- **License :** MIT

---

## 🙏 Remerciements

- **ScummVM Team** - Format RESSCI/RESMAP et Robot
- **Sierra On-Line** - Phantasmagoria et moteur SCI32

---

**Version précédente :** [v3.0.0](RELEASE_NOTES_v3.0.0.md)
