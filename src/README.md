# Robot Video Extractor - Code Source

Code source pour l'extraction et la conversion des fichiers vidéo Robot (.RBT) de Sierra SCI (1994-1997).

## Structure du Code

### Programmes Principaux

- **`export_robot_mkv.cpp`** - Export MKV multi-couches (recommandé)
- **`main.cpp`** - Export PNG/WAV/MP4 classique (robot_extractor)

### Modules Core (`core/`)

- **`rbt_parser.cpp/.h`** - Parseur principal de fichiers RBT
  - Lecture des chunks vidéo/audio
  - Extraction de frames et audio
  - Gestion de la palette RGB
  - Décompression LZS intégrée

### Formats (`formats/`)

- **`robot_mkv_exporter.cpp/.h`** - Export MKV 4 pistes
  - Décomposition BASE/REMAP/ALPHA/LUMINANCE
  - Encodage H264/H265/VP9/FFV1
  - Génération PNG par piste
  - Métadonnées Matroska

- **`decompressor_lzs.cpp/.h`** - Décompression LZS
  - Sliding window 4096 bytes
  - Tokens 12-bit (offset) + 4-bit (longueur)
  - Support chunks compressés

- **`dpcm.cpp/.h`** - Décodeur DPCM16
  - Audio différentiel 16-bit
  - 2 canaux entrelacés (EVEN/ODD)
  - Interpolation 22050 Hz

### Utilitaires (`utils/`)

- **`file_utils.cpp/.h`** - Helpers I/O fichiers

## Compilation

```bash
cmake .
cmake --build .
```

**Binaires générés** :
- `export_robot_mkv` (268 KB)
- `robot_extractor` (240 KB)

## Utilisation

### Export MKV Multi-couches

```bash
./export_robot_mkv <input.rbt> <output_dir> [codec]
```

Codecs : `h264` (défaut), `h265`, `vp9`, `ffv1`

### Export PNG/WAV Classique

```bash
./robot_extractor <input.rbt> <output_dir> [num_frames]
```

## Détails Techniques

### Parsing RBT

Le parseur (`rbt_parser.cpp`) lit séquentiellement :

1. **En-tête** (14 bytes)
   - Magic : 0x0016 (v5) ou 0x0006 (v6)
   - Nombre de frames
   - Audio primer size

2. **Palette** (768 bytes)
   - 256 entrées RGB (3 bytes chacune)

3. **Frames** (boucle)
   - Video chunks compressés LZS
   - Audio chunks DPCM16
   - Entrelacement vidéo/audio

### Classification des Pixels

```cpp
// BASE (0-235) : Couleurs fixes
if (pixelIdx <= 235) {
    baseRGB[i] = palette[pixelIdx];
}

// REMAP (236-254) : Recoloration
else if (pixelIdx <= 254) {
    remapRGB[i] = palette[pixelIdx];
}

// SKIP (255) : Transparent
else {
    alphaMask[i] = 0; // Transparent
}
```

### Export MKV

```cpp
// Décomposition en 4 couches
decomposeRobotFrame(frameData, layerFrame);

// Génération PNG par piste
savePNG("base.png", layerFrame.baseRGB);
savePNG("remap.png", layerFrame.remapRGB);
savePNG("alpha.png", layerFrame.alphaMask);
savePNG("luminance.png", layerFrame.luminanceGray);

// Encodage FFmpeg
ffmpeg -i base_%04d.png -c:v libx264 track0.mkv
ffmpeg -i remap_%04d.png -c:v libx264 track1.mkv
ffmpeg -i alpha_%04d.png -c:v libx264 track2.mkv
ffmpeg -i luminance_%04d.png -c:v libx264 track3.mkv

// Multiplexage
ffmpeg -i track0.mkv -i track1.mkv -i track2.mkv -i track3.mkv -i audio.wav \
       -map 0 -map 1 -map 2 -map 3 -map 4 output.mkv
```

### Conversion Luminance

```cpp
// ITU-R BT.601 (Standard TV)
uint8_t Y = 0.299 * R + 0.587 * G + 0.114 * B;
```

## Tests

Fichiers de test dans `ScummVM/rbt/` :
- **91.RBT** : 90 frames, 9.00s, 1.4 MB
- **170.RBT** : 113 frames, 11.30s, 1.8 MB
- **212.RBT** : 33 frames, 3.30s, 550 KB

Tous testés avec succès (100% extraction).

## Références Code

- ScummVM : `engines/sci/graphics/robot.*`
- LZS : Implementation basée sur LZSS standard
- DPCM : `audio/decoders/sierra_audio.cpp`

- **Vidéo** : Frames extraites en PPM RGB (décompression LZS)
- **Audio** : DPCM16 décodé avec clamping (pas de wrapping)
- **Structure modulaire** : core/formats/utils pour meilleure maintenabilité
- Basé sur l'implémentation ScummVM avec améliorations pour extraction batch
