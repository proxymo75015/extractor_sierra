# Robot RBT Extractor

## Vue d'ensemble

Extracteur et décodeur pour les fichiers vidéo Robot (.RBT) de Sierra SCI, basé sur l'implémentation de référence ScummVM.

### Fonctionnalités

✅ Extraction vidéo (frames PPM/PNG)  
✅ Extraction audio (PCM 16-bit, 22050 Hz)  
✅ Décompression LZS (vidéo)  
✅ Décodage DPCM16 (audio)  
✅ Séparation canaux LEFT/RIGHT  
✅ Génération vidéo MP4/AVI  
✅ Support Robot v5 (320×240) et v6 (640×480)

### Architecture

```
┌─────────────────────────────────────────────────────┐
│                  robot_decoder (C++)                │
│  ┌─────────────────────────────────────────────┐   │
│  │  RbtParser                                   │   │
│  │  ├─ parseHeader()      (RBT header)         │   │
│  │  ├─ extractFrame()     (LZS → PPM)          │   │
│  │  └─ extractAllAudio()  (DPCM16 → PCM)       │   │
│  └─────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────┐   │
│  │  RobotAudioStream (ScummVM)                 │   │
│  │  ├─ addPacket()        (streaming)          │   │
│  │  ├─ fillRobotBuffer()  (décompression)      │   │
│  │  └─ readBuffer()       (lecture)            │   │
│  └─────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────┐   │
│  │  Decoders                                    │   │
│  │  ├─ deDPCM16Mono()     (audio)              │   │
│  │  └─ decodeLZS()        (vidéo)              │   │
│  └─────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────┐
│              Scripts Python (workflows)             │
│  ├─ extract_lr_simple.py     (L/R séparés)         │
│  ├─ extract_and_make_video.py (vidéo complète)     │
│  └─ test_audio_video_sync.py  (validation)         │
└─────────────────────────────────────────────────────┘
```

## Compilation

### Prérequis

- CMake >= 3.10
- C++17
- GCC/Clang

### Build

```bash
cd src/robot_decoder
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

Binaire généré : `src/robot_decoder/build/robot_decoder`

## Utilisation

### 1. Extraction complète (vidéo + audio)

```bash
./src/robot_decoder/build/robot_decoder input.RBT output_dir/ <nb_frames> all
```

**Sortie** :
- `output_dir/frames/frame_XXXX.ppm` - Frames vidéo (PPM 8-bit)
- `output_dir/audio.raw.pcm` - Audio stéréo 16-bit 22050 Hz
- `output_dir/palette.pal` - Palette RGB 256 couleurs

**Exemple** :
```bash
./src/robot_decoder/build/robot_decoder ScummVM/rbt/91.RBT output/ 90 all
```

### 2. Extraction audio seule

```bash
./src/robot_decoder/build/robot_decoder input.RBT output_dir/ <nb_frames> audio
```

### 3. Extraction vidéo seule

```bash
./src/robot_decoder/build/robot_decoder input.RBT output_dir/ <nb_frames> video
```

### 4. Génération vidéo MP4

```bash
python3 extract_and_make_video.py input.RBT output_dir/
```

**Dépendances** :
- FFmpeg (pour génération vidéo)
- Python 3.7+

**Sortie** :
- `output_dir/output.mp4` - Vidéo avec audio

### 5. Extraction canaux LEFT/RIGHT

```bash
python3 extract_lr_simple.py input.RBT output_dir/
```

**Sortie** :
- `output_dir/<filename>_LEFT_simple.wav` - Canal gauche (mono 11025 Hz)
- `output_dir/<filename>_RIGHT_simple.wav` - Canal droit (mono 11025 Hz)

**Note** : Nécessite d'avoir d'abord lancé `robot_decoder` pour générer le log d'extraction.

## Structure des fichiers

```
extractor_sierra/
├── src/robot_decoder/         # Code source C++
│   ├── rbt_parser.cpp         # Parser RBT principal
│   ├── rbt_parser.h
│   ├── dpcm.cpp              # Décodeur DPCM16
│   ├── dpcm.h
│   ├── lzs.cpp               # Décompresseur LZS
│   ├── lzs.h
│   └── main.cpp              # Point d'entrée
├── ScummVM/                   # Code ScummVM adapté
│   ├── robot.cpp             # RobotAudioStream
│   └── robot.h
├── docs/                      # Documentation
│   ├── reference/            # Docs ScummVM (référence)
│   ├── project/              # Docs projet
│   └── technical/            # Notes techniques
├── extract_lr_simple.py      # Extraction L/R
├── extract_and_make_video.py # Workflow vidéo
└── test_audio_video_sync.py  # Tests
```

## Formats de sortie

### Audio

**Format** : PCM 16-bit little-endian  
**Channels** : 2 (stéréo, L=R duplication du mono)  
**Sample rate** : 22050 Hz  
**Durée** : Variable selon fichier

**Structure interne** :
- MONO 22050 Hz composé de deux canaux 11025 Hz (EVEN/ODD)
- Canaux entrelacés : EVEN aux positions paires, ODD aux positions impaires
- Interpolation multi-pass pour combler les gaps

### Vidéo

**Frames** : PPM P6 (binary RGB)  
**Résolution** : 320×240 (v5) ou 640×480 (v6)  
**Palette** : 256 couleurs RGB  
**Format** : 8-bit indexé → conversion RGB 24-bit

## Différences avec ScummVM

| Aspect | ScummVM | Notre projet |
|--------|---------|--------------|
| **Usage** | Playback temps-réel | Extraction batch |
| **Audio buffer** | Circulaire (streaming) | Linéaire (offline) |
| **DPCM overflow** | Wrapping (x86 compat) | Clamping (qualité) |
| **Interpolation** | Par canal | Multi-pass global |
| **Output** | Playback direct | Fichiers (PCM/PPM) |
| **Dépendances** | ScummVM engine | Standalone |

## Limitations

- ❌ Pas de playback temps-réel (extraction seulement)
- ❌ Pas de support des subtitles Robot
- ⚠️ Interpolation audio peut laisser quelques gaps résiduels
- ⚠️ Nécessite de connaître le nombre de frames à l'avance

## Performance

**Fichier test** : `91.RBT` (90 frames, 9 secondes)

```
Extraction complète : ~0.5s
  - Vidéo : ~0.3s (90 frames PPM)
  - Audio : ~0.2s (198,450 samples + interpolation)
Génération MP4 : ~2s (FFmpeg)
```

## Tests

### Validation audio/vidéo

```bash
python3 test_audio_video_sync.py output_dir/
```

Vérifie :
- Synchronisation audio/vidéo
- Durée cohérente
- Nombre de frames correct
- Qualité audio (zéros, discontinuités)

## Support

**Formats supportés** :
- ✅ Robot v5 (PC, Little-endian)
- ✅ Robot v6 (Mac, Big-endian avec audio LE)

**Fichiers testés** :
- Sierra Phantasmagoria
- Sierra Gabriel Knight 2
- Sierra King's Quest VII

## Licence

GPL v3+ (compatible ScummVM)

## Contributeurs

Basé sur l'excellent travail de l'équipe ScummVM.

## Références

- [ScummVM](https://www.scummvm.org/)
- [ScummVM Robot Decoder](https://github.com/scummvm/scummvm/blob/master/engines/sci/video/robot_decoder.cpp)
- [Sierra SCI Documentation](http://sciwiki.sierrahelp.com/)
