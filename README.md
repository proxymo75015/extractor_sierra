# 🎬 Extractor Sierra

> Extracteur vidéo professionnel pour fichiers Robot (.RBT) de Sierra SCI32  
> Génère MKV multicouche + MOV ProRes 4444 RGBA avec transparence

[![Version](https://img.shields.io/badge/version-3.0.0-blue.svg)](LICENSE)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

## 📋 Vue d'ensemble

**robot_extractor** est un extracteur vidéo unifié pour les fichiers Robot (.RBT) des jeux Sierra SCI32 (Phantasmagoria). Il génère automatiquement :
- **MKV multicouche** (4 pistes vidéo : BASE, REMAP, ALPHA, LUMINANCE)
- **MOV ProRes 4444** avec canal alpha 10-bit (yuva444p10le)
- **PNG RGBA** préservant la transparence complète
- **Audio WAV** 22050 Hz mono (décodage DPCM)

### ✨ Fonctionnalités

- 🎥 **Extraction unifiée** : Un seul programme, tous les formats
- 🎨 **Modes intelligents** : Canvas 630×450 (si coordonnées RESSCI) ou tight crop auto
- 📦 **MKV 4 pistes** : Séparation complète des couches (base, remap, alpha, luminance)
- 🎬 **MOV ProRes 4444** : Alpha 10-bit pour composition professionnelle
- 🖼️ **PNG RGBA** : Frames transparentes conservées dans `{robot}_frames/`
- 📍 **Intégration RESSCI** : Détection auto coordonnées + positionnement canvas
- 🔊 **Audio DPCM** : Interpolation mono 22050 Hz vers WAV

## 🚀 Installation

### Prérequis

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake ffmpeg

# macOS
brew install cmake ffmpeg
```

### Compilation

```bash
git clone https://github.com/proxymo75015/robot_extract.git
cd robot_extract
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

## 🎯 Usage

### Extraction simple

```bash
./build/robot_extractor RBT/ Resource/ output/
```

**Arguments :**
- `RBT/` : Répertoire contenant fichiers .RBT
- `Resource/` : Répertoire RESSCI (RESMAP.*, RESSCI.*) pour coordonnées
- `output/` : Répertoire de sortie

### Fichiers générés

Pour chaque robot `{ID}.RBT`, génère dans `output/{ID}/` :

```
output/1000/
├── 1000_video.mkv                # MKV 4 pistes (BASE+REMAP+ALPHA+LUMINANCE)
├── 1000_video_composite.mov      # MOV ProRes 4444 RGBA alpha 10-bit
├── 1000_audio.wav                # Audio WAV 22050 Hz mono
├── 1000_frames/                  # PNG RGBA avec transparence
│   ├── frame_0000.png
│   ├── frame_0001.png
│   └── ...
└── metadata.txt                  # Métadonnées (ID, frames, FPS, position, etc.)
```

## 📊 Modes de rendu

### Mode Canvas (630×450)
- **Condition** : Coordonnées trouvées dans RESSCI ET position ≠ (0,0)
- **Usage** : Robots positionnés sur fond virtuel du jeu
- **Exemple** : Robot 1000 à position (270, 150)

### Mode Tight Crop
- **Condition** : Pas de coordonnées OU position (0,0)
- **Calcul** : Bounding box globale sur tous pixels visibles (alpha > 0)
- **Réduction** : ~69% taille moyenne vs crop simple
- **Exemple** : Robot 1180 → 133×296 au lieu de 426×394

## 📖 Documentation

### 📚 Documentation de référence

- [Format RBT](docs/reference/FORMAT_RBT_DOCUMENTATION.md) - Structure fichiers Robot
- [Décodeur LZS](docs/reference/LZS_DECODER_DOCUMENTATION.md) - Compression LZS
- [Décodeur DPCM](docs/reference/DPCM16_DECODER_DOCUMENTATION.md) - Audio DPCM
- [Palette Robot](docs/reference/ROBOT_PALETTE_DECODING.md) - Système palette
- [Remapping](docs/reference/ROBOT_PALETTE_REMAPPING.md) - Remapping couleurs
- [Format SOL](docs/reference/SOL_FILE_FORMAT_DOCUMENTATION.md) - Fichiers SOL
- [GfxPalette32](docs/reference/GFXPALETTE32_SYSTEM.md) - Système palette SCI32
- [GfxRemap SCI16](docs/reference/GFXREMAP_SCI16.md) - Remapping SCI16
- [Virtual Background](docs/reference/ROBOT_VIRTUAL_BACKGROUND.md) - Backgrounds virtuels
- [Export OpenEXR](docs/reference/OPENEXR_EXPORT.md) - Export format EXR

## 🔍 Détails techniques

### MKV 4 pistes
- **Piste 0 (BASE)** : RGB pixels 0-235 (base layer)
- **Piste 1 (REMAP)** : RGB pixels 236-254 (remap layer)
- **Piste 2 (ALPHA)** : Masque binaire (255 = transparent)
- **Piste 3 (LUMINANCE)** : Grayscale Y
- **Audio** : PCM 16-bit 48 kHz mono

### MOV ProRes 4444
- **Codec** : Apple ProRes 4444 (profile 4)
- **Format pixel** : yuva444p10le (YUV 4:4:4 + alpha 10-bit)
- **Transparence** : Canal alpha complet pour composition

### Tight Crop Algorithm
1. Parcours tous pixels alpha > 0 de toutes les frames
2. Calcul bounding box globale : `globalMinX/Y`, `globalMaxX/Y`
3. Dimensions finales : `width = maxX - minX + 1`, `height = maxY - minY + 1`
4. Application offset crop : `croppedX = x - cropOffsetX`

## 🏗️ Architecture

### Programme unifié
`robot_extractor` intègre toutes les fonctionnalités :
- Parsing RBT (format Robot)
- Parsing RESSCI (coordonnées)
- Décodage LZS (compression)
- Décodage DPCM (audio)
- Génération MKV multicouche
- Génération MOV ProRes 4444
- Export PNG RGBA
- Calcul tight crop bbox

### Fichiers sources principaux
- `src/main.cpp` : Programme principal robot_extractor
- `src/core/rbt_parser.cpp` : Parser format Robot
- `src/core/ressci_parser.cpp` : Parser RESSCI (coordonnées)
- `src/formats/robot_mkv_exporter.cpp` : Export MKV/MOV
- `src/formats/lzs.cpp` : Décompression LZS
- `src/formats/dpcm.cpp` : Décodage audio DPCM

## 📊 Exemples de résultats

### Robot 1000 (Canvas mode)
```
Dimensions: 630×450
Frames: 143
Position: (270, 150)
MKV: 1.6 MB
MOV: 5.6 MB
PNG: 143 frames × 26 KB
```

### Robot 1180 (Tight crop)
```
Dimensions: 133×296 (vs 426×394 crop simple = -69%)
Frames: 35
Position: N/A
MKV: 467 KB
MOV: 1.8 MB
PNG: 35 frames
```

### Robot 230 (Tight crop)
```
Dimensions: 170×342 (vs 390×462 crop simple = -68%)
Frames: 207
Position: N/A
MKV: 2.5 MB
MOV: 9.3 MB
PNG: 207 frames
```

## 🤝 Contribution

Les contributions sont bienvenues ! Pour proposer des améliorations :

1. Fork le projet
2. Créer une branche (`git checkout -b feature/amelioration`)
3. Commit les changements (`git commit -m 'Ajout fonctionnalité'`)
4. Push la branche (`git push origin feature/amelioration`)
5. Ouvrir une Pull Request

## 📜 Licence

MIT License - voir [LICENSE](LICENSE)

## 🙏 Crédits

- **Format SCI32** : Documentation ScummVM
- **Décodeurs** : Inspirés de ScummVM (LGPL)
- **FFmpeg** : Encodage vidéo et audio

## 📞 Support

- **Issues** : [GitHub Issues](https://github.com/proxymo75015/robot_extract/issues)
- **Documentation** : [docs/README.md](docs/README.md)

---

**Version 3.0.0** - Extracteur unifié avec MKV multicouche + MOV ProRes 4444 RGBA

**Développé avec ❤️ pour la préservation des jeux classiques Sierra**
