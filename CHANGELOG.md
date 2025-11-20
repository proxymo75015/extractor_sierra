# Changelog

## [1.0.0] - 2025-11-20

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
