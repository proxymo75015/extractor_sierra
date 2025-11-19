# 🎮 Robot Decoder - Sierra .RBT Extractor

Décodeur et extracteur pour les vidéos Robot du moteur Sierra SCI (années 90).

## 📋 Description

Extraction complète des vidéos au format .RBT (Robot) utilisées dans les jeux Sierra:
- **Vidéo**: Décompression RLE, palettes 256 couleurs, 640×480
- **Audio**: Décompression DPCM16, 22050 Hz mono, synchronisation parfaite
- **Export**: Vidéo MP4 (H.264 + AAC) avec FFmpeg

## 🚀 Quick Start

### Compilation

```bash
mkdir build && cd build
cmake ..
make
```

### Extraction d'une vidéo

```bash
# Extraction complète avec audio
./build/robot_decoder ScummVM/rbt/91.RBT output_91/

# Génération de la vidéo MP4
python3 extract_and_make_video.py output_91/
```

### Test de synchronisation

```bash
python3 test_audio_video_sync.py
```

## 📁 Structure du Projet

```
extractor_sierra/
├── src/robot_decoder/      # Code source C++
│   ├── robot_decoder.cpp   # Décodeur principal
│   ├── robot_audio_stream.cpp  # Buffer circulaire audio
│   ├── dpcm.cpp            # Décompression DPCM16
│   └── rle.cpp             # Décompression RLE vidéo
├── include/                # Headers
├── docs/                   # Documentation technique
├── build/                  # Binaires compilés
│   └── robot_decoder       # Exécutable principal
├── ScummVM/rbt/            # Fichiers .RBT de test
└── tools/                  # Scripts utilitaires
```

## 🎯 Fonctionnalités

- ✅ Décodage vidéo RLE (640×480, 256 couleurs)
- ✅ Décodage audio DPCM16 (22050 Hz)
- ✅ Buffer circulaire avec entrelacement stéréo
- ✅ Synchronisation audio/vidéo parfaite (100ms/frame)
- ✅ Export MP4 avec FFmpeg
- ✅ Support multi-fichiers (.RBT)

## 🔬 Technique

### Format Audio
- **Codec**: DPCM16 (Differential PCM 16-bit)
- **Canaux**: EVEN/ODD entrelacés → mono 22050 Hz
- **Packets**: 2205 samples/packet = 100ms exactement
- **Synchronisation**: Intrinsèque au format (pas d'élongation)

### Format Vidéo
- **Compression**: RLE (Run-Length Encoding)
- **Résolution**: 640×480 pixels
- **Palette**: 256 couleurs (RGB)
- **Framerate**: 10 fps

## 📚 Documentation

Voir [docs/INDEX.md](docs/INDEX.md) pour la documentation technique complète:
- Architecture du décodeur
- Format DPCM16
- Buffer circulaire
- Synchronisation A/V

## 🧪 Tests

```bash
# Test de synchronisation A/V
python3 test_audio_video_sync.py

# Validation complète
./validate.sh
```

## 📄 Licence

Voir [LICENSE](LICENSE)

## 🙏 Références

Basé sur l'implémentation ScummVM du décodeur Robot:
- [ScummVM - SCI Engine](https://github.com/scummvm/scummvm)
- Documentation du format Sierra Robot

---

**Dernière mise à jour**: Novembre 2025
