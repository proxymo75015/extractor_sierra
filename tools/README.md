# Outils / Tools

Scripts utilitaires pour l'extraction et l'analyse de fichiers Robot (.RBT).

## Scripts disponibles

### `extract_lr_simple.py`
**Extraction directe des canaux LEFT/RIGHT**

Extrait les canaux audio EVEN (LEFT) et ODD (RIGHT) directement depuis un fichier RBT, sans passer par le buffer circulaire de ScummVM.

**Utilisation** :
```bash
# 1. Générer le log d'extraction
./src/robot_decoder/build/robot_decoder ScummVM/rbt/91.RBT /tmp/output/ 90 audio 2>&1 | tee audio_extraction.log

# 2. Extraire les pistes L/R
python3 tools/extract_lr_simple.py ScummVM/rbt/91.RBT output/
```

**Sortie** :
- `91_EVEN.pcm` - Canal gauche @ 11025Hz mono
- `91_ODD.pcm` - Canal droit @ 11025Hz mono
- `91_MONO_22050Hz.pcm` - Audio entrelacé @ 22050Hz mono
- `91_LEFT_simple.wav` - WAV canal gauche
- `91_RIGHT_simple.wav` - WAV canal droit

**Caractéristiques** :
- ✅ Gère le runway DPCM (saute les 8 premiers samples)
- ✅ Classification correcte via `audioPos % 4`
- ✅ Algorithme DPCM16 conforme à ScummVM
- ✅ Pas d'interpolation (extraction brute)

---

### `extract_and_make_video.py`
**Extraction complète et génération vidéo**

Pipeline complet : extraction RBT → génération vidéo MP4 avec audio synchronisé.

**Utilisation** :
```bash
python3 tools/extract_and_make_video.py ScummVM/rbt/91.RBT output/ 90
```

**Sortie** :
- Frames vidéo (PPM)
- Audio PCM
- Vidéo finale MP4

---

### `test_audio_video_sync.py`
**Validation de la synchronisation audio/vidéo**

Vérifie que l'audio et la vidéo sont correctement synchronisés.

**Utilisation** :
```bash
python3 tools/test_audio_video_sync.py output/
```

**Vérifie** :
- Nombre de frames vidéo
- Durée audio
- Synchronisation (10 fps attendu)

---

### `make_scummvm_video.py`
**Générateur de vidéo compatible ScummVM**

Crée une vidéo MP4 à partir des frames et de l'audio extraits.

**Utilisation** :
```bash
python3 tools/make_scummvm_video.py frames_dir/ audio.pcm output.mp4
```

## Dépendances

- Python 3.8+
- `struct` (stdlib)
- `wave` (stdlib)
- FFmpeg (pour génération vidéo)

## Voir aussi

- [Documentation technique](../docs/technical/) - Détails sur les formats
- [Référence ScummVM](../docs/reference/) - Implémentation de référence
- [Guide du projet](../docs/project/) - Vue d'ensemble
