# Changelog

Historique des modifications du projet `extractor_sierra`.

---

## [2.2.0] - 2024-12-02 - Correction Synchronisation Audio (Positions)

### 🐛 Correction Critique

#### Synchronisation Audio - Correction Calcul de Position
- **Bug identifié** : Distorsion audio après le début (son ralenti/déformé)
- **Cause racine** : Mauvaise interprétation de `audioAbsolutePosition` 
  - Code précédent : `bufferPos = (audioAbsolutePosition * 2) + offset` → doublait la position!
  - `audioAbsolutePosition` est DÉJÀ une position dans le buffer entrelaçé final
- **Solution** : Utilisation directe de `audioAbsolutePosition` comme index de base
  - Nouveau calcul : `bufferPos = audioAbsolutePosition + (sample * 2)`
  - Le `* 2` s'applique seulement à l'offset des samples, pas à la position de départ
- **Résultat** : Audio correctement synchronisé sans distorsion
- **Impact** : Synchronisation parfaite sur toute la durée de la vidéo

#### Détails Techniques
- `audioAbsolutePosition` pour EVEN : 39844, 44254, 48664... (positions paires)
- `audioAbsolutePosition` pour ODD  : 42049, 46459, 50869... (positions impaires)  
- Ces valeurs incluent déjà l'offset des primers (40946 samples)
- L'interpolation entre canaux EVEN/ODD reste active pour assurer un flux audio continu

### 📚 Documentation

#### Nouvelles Documentations
- **TECHNICAL.md** : Documentation technique complète
  - Architecture audio DPCM16 entrelaçé
  - Explication détaillée de `audioAbsolutePosition`
  - Processus d'extraction complet avec diagrammes
  - Historique des corrections avec analyses
  - Références ScummVM et format LZS
  
- **QUICKSTART.md** : Guide de démarrage rapide
  - Installation et vérification
  - Exemples d'utilisation
  - Tests de synchronisation audio
  - Dépannage courant
  - Commandes pratiques

#### Mises à Jour
- **README.md** : Section audio améliorée avec détails sur l'interpolation
- **Code source** : Commentaires mis à jour pour refléter le calcul correct
- Suppression des commentaires de debug obsolètes

### 🧹 Nettoyage du Projet

- Suppression des fichiers de debug temporaires (`debug_audio_positions.cpp`)
- Nettoyage des logs temporaires dans `/tmp/`
- Suppression du répertoire `build_windows/` obsolète
- Commentaires de code mis à jour (suppression des références aux bugs corrigés)
- Build final propre pour Linux et Windows

### 📝 Corrections Précédentes (Annulées)
Une tentative précédente de désactivation de l'interpolation a été annulée car elle n'était pas la cause du problème. L'interpolation est nécessaire pour créer des transitions douces entre les canaux EVEN et ODD.

---

## [2.1.0] - 2024-12-01 - Batch Processing + Corrections Windows

### 🎯 Nouveautés

#### Mode Batch Automatique
- **Scan automatique** : Détection de tous les fichiers .RBT dans le dossier RBT/
- **Structure organisée** : Chaque RBT génère son propre sous-dossier `output/<rbt_name>/`
- **Traitement en série** : Tous les fichiers traités en une seule commande
- **Statistiques complètes** : Compteur de succès/échecs, progression affichée
- **Export frames PNG** : Chaque frame est sauvegardée dans `<rbt_name>_frames/frame_XXXX.png`

#### Corrections Windows
- **Commandes système corrigées** :
  - `tail -5` → `2>nul` sous Windows
  - `rm -rf` → `rd /s /q` sous Windows
  - Vérification FFmpeg adaptée (>nul vs >/dev/null)
- **Vérification FFmpeg obligatoire** : Message d'erreur explicite si FFmpeg absent
- **Encodage fichiers .bat** : Suppression des accents pour compatibilité CP1252
- **DLLs incluses** : Package Windows contient libstdc++-6.dll, libgcc_s_seh-1.dll, libwinpthread-1.dll

### 🐛 Corrections

#### Synchronisation Audio/Vidéo
- **Bug critique corrigé** : Décalage audio sur fichiers avec frames skip (temps morts)
- **Cause** : Les frames skip (videoSize==0) généraient de l'audio sans vidéo
- **Solution** : L'audio ne génère plus de samples pour les frames skip
- **Impact** : Synchronisation parfaite entre audio et vidéo maintenue

#### Couche Luminance
- **Bug visuel corrigé** : La piste luminance affichait des artefacts verts dans VLC
- **Format PNG** : Conversion de grayscale (1 canal) → RGB (3 canaux identiques)
- **Compatibilité codec** : Les codecs H.264/H.265 gèrent mieux les RGB uniformes

#### Documentation
- **README_WINDOWS.txt** : Instructions détaillées d'installation FFmpeg
- **TEST_WINDOWS.md** : Guide de débogage pour problèmes Windows
- **Messages d'erreur** : Textes en anglais et plus explicites

---

## [2.0.0] - 2024 - Export MKV Multi-couches

### 🎯 Nouveautés Majeures

#### Export MKV Multi-couches
- **Nouvel outil** : `export_robot_mkv` - Export MKV avec 4 pistes vidéo + audio
- **4 pistes vidéo séparées** :
  - Track 0 : BASE RGB (pixels fixes 0-235)
  - Track 1 : REMAP RGB (pixels recoloriables 236-254)
  - Track 2 : ALPHA Grayscale (masque transparence 255)
  - Track 3 : LUMINANCE Y (niveaux de gris BT.601)
- **Support multi-codecs** : H.264, H.265, VP9, FFV1 (lossless)
- **Audio PCM 48 kHz** : Resampling depuis 22050 Hz natif
- **Métadonnées Matroska** : Titres de piste pour identification
- **Fichier metadata.txt** : Statistiques complètes (pixels BASE/REMAP/SKIP par frame)

#### Gestion de fichiers
- **Noms personnalisés** : Fichiers de sortie préfixés avec le nom du RBT source
  - Format : `{rbt}_video.mkv`, `{rbt}_audio.wav`, `{rbt}_metadata.txt`
  - Permet le traitement en batch sans conflit de noms

#### Documentation complète
- **README.md** : Guide complet d'utilisation (français)
- **src/README.md** : Documentation du code source
- **docs/MKV_FORMAT.md** : Spécifications techniques du format MKV multi-pistes
- **PROJECT_STATUS.md** : État détaillé du projet avec historique des bugs

### 🐛 Corrections Critiques

#### Extraction de frames (Bug majeur)
- **Symptôme** : Crash après frame ~27 (fichiers >65KB par chunk)
- **Cause** : Tailles de chunks lues en `uint16` au lieu de `uint32`
- **Correction** : Lecture correcte des headers (10 bytes : 2×uint32 + 1×uint16)
- **Impact** : 100% des frames extraites maintenant (validé sur 8 fichiers RBT)

#### Détection de transparence
- **Symptôme** : Pixels transparents (index 255) non détectés (0% SKIP reporté)
- **Cause** : Filtre `if (pixelIdx != 255)` avant écriture buffer
- **Correction** : Écriture inconditionnelle de tous les pixels
- **Impact** : Détection correcte ~82-85% pixels SKIP

#### Initialisation buffer
- **Symptôme** : Fond noir au lieu de transparent dans les exports
- **Cause** : Buffer pixel initialisé à 0 (noir opaque)
- **Correction** : Initialisation à 255 (transparent par défaut)
- **Impact** : Transparence correcte dans toutes les pistes

#### Noms de fichiers
- **Symptôme** : Écrasement des fichiers en traitement batch
- **Cause** : Noms génériques (video.mkv, audio.wav)
- **Correction** : Extraction du nom RBT et préfixage des sorties
- **Impact** : Noms uniques (212_video.mkv, 212_audio.wav, 212_metadata.txt)

### 🔬 Investigations Techniques

#### Pixels REMAP (236-254) - Mystère résolu
- **Observation** : 6-7% de bytes 236-254 dans fichiers bruts, mais 0% après décompression
- **Analyse** : Création d'outil `analyze_byte_locations.cpp` pour localisation
- **Résultat** : 99.8% de ces bytes sont dans les chunks **compressés** LZS
- **Conclusion** : Ce sont des **codes de contrôle LZS**, pas des indices de pixels
- **Validation** : Vérifié sur 8 fichiers RBT différents (0% REMAP après décompression)
- **Impact** : Comportement normal, ces vidéos n'utilisent pas la recoloration

### ✨ Améliorations Techniques

#### Conversion luminance
- **Formule BT.601** : `Y = 0.299R + 0.587G + 0.114B` (standard ITU-R)
- Génération automatique de la piste Track 3 pour prévisualisation

#### Statistiques pixels avancées
- Scan complet de toutes les frames (au lieu de frame 0 uniquement)
- Rapport du premier frame contenant REMAP/SKIP pixels
- Totaux cumulés par type de pixel

#### Classification pixels
```cpp
// BASE (0-235) : Couleurs fixes opaques
if (pixelIdx <= 235) {
    baseRGB[i] = palette[pixelIdx];
}

// REMAP (236-254) : Zones recoloriables
else if (pixelIdx <= 254) {
    remapRGB[i] = palette[pixelIdx];
}

// SKIP (255) : Transparent
else {
    alphaMask[i] = 0; // Transparent
}
```

### 📊 Tests de Validation

#### Fichiers testés (100% succès)
- ✅ 91.RBT : 90 frames, 9.0s
- ✅ 170.RBT : 113 frames, 11.3s
- ✅ 212.RBT : 33 frames, 3.3s
- ✅ 300.RBT, 340.RBT, 380.RBT, 470.RBT, 530.RBT

#### Métriques
- **Extraction frames** : 100% succès (vs ~70% avant correction)
- **Taille MKV moyenne** : ~1.0-1.1 Mbps (H.264 CRF 18)
- **Distribution pixels** : BASE 15-18%, REMAP 0%, SKIP 82-85%

### 🏗️ Build & Environnements

#### Configuration
- CMake 3.10+ minimum
- Support C++11 obligatoire
- Dépendance FFmpeg 4.0+

#### Environnements validés
- ✅ Dev Container (Ubuntu 24.04.2 LTS, GCC 13.2)
- ✅ Ubuntu 22.04 (GCC 11.4)
- ✅ Debian 12 (GCC 12.2)
- ⚠️ Windows 10 (MSVC 2022, chemins à adapter)

### ❌ Supprimé

- Références obsolètes `robot_decoder` dans CMakeLists.txt
- Fichiers temporaires de test et debug (~50 fichiers)

---

## [1.0.0] - 2025-11-20 - Version Stable

### ✨ Fonctionnalités principales

- ✅ Extraction vidéo complète (LZS → PPM/PNG)
- ✅ Extraction audio complète (DPCM16 → PCM 22050 Hz)
- ✅ Support Robot v5 (320×240) et v6 (640×480)
- ✅ Séparation canaux audio LEFT/RIGHT
- ✅ Génération vidéo MP4 via FFmpeg
- ✅ Buffer circulaire audio (basé sur ScummVM)
- ✅ Interpolation multi-pass pour qualité audio

### 🔧 Implémentation

#### Audio
- **DPCM16 Decoder** avec clamping (au lieu de wrapping ScummVM)
- **RobotAudioStream** adapté de ScummVM pour extraction batch
- **Primers** : Support complet (19922 + 21024 samples)
- **Runway** : 8 bytes automatiquement gérés par positions
- **Classification** : `audioPos % 4` pour EVEN/ODD
- **Interpolation** : Multi-pass pour combler les gaps

#### Vidéo
- **LZS Decoder** pour décompression vidéo
- **Palette** : Support HunkPalette 256 couleurs
- **Frames** : Export PPM P6 (binary RGB)

### 📚 Documentation

#### Structure réorganisée
- `docs/reference/` - Documentation ScummVM (référence)
- `docs/project/` - Documentation du projet
- `docs/technical/` - Notes techniques

#### Nouveaux documents
- **SCUMMVM_ROBOT_FORMAT.md** - Format Robot v5/v6 complet
- **SCUMMVM_AUDIO_IMPLEMENTATION.md** - Implémentation ScummVM détaillée
- **PROJECT_OVERVIEW.md** - Vue d'ensemble du projet
- **STRUCTURE.md** - Guide de la documentation

#### Mis à jour
- **AUDIO_ENCODING.md** - Comparaison ScummVM vs notre projet
- **AUDIO_EXTRACTION_LR.md** - Extraction L/R avec runway clarifié
- **README.md** - Restructuré et complété

### 🧹 Nettoyage

#### Fichiers supprimés
- Scripts Python de debug/test obsolètes (8 fichiers)
- Binaires compilés temporaires (extract_positions, show_positions)
- Fichiers de log (audio_extraction.log, extraction.log, etc.)
- Répertoires temporaires (output_91, temp_extract, test_dump, audio)
- Patches temporaires (log_audio_positions.patch)
- Code source de debug (extract_with_positions.cpp, show_audio_positions.cpp)

#### Scripts conservés
- `extract_lr_simple.py` - Extraction L/R autonome
- `extract_and_make_video.py` - Workflow complet vidéo
- `test_audio_video_sync.py` - Validation A/V

### 🐛 Corrections

- ✅ DPCM overflow : Clamping au lieu de wrapping (meilleure qualité)
- ✅ Clarification runway : Documentation corrigée (8 bytes, géré par positions)
- ✅ Classification canaux : Documentation `% 4` au lieu de `% 2` (erreur ScummVM)
- ✅ Premiers zero samples : Activation des primers (résolu 1.8s de silence)

### 📊 Performance

**Test** : 91.RBT (90 frames, 9 secondes)

```
Extraction C++    : ~0.5s
  - Vidéo         : ~0.3s (90 frames PPM)
  - Audio         : ~0.2s (198,450 samples + interpolation)
FFmpeg (MP4)      : ~2s
```

**Qualité audio** :
- Zeros : 0.04% (98/238,302 samples)
- Discontinuités >5000 : 36 (vs 111,614 avant corrections)
- Amélioration : ~3100× réduction discontinuités

### 🎯 Différences avec ScummVM

| Aspect | ScummVM | Notre projet |
|--------|---------|--------------|
| Usage | Playback temps-réel | Extraction batch |
| Buffer audio | Circulaire (streaming) | Linéaire (offline) |
| DPCM overflow | Wrapping (x86 compat) | Clamping (qualité) ✅ |
| Interpolation | Par canal | Multi-pass ✅ |
| Output | Playback direct | Fichiers (PCM/PPM) |

### 🔗 Références

- ScummVM : https://github.com/scummvm/scummvm
- Robot Decoder : `engines/sci/video/robot_decoder.cpp`
- DPCM Decoder : `engines/sci/sound/decoders/sol.cpp`

---

## Notes de version

**Version 1.0.0** : Première version stable avec :
- Extraction complète fonctionnelle
- Documentation exhaustive
- Code nettoyé et organisé
- Qualité audio validée
