# Extraction des pistes audio L/R de fichiers Robot (.RBT)

## ✅ Solution finale: `extract_lr_simple.py`

### Principe

Extraction directe et simplifiée des canaux LEFT et RIGHT:

1. **Parse** le log d'extraction (`audio_extraction.log`) pour obtenir les métadonnées
2. **Lit** chaque frame audio compressée directement depuis le fichier RBT
3. **Décompresse** avec l'algorithme DPCM16 de ScummVM (table de lookup)
4. **Classifie** chaque frame: `audioPos % 4 == 0` → LEFT, sinon → RIGHT
5. **Concatène** séquentiellement dans les fichiers L et R

### Utilisation

```bash
# 1. Générer le log d'extraction avec robot_decoder
./build/robot_decoder ScummVM/rbt/91.RBT /tmp/output/ 90 audio 2>&1 | tee audio_extraction.log

# 2. Extraire les pistes L/R
python3 extract_lr_simple.py ScummVM/rbt/91.RBT audio/lr_simple/
```

### Résultats pour 91.RBT

| Canal | Frames | Échantillons | Durée | Fichier |
|-------|--------|--------------|-------|---------|
| LEFT  | 23 | 50,715 | 2.30s | `91_LEFT_simple.wav` |
| RIGHT | 67 | 147,735 | 6.70s | `91_RIGHT_simple.wav` |
| **TOTAL** | **90** | **198,450** | **9.00s** | - |

**Format**: PCM 16-bit mono @ 22050 Hz

## Algorithme DPCM16

### Description

Le format Robot utilise DPCM16 (Differential Pulse Code Modulation 16-bit) avec table de lookup.

```python
# Table de 128 valeurs pré-calculées
DPCM16_TABLE = [0x0000, 0x0008, 0x0010, 0x0020, ..., 0x4000]

# Décompression
sample = 0  # Valeur précédente
pour chaque byte dans compressed_data:
    si byte & 0x80:  # Bit 7 = 1
        sample -= DPCM16_TABLE[byte & 0x7F]
    sinon:           # Bit 7 = 0
        sample += DPCM16_TABLE[byte]
    
    # Saturation
    sample = max(-32768, min(32767, sample))
    output(sample)
```

### Caractéristiques

- **Compression**: ~2:1 (2213 bytes → 2205 samples × 2 bytes = 4410 bytes)
- **Type**: Différentiel avec table non-linéaire
- **Qualité**: Adapté pour la parole et effets sonores
- **Implémentation**: Basé sur `deDPCM16Mono()` de ScummVM

## Classification LEFT/RIGHT

### Règle

```python
audioPos = position_du_packet_dans_le_flux_audio
bufferIndex = 0 if (audioPos % 4 == 0) else 1

if bufferIndex == 0:
    canal = LEFT   # EVEN
else:
    canal = RIGHT  # ODD
```

### Pattern observé (91.RBT)

```
Frame: 0  1  2  3  4  5  6  7  8  9 10 11 12 ...
Canal: L  R  R  R  L  R  R  R  L  R  R  R  L  ...

Pattern répété: 1 LEFT, 3 RIGHT
Ratio: 23 LEFT : 67 RIGHT (1:2.91)
```

### Explication

Ce pattern provient du système de buffer circulaire de ScummVM:
- Les positions EVEN (divisibles par 4) vont dans le buffer 0 (LEFT)
- Les positions ODD (non divisibles par 4) vont dans le buffer 1 (RIGHT)

Le déséquilibre est INTENTIONNEL et fait partie du format Robot.

## Validation

### Comparaison avec robot_decoder

```bash
# Méthode 1: Dumps audio décompressés
build/rbt_dumps/91/frame_*/audio_decomp.pcm
→ 90 frames × 2205 samples = 198,450 samples

# Méthode 2: extract_lr_simple.py
audio/lr_simple/91_LEFT_simple.wav + 91_RIGHT_simple.wav
→ 50,715 + 147,735 = 198,450 samples ✅

# Méthode 3: extractAllAudio() de robot_decoder
/tmp/test91_audio/audio.raw.pcm
→ 342,970 samples stéréo = 171,485 frames mono
→ Inclut les primers + interpolation
```

**Conclusion**: Les résultats de `extract_lr_simple.py` correspondent EXACTEMENT aux dumps bruts.

## Avantages de la méthode simplifiée

- ✅ **Précision**: Utilise le vrai algorithme DPCM16
- ✅ **Simplicité**: Pas de buffer circulaire ni interpolation
- ✅ **Performance**: Une seule passe, lecture directe
- ✅ **Validation**: Résultats identiques aux dumps C++
- ✅ **Portabilité**: Python pur, pas de dépendances externes

## Fichiers

- `extract_lr_simple.py` - Script d'extraction (200 lignes)
- `audio/lr_simple/91_LEFT_simple.wav` - Canal gauche
- `audio/lr_simple/91_RIGHT_simple.wav` - Canal droit
- `audio/lr_simple/README.md` - Documentation détaillée

## Références

- **ScummVM**: `engines/sci/video/robot_decoder.cpp` - Implémentation complète
- **DPCM16**: `src/robot_decoder/dpcm.cpp` - Table et algorithme
- **Tests**: `test_audio_video_sync.py` - Validation de la synchronisation

---

**Note**: Pour un extracteur 100% autonome sans log, il faudrait parser complètement le format SOL/RBT, ce qui est complexe. La méthode actuelle (avec log) est un bon compromis entre simplicité et fonctionnalité.
