# 🎬 Extractor Sierra

> Extracteur vidéo professionnel pour fichiers Robot (.RBT) de Sierra SCI32  
> Génère MKV multicouche + MOV ProRes 4444 RGBA avec transparence

[![Version](https://img.shields.io/badge/version-3.1.0-blue.svg)](LICENSE)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

## 📋 Vue d'ensemble

**robot_extractor** est un extracteur vidéo pour les fichiers Robot (.RBT) des jeux Sierra SCI32 (Phantasmagoria). Il génère :
- **MKV multicouche** (4 pistes vidéo : BASE, REMAP, ALPHA, LUMINANCE)
- **MOV ProRes 4444** avec canal alpha 10-bit (yuva444p10le)
- **PNG RGBA** préservant la transparence complète
- **Audio WAV** 22050 Hz mono (décodage DPCM)

### ✨ Fonctionnalités

- 🎥 **Extraction complète** : Vidéo, audio et métadonnées
- 🎨 **Modes intelligents** : Canvas 640×480 (avec coordonnées) ou tight crop auto
- 📦 **MKV 4 pistes** : Séparation BASE, REMAP, ALPHA, LUMINANCE
- 🎬 **MOV ProRes 4444** : Alpha 10-bit pour composition professionnelle
- 🖼️ **PNG RGBA** : Frames transparentes dans `{robot}_frames/`
- 📍 **Coordonnées automatiques** : Extraction depuis scripts SCI (opcode 0x76 CALLK Robot)
- 🔊 **Audio DPCM** : Décodage vers WAV 22050 Hz mono

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

### Extraction complète

```bash
./build/export_robot_mkv RBT/ Resource/ output/
```

**Arguments :**
- `RBT/` : Répertoire contenant fichiers .RBT
- `Resource/` : Répertoire RESSCI (RESMAP.*, RESSCI.*) - optionnel
- `output/` : Répertoire de sortie

### Fichiers générés

Pour chaque robot `{ID}.RBT`, génère dans `output/{ID}/` :

```
output/260/
├── 260_video.mkv                # MKV 4 pistes (BASE+REMAP+ALPHA+LUMINANCE)
├── 260_video_composite.mov      # MOV ProRes 4444 RGBA alpha 10-bit
├── 260_audio.wav                # Audio WAV 22050 Hz mono
├── 260_frames/                  # PNG RGBA avec transparence
│   ├── frame_0000.png
│   ├── frame_0001.png
│   └── ...
├── 260_metadata.txt             # Métadonnées (ID, frames, FPS, position)
└── 260_coordinates.txt          # Coordonnées X,Y extraites depuis scripts
```

**Coordonnées automatiques** :
```
output/
└── robot_positions_final.txt    # Coordonnées extraites de tous les robots
                                 # Format: robotId X Y
                                 # Source: Scripts SCI (opcode 0x76 CALLK)
```

## 📊 Modes de rendu

### Mode Canvas (640×480)

- **Condition** : Coordonnées trouvées dans scripts SCI ET position ≠ (0,0)
- **Usage** : Robots positionnés sur fond virtuel du jeu
- **Exemple** : Robot 260 à position (309, 130)

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
- [Virtual Background](docs/reference/ROBOT_VIRTUAL_BACKGROUND.md) - Backgrounds virtuels

## 🔍 Détails techniques

### Extraction coordonnées

Le programme `export_robot_mkv` extrait automatiquement les coordonnées X,Y depuis les scripts SCI :

- **Méthode** : Parse bytecode SCI32, détecte opcode 0x76 (CALLK Robot)
- **Kernel IDs** : Filtre sur {57, 67, 74, 84} (appels Robot connus)
- **Validation** : robotId 1-9999, x/y entre -100 et 740/580
- **Format CALLK** : `0x76 <kernelId> <argc>` suivi de PUSHI parameters
- **Ordre params** : robotId, x, y, priority (empilés puis inversés)

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

### Programmes

- **`export_robot_mkv`** : Extraction complète RBT → MKV/MOV/PNG + coordonnées
- **`robot_extractor`** : Extraction basique RBT → PNG frames

### Fichiers sources

- `src/export_robot_mkv.cpp` : Programme principal
- `src/core/rbt_parser.cpp` : Parser format Robot
- `src/core/ressci_parser.cpp` : Parser RESSCI + extraction coordonnées
- `src/formats/robot_mkv_exporter.cpp` : Export MKV/MOV
- `src/formats/lzs.cpp` : Décompression LZS
- `src/formats/dpcm.cpp` : Décodage audio DPCM

## 📊 Exemples de résultats

### Robot 260 (Canvas mode)

```text
Dimensions: 640×480
Frames: 143
Position: (309, 130)
MKV: 1.6 MB
MOV: 5.6 MB
```

### Robot 170 (Canvas mode)

```text
Dimensions: 640×480
Frames: 35
Position: (169, 179)
MKV: 467 KB
MOV: 1.8 MB
```

### Robot 162 (Canvas mode)

```text
Dimensions: 640×480
Frames: 207
Position: (179, 182)
MKV: 2.5 MB
MOV: 9.3 MB
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

Version 3.1.0 - Extraction coordonnées automatique depuis scripts SCI  
Développé avec ❤️ pour la préservation des jeux classiques Sierra
