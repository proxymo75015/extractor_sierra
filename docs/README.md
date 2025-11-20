# 📖 Documentation extractor_sierra

Index de la documentation complète du projet.

---

## 🗂️ Structure

```
docs/
├── README.md                  # ← Vous êtes ici
├── reference/                 # 📚 Référence ScummVM (implémentation originale)
│   ├── SCUMMVM_ROBOT_FORMAT.md
│   └── SCUMMVM_AUDIO_IMPLEMENTATION.md
├── project/                   # 🔧 Notre implémentation
│   └── OUR_IMPLEMENTATION.md
└── technical/                 # ⚙️ Détails techniques
    ├── AUDIO_ENCODING.md
    ├── AUDIO_EXTRACTION_LR.md
    └── DESIGN_NOTES.md
```

---

## 🎯 Par où commencer ?

### Nouveau sur le projet ?

1. **[README principal](../README.md)** - Vue d'ensemble et quick start
2. **[Notre implémentation](project/OUR_IMPLEMENTATION.md)** - Architecture et usage
3. **[Format Robot](reference/SCUMMVM_ROBOT_FORMAT.md)** - Comprendre le format

### Développeur ?

1. **[Architecture C++](../src/robot_decoder/README.md)** - Code source
2. **[Scripts Python](../tools/README.md)** - Utilitaires
3. **[Notes techniques](technical/DESIGN_NOTES.md)** - Décisions de design

### Chercheur / Analyste ?

1. **[Format Robot ScummVM](reference/SCUMMVM_ROBOT_FORMAT.md)** - Spécification complète
2. **[Implémentation audio ScummVM](reference/SCUMMVM_AUDIO_IMPLEMENTATION.md)** - Détails algorithmes
3. **[Encodage audio](technical/AUDIO_ENCODING.md)** - Comparaison implémentations

---

## 📚 Référence ScummVM

Documentation de l'implémentation originale (source de référence).

### [SCUMMVM_ROBOT_FORMAT.md](reference/SCUMMVM_ROBOT_FORMAT.md)

**Format de fichier Robot v5/v6**

- Structure du header (v5 vs v6)
- Format vidéo (LZS, palette)
- Format audio (DPCM16, EVEN/ODD)
- Primers et packets
- Index de frames

### [SCUMMVM_AUDIO_IMPLEMENTATION.md](reference/SCUMMVM_AUDIO_IMPLEMENTATION.md)

**Implémentation audio de ScummVM**

- Architecture `RobotAudioStream`
- Buffer circulaire (88200 bytes)
- Décompression DPCM16
- Classification EVEN/ODD (`audioPos % 4`)
- Interpolation et entrelacement
- Gestion du runway (8 bytes)

---

## 🔧 Notre projet

Documentation de notre implémentation et différences avec ScummVM.

### [OUR_IMPLEMENTATION.md](project/OUR_IMPLEMENTATION.md)

**Notre implémentation détaillée**

- Architecture C++ (robot_decoder)
- Scripts Python (tools/)
- Pipeline de traitement
- Différences vs ScummVM
- Installation et usage
- Tests et validation

---

## ⚙️ Détails techniques

Analyses approfondies et notes de design.

### [AUDIO_ENCODING.md](technical/AUDIO_ENCODING.md)

**Encodage audio : ScummVM vs Notre projet**

- Comparaison des deux approches
- Table DPCM16 (128 valeurs)
- Wrapping vs Clamping
- Buffer circulaire vs linéaire
- Interpolation multi-pass
- Gestion des primers
- Métriques de qualité

### [AUDIO_EXTRACTION_LR.md](technical/AUDIO_EXTRACTION_LR.md)

**Extraction canaux LEFT/RIGHT**

- Classification EVEN/ODD
- Runway DPCM (8 bytes)
- Script `extract_lr_simple.py`
- Format de sortie WAV
- Validation et tests

### [DESIGN_NOTES.md](technical/DESIGN_NOTES.md)

**Notes de conception et décisions**

- Choix d'architecture
- Problèmes rencontrés
- Solutions apportées
- Optimisations

---

## 🔍 Index par sujet

### Audio

| Sujet | Document | 
|-------|----------|
| **DPCM16 décompression** | [SCUMMVM_AUDIO_IMPLEMENTATION.md](reference/SCUMMVM_AUDIO_IMPLEMENTATION.md) |
| **Classification EVEN/ODD** | [AUDIO_EXTRACTION_LR.md](technical/AUDIO_EXTRACTION_LR.md) |
| **Runway (8 bytes)** | [AUDIO_EXTRACTION_LR.md](technical/AUDIO_EXTRACTION_LR.md) |
| **Primers** | [SCUMMVM_AUDIO_IMPLEMENTATION.md](reference/SCUMMVM_AUDIO_IMPLEMENTATION.md) |
| **Buffer circulaire** | [SCUMMVM_AUDIO_IMPLEMENTATION.md](reference/SCUMMVM_AUDIO_IMPLEMENTATION.md) |
| **Interpolation** | [AUDIO_ENCODING.md](technical/AUDIO_ENCODING.md) |
| **Wrapping vs Clamping** | [AUDIO_ENCODING.md](technical/AUDIO_ENCODING.md) |

### Format

| Sujet | Document |
|-------|----------|
| **Structure RBT** | [SCUMMVM_ROBOT_FORMAT.md](reference/SCUMMVM_ROBOT_FORMAT.md) |
| **Vidéo LZS** | [SCUMMVM_ROBOT_FORMAT.md](reference/SCUMMVM_ROBOT_FORMAT.md) |
| **Audio DPCM16** | [SCUMMVM_ROBOT_FORMAT.md](reference/SCUMMVM_ROBOT_FORMAT.md) |
| **Palette** | [SCUMMVM_ROBOT_FORMAT.md](reference/SCUMMVM_ROBOT_FORMAT.md) |

### Implémentation

| Sujet | Document |
|-------|----------|
| **Architecture C++** | [OUR_IMPLEMENTATION.md](project/OUR_IMPLEMENTATION.md) |
| **Scripts Python** | [../tools/README.md](../tools/README.md) |
| **Pipeline extraction** | [OUR_IMPLEMENTATION.md](project/OUR_IMPLEMENTATION.md) |
| **Tests** | [OUR_IMPLEMENTATION.md](project/OUR_IMPLEMENTATION.md) |

---

<div align="center">

**[⬅ Retour au README principal](../README.md)**

</div>
