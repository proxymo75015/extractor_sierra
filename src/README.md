robot_decoder
=================

Décodeur autonome pour fichiers Robot `.rbt` de Sierra SCI.

## Structure

```
src/
├── main.cpp              # Point d'entrée
├── core/                 # Cœur du décodeur
│   ├── rbt_parser.*     # Parser format RBT
│   └── robot_audio_stream.* # Buffer audio (ScummVM)
├── formats/              # Codecs
│   ├── dpcm.*           # Décodeur DPCM16
│   ├── lzs.*            # Compression LZS
│   └── decompressor_lzs.* # Décompression LZS
└── utils/                # Utilitaires
    ├── sci_util.*       # Utils SCI/ScummVM
    └── memory_stream.h  # Stream mémoire
```

## Build (dans dev container)

```bash
mkdir build && cd build
cmake ..
make -j
```

## Utilisation

```bash
./robot_decoder <path/to/file.rbt> <out_dir> [num_frames]
```

**Arguments** :
- `<path/to/file.rbt>` : Fichier RBT source
- `<out_dir>` : Répertoire de sortie
- `[num_frames]` : Nombre de frames à extraire (optionnel, toutes par défaut)

**Sortie** :
- `<out_dir>/metadata.txt` : Métadonnées basiques
- `<out_dir>/palette.bin` : Palette brute (si présente)
- `<out_dir>/frames/frame_XXXX_cel_00.ppm` : Frames RGB 24-bit
- `<out_dir>/LEFT.wav` : Canal gauche @ 11025Hz mono
- `<out_dir>/RIGHT.wav` : Canal droit @ 11025Hz mono
- `<out_dir>/output.mp4` : Vidéo H.264 + audio AAC stéréo

## Notes

- **Vidéo** : Frames extraites en PPM RGB (décompression LZS)
- **Audio** : DPCM16 décodé avec clamping (pas de wrapping)
- **Structure modulaire** : core/formats/utils pour meilleure maintenabilité
- Basé sur l'implémentation ScummVM avec améliorations pour extraction batch
