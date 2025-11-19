# Documentation Technique - Robot Decoder

Documentation complète du projet d'extraction Robot (format Sierra .RBT).

## 📚 Documents

### 1. [RBT_Decoder_Design.md](RBT_Decoder_Design.md)
**Architecture complète du décodeur Robot**
- Format .RBT (structure des fichiers)
- Décodage vidéo (RLE, palettes)
- Décodage audio (DPCM16)
- Flux de traitement complet

### 2. [AUDIO_ENCODING.md](AUDIO_ENCODING.md)
**Codage audio DPCM16 (Sierra SOL)**
- Compression/décompression DPCM
- Tables de différences
- Gestion des primers
- Algorithmes de décodage

### 3. [AUDIO_VIDEO_SYNC.md](AUDIO_VIDEO_SYNC.md)
**Synchronisation audio/vidéo**
- Découverte: packets DPCM = 2205 samples = 100ms
- Synchronisation intrinsèque au format
- Rôle de l'interpolation linéaire
- Architecture du flux audio

### 4. [CIRCULAR_BUFFER_IMPLEMENTATION.md](CIRCULAR_BUFFER_IMPLEMENTATION.md)
**Buffer circulaire audio**
- Gestion des positions absolues
- Entrelacement EVEN/ODD (stride de 4)
- Interpolation des canaux stéréo
- Lecture continue sans gaps

## 🗺️ Architecture Globale

```
Fichier .RBT
    ↓
┌─────────────────┬──────────────────┐
│  Vidéo (RLE)    │  Audio (DPCM16)  │
└─────────────────┴──────────────────┘
         ↓                  ↓
   Décodage RLE      Décompression DPCM
         ↓                  ↓
   Frames vidéo      Buffer circulaire
    (640×480)        (entrelacement)
         ↓                  ↓
   10 fps (100ms)    2205 samples/frame
         ↓                  ↓
         └──────────┬───────┘
                    ↓
         Synchronisation parfaite
              (100ms/frame)
```

## 🎯 Guide de Lecture

**Pour comprendre le format:**
1. `RBT_Decoder_Design.md` - Vue d'ensemble
2. `AUDIO_VIDEO_SYNC.md` - Synchronisation

**Pour implémenter:**
1. `AUDIO_ENCODING.md` - DPCM16
2. `CIRCULAR_BUFFER_IMPLEMENTATION.md` - Buffer audio

## 📂 Organisation du Code

```
extractor_sierra/
├── src/robot_decoder/     # Code source
├── include/               # Headers
├── docs/                  # Cette documentation
├── build/                 # Binaires compilés
└── test_audio_video_sync.py  # Tests
```

## ✅ Points Clés

- **Synchronisation**: Chaque packet DPCM = exactement 100ms
- **Pas d'élongation**: Le format est synchronisé par design
- **Interpolation**: Reconstruction stéréo, pas time-stretching
- **Buffer circulaire**: Gestion élégante du flux continu

---

**Dernière mise à jour**: Novembre 2025
