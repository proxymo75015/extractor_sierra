# Documentation - Extractor Sierra

Documentation complète du projet **robot_extractor** - Extracteur vidéo unifié pour fichiers Robot (.RBT) de Sierra SCI32.

---

## 📋 Vue d'ensemble

**robot_extractor** génère automatiquement :
- **MKV multicouche** (4 pistes : BASE, REMAP, ALPHA, LUMINANCE)
- **MOV ProRes 4444** avec alpha 10-bit (yuva444p10le)
- **PNG RGBA** préservant transparence complète
- **Audio WAV** 22050 Hz mono (décodage DPCM)

### Modes intelligents
- **Canvas 630×450** si coordonnées RESSCI trouvées
- **Tight crop automatique** sinon (réduction ~69% taille)

---

## 📚 Documentation de référence

### Formats Sierra SCI32

| Document | Description |
|----------|-------------|
| [FORMAT_RBT_DOCUMENTATION.md](reference/FORMAT_RBT_DOCUMENTATION.md) | Format vidéo Robot (.RBT) complet |
| [SOL_FILE_FORMAT_DOCUMENTATION.md](reference/SOL_FILE_FORMAT_DOCUMENTATION.md) | Format audio SOL Sierra |

### Décodeurs

| Document | Description |
|----------|-------------|
| [LZS_DECODER_DOCUMENTATION.md](reference/LZS_DECODER_DOCUMENTATION.md) | Décompression LZS/STACpack |
| [DPCM16_DECODER_DOCUMENTATION.md](reference/DPCM16_DECODER_DOCUMENTATION.md) | Décodage audio DPCM16 |

### Systèmes graphiques (ScummVM)

| Document | Description |
|----------|-------------|
| [GFXPALETTE32_SYSTEM.md](reference/GFXPALETTE32_SYSTEM.md) | Système palette SCI32 |
| [ROBOT_PALETTE_DECODING.md](reference/ROBOT_PALETTE_DECODING.md) | Décodage palette Robot |
| [ROBOT_PALETTE_REMAPPING.md](reference/ROBOT_PALETTE_REMAPPING.md) | Remapping palette Robot |
| [GFXREMAP_SCI16.md](reference/GFXREMAP_SCI16.md) | Système remap SCI16 |
| [ROBOT_VIRTUAL_BACKGROUND.md](reference/ROBOT_VIRTUAL_BACKGROUND.md) | Virtual background Robot |

### Export & formats

| Document | Description |
|----------|-------------|
| [OPENEXR_EXPORT.md](reference/OPENEXR_EXPORT.md) | Export format OpenEXR |


## 🚀 Démarrage rapide

### Installation et compilation

```bash
git clone https://github.com/proxymo75015/robot_extract.git
cd robot_extract
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Extraction batch

```bash
./build/export_robot_mkv RBT/
```

Génère pour chaque `{ID}.RBT` :
- `{ID}_video.mkv` - MKV 4 pistes + audio
- `{ID}_video.mov` - MOV ProRes 4444 RGBA + audio
- `{ID}_audio.wav` - Audio WAV 22050 Hz
- `{ID}_frames/` - PNG RGBA
- `{ID}_metadata.txt` - Métadonnées

### Modes automatiques

**Canvas 630×450** si coordonnées RESSCI trouvées :
```
Robot 260: position (257, 257) → canvas 630×450
```

**Tight crop** sinon :
```
Robot 161: 112×155 (crop automatique)
Robot 162: 258×332 (crop automatique)
```

---

## 🔍 Architecture technique

### MKV 4 pistes
- **Piste 0 (BASE)** : RGB pixels 0-235
- **Piste 1 (REMAP)** : RGB pixels 236-254
- **Piste 2 (ALPHA)** : Masque binaire (255 = transparent)
- **Piste 3 (LUMINANCE)** : Grayscale Y
- **Audio** : PCM 16-bit 48 kHz mono

### MOV ProRes 4444
- **Codec** : Apple ProRes 4444 profile 4
- **Format** : yuva444p10le (YUV 4:4:4 + alpha 10-bit)
- **Transparence** : Canal alpha complet

### Architecture ScummVM

**Mode CANVAS** (coordonnées RESSCI disponibles) :
```cpp
// Formule positionnement ScummVM
screenX = robotX + celX
screenY = robotY + celY - celHeight

// Exemple Robot 260 (position RESSCI: 257, 257)
// Cel: width=235, height=267, celX=0, celY=267
screenX = 257 + 0 = 257
screenY = 257 + 267 - 267 = 257
// Résultat: canvas 630×450 pixels
```

**Mode CROP** (pas de coordonnées RESSCI) :
```cpp
// Tight crop automatique
screenX = celX
screenY = celY - celHeight
// Résultat: dimensions minimales (bounding box)
```

---

## 📖 Structure projet

```
robot_extract/
├── build/
│   └── export_robot_mkv          # Programme extraction batch
├── src/
│   ├── export_robot_mkv.cpp      # Extraction batch MKV/MOV
│   ├── core/
│   │   ├── rbt_parser.cpp        # Parser Robot
│   │   ├── ressci_parser.cpp     # Parser RESSCI
│   │   └── scummvm_robot_helpers.cpp # Formules ScummVM
│   ├── formats/
│   │   ├── robot_mkv_exporter.cpp # Export MKV/MOV
│   │   ├── lzs.cpp               # Décompression LZS
│   │   └── dpcm.cpp              # Décodage DPCM
│   └── utils/
├── docs/
│   └── reference/                # Documentation formats
├── RBT/                          # Fichiers .RBT input
└── output/                       # Fichiers générés
```

---

## 🏆 Crédits

- **ScummVM Team** : Documentation formats SCI32
- **André Beck** : Documentation LZS/STACpack
- **Sierra On-Line** : Formats originaux

---

**Version 3.0.0** - Documentation mise à jour pour extracteur unifié


---

**Note** : Tous les documents sont fournis à des fins éducatives et de préservation.
