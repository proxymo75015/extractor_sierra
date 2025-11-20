# 🎮 extractor_sierra - Robot RBT Extractor

> Extracteur et convertisseur de fichiers vidéo Robot (.RBT) de Sierra SCI  
> Basé sur l'implémentation de référence ScummVM

Convertit les vidéos des jeux Sierra (Phantasmagoria, Gabriel Knight 2, etc.) en formats modernes (MP4, WAV).

---

## 📋 Fonctionnalités

✅ **Vidéo** : Extraction frames (PPM) + décompression LZS  
✅ **Audio** : DPCM16 → PCM 16-bit @ 22050 Hz mono  
✅ **Canaux L/R** : Séparation EVEN/ODD (11025 Hz chacun)  
✅ **Export MP4** : H.264 + AAC via FFmpeg  
✅ **Formats** : Robot v5 (320×240) et v6 (640×480)  
✅ **Qualité** : Clamping DPCM + interpolation multi-pass

---

## 🚀 Installation rapide

### Prérequis

- **C++11** : g++ ou clang
- **CMake** : 3.10+
- **Python** : 3.8+
- **FFmpeg** : (optionnel, pour MP4)

### Compilation

```bash
# Compiler le décodeur C++
mkdir -p build
cd build
cmake ../src
make -j$(nproc)

# Vérifier
./robot_decoder --help
```

---

## 💡 Utilisation

### 1. Extraction complète (vidéo + audio)

```bash
./build/robot_decoder \
    ScummVM/rbt/91.RBT \  # Fichier RBT source
    output/ \              # Répertoire de sortie
    90 \                   # Nombre de frames
    audio                  # Mode: video, audio, ou all
```

**Résultat** :
```
output/
├── frames/
│   ├── frame_0000.ppm
│   ├── frame_0001.ppm
│   └── ...
└── audio.raw.pcm (mono 22050Hz, 16-bit)
```

### 2. Génération vidéo MP4

```bash
# Méthode 1: Script Python (recommandé)
python3 tools/extract_and_make_video.py \
    ScummVM/rbt/91.RBT \
    output/

# Méthode 2: FFmpeg direct
cd output/
ffmpeg -framerate 10 -pattern_type glob -i 'frames/*.ppm' \
       -f s16le -ar 22050 -ac 2 -i audio.raw.pcm \
       -c:v libx264 -pix_fmt yuv420p -c:a aac \
       -shortest output.mp4
```

### 3. Extraction canaux LEFT/RIGHT

```bash
# 1. Générer le log d'extraction
./build/robot_decoder \
    ScummVM/rbt/91.RBT output/ 90 audio 2>&1 | tee audio_extraction.log

# 2. Extraire les canaux séparés
python3 tools/extract_lr_simple.py \
    ScummVM/rbt/91.RBT \
    output_lr/
```

**Résultat** :
```
output_lr/
├── 91_LEFT_simple.wav   (EVEN channel @ 11025Hz)
├── 91_RIGHT_simple.wav  (ODD channel @ 11025Hz)
└── 91_MONO_22050Hz.pcm  (entrelacé)
```

---

## 📁 Structure du projet

```
extractor_sierra/
├── src/                       # 🔧 Code source C++
│   ├── main.cpp              #    Point d'entrée
│   ├── core/                 #    Cœur du décodeur
│   │   ├── rbt_parser.*      #      Parser format RBT
│   │   └── robot_audio_stream.*#    Buffer audio
│   ├── formats/              #    Codecs spécifiques
│   │   ├── dpcm.*            #      Décodeur DPCM16
│   │   ├── lzs.*             #      Compression LZS
│   │   └── decompressor_lzs.*#      Décompression LZS
│   └── utils/                #    Utilitaires
│       ├── sci_util.*        #      Utils SCI/ScummVM
│       └── memory_stream.h   #      Stream mémoire
│
├── build/                     # 🏗️ Binaires compilés
│   └── robot_decoder         #    Exécutable principal
│
├── tools/                     # 🐍 Scripts Python
│   ├── extract_lr_simple.py  #    Extraction L/R directe
│   ├── extract_and_make_video.py
│   └── make_scummvm_video.py
│
├── docs/                      # 📖 Documentation
│   ├── reference/            #    Référence ScummVM
│   ├── project/              #    Notre implémentation
│   └── technical/            #    Détails techniques
│
└── ScummVM/                   # 📦 Code référence ScummVM
    ├── robot.cpp             #    RobotAudioStream
    └── robot.h
```

---

## 🔬 Détails techniques

### Format Audio Robot

**Structure** :
```
Robot Audio = EVEN channel (11025Hz) + ODD channel (11025Hz)
           → Entrelacés → Mono 22050Hz
```

**Classification des packets** :
```cpp
bufferIndex = (audioPos % 4) ? 1 : 0;
// audioPos % 4 == 0 → EVEN (LEFT)
// audioPos % 4 != 0 → ODD (RIGHT)
```

**Runway DPCM** :
- 8 bytes d'initialisation au début de chaque packet
- Primers (19922 + 21024 samples) : runway INCLUS
- Packets réguliers (2213 bytes) : runway EXCLU (audioPos avance de 2205)

### Algorithme DPCM16

```cpp
// Décompression différentielle avec table de lookup
nextSample = prevSample ± tableDPCM16[delta];

// Clamping (notre amélioration vs wrapping ScummVM)
if (nextSample > 32767) nextSample = 32767;
else if (nextSample < -32768) nextSample = -32768;
```

### Compression vidéo

- **LZS** : Lempel-Ziv-Storer (compression sans perte)
- **Palette** : HunkPalette 256 couleurs RGB
- **Format** : 8-bit indexé → RGB 24-bit (PPM)

---

## 📚 Documentation

| Section | Fichier | Description |
|---------|---------|-------------|
| **Vue d'ensemble** | [docs/README.md](docs/README.md) | Index principal |
| **Référence ScummVM** | [docs/reference/](docs/reference/) | Format Robot + implémentation audio |
| **Notre projet** | [docs/project/OUR_IMPLEMENTATION.md](docs/project/OUR_IMPLEMENTATION.md) | Architecture et différences |
| **Technique** | [docs/technical/](docs/technical/) | Encodage audio, extraction L/R |
| **Outils** | [tools/README.md](tools/README.md) | Scripts Python |

### Documentation clé

📄 **Référence ScummVM** :
- [SCUMMVM_ROBOT_FORMAT.md](docs/reference/SCUMMVM_ROBOT_FORMAT.md) - Spécification format Robot v5/v6
- [SCUMMVM_AUDIO_IMPLEMENTATION.md](docs/reference/SCUMMVM_AUDIO_IMPLEMENTATION.md) - Buffer circulaire, DPCM, interpolation

📄 **Notre implémentation** :
- [OUR_IMPLEMENTATION.md](docs/project/OUR_IMPLEMENTATION.md) - Architecture, pipeline, différences vs ScummVM
- [AUDIO_ENCODING.md](docs/technical/AUDIO_ENCODING.md) - Comparaison ScummVM vs notre approche
- [AUDIO_EXTRACTION_LR.md](docs/technical/AUDIO_EXTRACTION_LR.md) - Extraction canaux EVEN/ODD

---

## 🧪 Tests et validation

```bash
# Test extraction complète
./validate.sh ScummVM/rbt/91.RBT

# Vérification synchronisation
python3 tools/test_audio_video_sync.py output/

# Analyse qualité audio
python3 -c "
import struct
with open('output/audio.raw.pcm', 'rb') as f:
    data = f.read()
samples = len(data) // 2
zeros = sum(1 for i in range(0, len(data), 2) 
            if struct.unpack('<h', data[i:i+2])[0] == 0)
print(f'Zéros: {zeros}/{samples} ({100*zeros/samples:.2f}%)')
"
```

**Métriques qualité (91.RBT)** :
- Zéros : ~0.04% (98 sur 238,302 samples)
- Discontinuités >5000 : 36 (vs 111,614 avant optimisations)
- Amélioration : ~3100× réduction des artefacts

---

## 🎮 Jeux compatibles

✅ **Phantasmagoria** (Sierra, 1995)  
✅ **Gabriel Knight 2: The Beast Within** (Sierra, 1995)  
✅ **King's Quest VII** (Sierra, 1994)  
✅ **Tous les jeux SCI** utilisant Robot v5/v6

---

## 🔧 Dépendances

**Compilation** :
- CMake ≥ 3.10
- Compilateur C++11 (g++, clang)

**Runtime** :
- Python ≥ 3.8 (pour scripts)
- FFmpeg (optionnel, pour MP4)

**Bibliothèques** :
- Aucune dépendance externe (code standalone)

---

## 📊 Performance

## 📊 Performance

**Fichier test** : 91.RBT (Phantasmagoria 2, 90 frames, 9 secondes)

| Opération | Temps | Détails |
|-----------|-------|---------|
| **Extraction C++** | ~0.5s | Vidéo + audio |
| • Décompression LZS | ~0.3s | 90 frames 320×240 |
| • Décompression DPCM | ~0.2s | 171,485 samples |
| **Génération MP4** | ~2s | FFmpeg H.264 + AAC |
| **TOTAL** | ~2.5s | Pipeline complet |

**Sortie** :
- Frames PPM : ~27 MB (90 × 300 KB)
- Audio PCM : ~685 KB (342,970 bytes)
- Vidéo MP4 : ~200 KB (compression H.264)

---

## 🐛 Debugging

### Logs verbeux

```bash
# Activer logs détaillés
./src/robot_decoder/build/robot_decoder input.RBT output/ 90 audio 2>&1 | tee debug.log

# Vérifier:
# ✓ "Added evenPrimer: pos=0, size=19922"
# ✓ "Added oddPrimer: pos=2, size=21024"
# ✓ "Frame X: audioPos=Y bufferIndex=Z (EVEN/ODD)"
# ✓ "Streaming complete: read N samples"
```

### Analyse discontinuités

```python
import struct
import numpy as np

# Charger audio
with open('output/audio.raw.pcm', 'rb') as f:
    samples = np.frombuffer(f.read(), dtype=np.int16)

# Détecter sauts importants
diffs = np.abs(np.diff(samples))
large = np.where(diffs > 5000)[0]

print(f"Samples: {len(samples)}")
print(f"Zéros: {np.sum(samples == 0)}")
print(f"Discontinuités >5000: {len(large)}")
print(f"Max jump: {diffs.max()}")
```

---

## 🚧 Limitations connues

1. **Format** : Robot v5/v6 uniquement (pas v1-v4)
2. **Compression** : LZS vidéo (pas RLE)
3. **Palette** : HunkPalette seulement
4. **Audio** : DPCM16 mono (pas DPCM8, pas stéréo natif)
5. **Plateforme** : Testé sur Linux/Docker (support Mac/Windows à venir)

---

## 🗺️ Roadmap

- [ ] Support Robot v4 (format différent)
- [ ] Décodage RLE vidéo (ancien format)
- [ ] Export MP4 natif (sans FFmpeg)
- [ ] GUI extraction batch
- [ ] Builds Windows/macOS

---

## 🙏 Remerciements

- **ScummVM Team** - Implémentation de référence
- **Sierra On-Line** - Format Robot original
- Communauté des préservateurs de jeux rétro

---

## 📜 Licence

**GPL-3.0** - Même licence que ScummVM

Ce projet est dérivé de ScummVM et respecte sa licence GPL-3.0.  
Voir [LICENSE](LICENSE) pour les détails.

---

## 🔗 Liens

- 📦 **Projet** : [GitHub - extractor_sierra](https://github.com/proxymo75015/extractor_sierra)
- 🎮 **ScummVM** : [scummvm.org](https://www.scummvm.org/)
- 📖 **Documentation** : [docs/](docs/)
- 🐛 **Issues** : [GitHub Issues](https://github.com/proxymo75015/extractor_sierra/issues)

---

## 💬 Support

Pour questions, bugs ou contributions :

1. **Issues GitHub** : [Créer un issue](https://github.com/proxymo75015/extractor_sierra/issues/new)
2. **Documentation** : Consulter [docs/](docs/)
3. **Exemples** : Voir [tools/README.md](tools/README.md)

---
