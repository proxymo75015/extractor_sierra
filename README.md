# Sierra Robot Video Extractor

Extracteur et convertisseur pour fichiers vidéo Robot (`.RBT`) de Sierra SCI utilisés dans les jeux d'aventure des années 90.

## 🎯 Fonctionnalités

### Formats de sortie supportés

1. **MKV Multi-couches** (`export_robot_mkv`) - **Recommandé**
   - 4 pistes vidéo séparées (BASE, REMAP, ALPHA, LUMINANCE)
   - Audio PCM 48 kHz mono
   - Codecs : H.264, H.265, VP9, FFV1
   - Métadonnées complètes

2. **PNG + WAV + MP4** (`robot_extractor`)
   - Extraction frame par frame en PNG
   - Audio WAV stéréo 22050 Hz
   - Vidéo MP4 H.264 standard

## 📦 Installation

### Prérequis

- **Compilateur C++11** (GCC 7+, Clang 5+, MSVC 2017+)
- **CMake 3.10+**
- **FFmpeg** (pour l'export MKV/MP4)

### Dev Container (VS Code)

Le projet inclut une configuration Dev Container complète :

```bash
# Ouvrir dans VS Code avec l'extension Dev Containers
code .
# Puis : "Reopen in Container"
```

### Compilation

```bash
cmake .
cmake --build .
```

**Binaires générés** :
- `export_robot_mkv` - Export MKV multi-couches (recommandé)
- `robot_extractor` - Export PNG/WAV/MP4 classique

## 🚀 Utilisation

### Export MKV Multi-couches (Recommandé)

Le programme scanne automatiquement le répertoire `RBT/` et traite tous les fichiers `.RBT` qu'il contient.

**Préparation** :
```bash
# Créer le répertoire RBT et y placer vos fichiers
mkdir RBT
cp /chemin/vers/vos/fichiers/*.RBT RBT/
```

**Lancement** :
```bash
./export_robot_mkv [codec]
```

**Codecs disponibles** :
- `h264` (défaut) - Universel, compatible partout
- `h265` - Meilleure compression, +moderne
- `vp9` - Open source, excellente qualité
- `ffv1` - Lossless, archivage

**Exemple** :
```bash
./export_robot_mkv h264
```

**Résultats** :
```
output/
├── 91/
│   ├── 91_video.mkv        # MKV 4 pistes + audio
│   ├── 91_audio.wav        # Audio natif 22050 Hz
│   ├── 91_composite.mp4    # Vidéo composite H.264 + AAC
│   └── 91_metadata.txt     # Métadonnées complètes
├── 170/
│   ├── 170_video.mkv
│   ├── 170_audio.wav
│   ├── 170_composite.mp4
│   └── 170_metadata.txt
└── ...
```

**Structure du MKV** :
- **Track 0 (BASE)** : Pixels RGB fixes (indices 0-235)
- **Track 1 (REMAP)** : Zones recoloriables (indices 236-254)
- **Track 2 (ALPHA)** : Masque de transparence (index 255)
- **Track 3 (LUMINANCE)** : Niveaux de gris Y (aperçu)
- **Track 4 (AUDIO)** : PCM 16-bit 48 kHz mono

### Export PNG/WAV/MP4 Classique

```bash
./robot_extractor <fichier.rbt> <dossier_sortie> [nb_frames]
```

**Exemple** :
```bash
./robot_extractor ScummVM/rbt/91.RBT output_91
```

**Fichiers générés** :
```
output_91/
├── frames/              # PNG 320x240 RGB
│   ├── frame_0000_cel_00.png
│   ├── frame_0001_cel_00.png
│   └── ...
├── LEFT.wav             # Audio gauche 11025 Hz
├── RIGHT.wav            # Audio droit 11025 Hz
├── output.mp4           # Vidéo H.264 + AAC stéréo
├── palette.bin          # Palette RGB brute
└── metadata.txt         # Métadonnées
```

## 📊 Format Robot SCI

### Structure du fichier

```
[PRIMER]        # Données audio initiales (EVEN/ODD)
[PALETTE]       # Palette RGB 256 couleurs
[FRAME 0]       # Vidéo + Audio entrelacés
  ├── Video     # Cels compressés LZS
  └── Audio     # DPCM16 compressé
[FRAME 1]
...
[FRAME N]
```

### Classification des pixels

| Type | Indices | Usage | Piste MKV |
|------|---------|-------|-----------|
| **BASE** | 0-235 | Couleurs fixes opaques | Track 0 (RGB) |
| **REMAP** | 236-254 | Zones recoloriables | Track 1 (RGB) |
| **SKIP** | 255 | Transparent | Track 2 (ALPHA) |

### Compression

- **Vidéo** : LZS (Lempel-Ziv-Storer)
- **Audio** : DPCM16 (Delta Pulse Code Modulation)
- **Fréquence audio** : 22050 Hz mono (2 canaux entrelacés)
- **Framerate** : 10 fps (typique)

## 🎮 Jeux supportés

Testé avec :
- Phantasmagoria (1995)
- The Beast Within: A Gabriel Knight Mystery (1995)
- King's Quest VII (1994)
- Torin's Passage (1995)

Tous les jeux Sierra SCI utilisant le format Robot v5/v6 devraient fonctionner.

## 📁 Structure du projet

```
extractor_sierra/
├── src/
│   ├── core/                  # Parseur Robot
│   │   └── rbt_parser.cpp
│   ├── formats/               # Codecs
│   │   ├── decompressor_lzs.cpp  # Décompression LZS
│   │   ├── dpcm.cpp              # Décodeur DPCM16
│   │   └── robot_mkv_exporter.cpp # Export MKV
│   ├── utils/                 # Utilitaires
│   ├── main.cpp               # robot_extractor
│   └── export_robot_mkv.cpp   # export_robot_mkv
├── ScummVM/rbt/               # Fichiers RBT de test
├── examples/                  # Exemples de sortie
├── docs/                      # Documentation
├── CMakeLists.txt
└── README.md
```

## 🔬 Technique

### Décompression LZS

Le format Robot utilise une variante de LZS avec :
- Sliding window de 4096 bytes
- Tokens de 12 bits (offset) + 4 bits (longueur)
- Compression par blocs (chunks)

### Audio DPCM16

- Codage différentiel 16-bit
- 2 canaux entrelacés (EVEN/ODD)
- Runway de 8 samples au début de chaque bloc
- Interpolation des échantillons manquants

### Export MKV

- 4 pistes vidéo parallèles encodées séparément
- Format Matroska supportant les multi-tracks nativement
- Métadonnées de piste pour identification
- Resampling audio 22050 Hz → 48 kHz (SoXR)

## 🐛 Dépannage

### "No such file or directory"

Vérifiez les chemins et assurez-vous que le fichier RBT existe :
```bash
ls -l ScummVM/rbt/*.RBT
```

### "FFmpeg not found"

Installez FFmpeg :
```bash
# Ubuntu/Debian
sudo apt-get install ffmpeg

# macOS
brew install ffmpeg

# Windows
# Télécharger depuis https://ffmpeg.org/download.html
```

### Pistes REMAP/ALPHA vides

C'est normal ! La plupart des vidéos Robot n'utilisent pas :
- **REMAP** : Fonctionnalité optionnelle pour la recoloration
- **ALPHA** : Transparence variable (255 = fond transparent)

Seule la piste **BASE** contient généralement toute l'image.

### Problèmes de compilation

```bash
# Nettoyer et recompiler
rm -rf CMakeCache.txt CMakeFiles/
cmake .
cmake --build . --clean-first
```

## 📖 Références

- [ScummVM Robot Engine](https://github.com/scummvm/scummvm/tree/master/engines/sci/graphics)
- [SCI Specifications](http://scummvm.org/docs/SCI_Specifications.pdf)
- [LZS Compression](https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Storer%E2%80%93Szymanski)
- [Matroska Format](https://www.matroska.org/technical/specs/index.html)

## 📝 Licence

MIT License - Voir fichier `LICENSE`

## 🙏 Crédits

Basé sur l'implémentation ScummVM du moteur Robot SCI.
