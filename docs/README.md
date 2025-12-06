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
git clone https://github.com/proxymo75015/extractor_sierra.git
cd extractor_sierra
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Extraction batch

```bash
./build/robot_extractor RBT/ Resource/ output/
```

Génère pour chaque `{ID}.RBT` :
- `{ID}_video.mkv` - MKV 4 pistes
- `{ID}_video_composite.mov` - MOV ProRes 4444 RGBA
- `{ID}_audio.wav` - Audio WAV 22050 Hz
- `{ID}_frames/` - PNG RGBA
- `metadata.txt` - Métadonnées

### Modes automatiques

**Canvas 630×450** si coordonnées RESSCI trouvées :
```
Robot 1000: position (270, 150) → canvas 630×450
```

**Tight crop** sinon :
```
Robot 1180: 133×296 (réduction 69% vs crop simple)
Robot 230: 170×342 (réduction 68% vs crop simple)
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

### Tight Crop Algorithm
```cpp
// 1. Calcul bounding box globale
for (frame : frames) {
    for (pixel : frame if alpha > 0) {
        globalMinX = min(globalMinX, x);
        globalMaxX = max(globalMaxX, x);
        globalMinY = min(globalMinY, y);
        globalMaxY = max(globalMaxY, y);
    }
}

// 2. Dimensions finales
width = globalMaxX - globalMinX + 1;
height = globalMaxY - globalMinY + 1;

// 3. Application crop
for (pixel : frame) {
    croppedX = x - globalMinX;
    croppedY = y - globalMinY;
}
```

---

## 📖 Structure projet

```
extractor_sierra/
├── build/
│   └── robot_extractor           # Programme unifié
├── src/
│   ├── main.cpp                  # robot_extractor
│   ├── core/
│   │   ├── rbt_parser.cpp        # Parser Robot
│   │   └── ressci_parser.cpp     # Parser RESSCI
│   ├── formats/
│   │   ├── robot_mkv_exporter.cpp # Export MKV/MOV
│   │   ├── lzs.cpp               # Décompression LZS
│   │   └── dpcm.cpp              # Décodage DPCM
│   └── utils/
├── docs/
│   └── reference/                # Documentation formats
├── RBT/                          # Fichiers .RBT input
├── Resource/                     # Fichiers RESSCI (coordonnées)
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
