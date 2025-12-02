# Documentation Technique - Extractor Sierra

## Architecture Audio Robot

### Format DPCM16 Entrelaçé

Le format audio Robot utilise une architecture unique à deux canaux entrelacés :

```
Buffer final (22050 Hz mono) :
┌────┬────┬────┬────┬────┬────┬────┬────┬────┐
│ E0 │ O0 │ E1 │ O1 │ E2 │ O2 │ E3 │ O3 │ ...│
└────┴────┴────┴────┴────┴────┴────┴────┴────┘
  ↑    ↑    ↑    ↑
  |    |    |    └─ Odd sample 1 (11025 Hz)
  |    |    └────── Even sample 1 (11025 Hz)
  |    └─────────── Odd sample 0 (11025 Hz)
  └──────────────── Even sample 0 (11025 Hz)
```

**Caractéristiques** :
- Fréquence finale : 22050 Hz mono
- Canal EVEN : 11025 Hz (positions paires : 0, 2, 4, 6...)
- Canal ODD : 11025 Hz (positions impaires : 1, 3, 5, 7...)
- Résultat : Entrelacement des deux canaux → flux mono continu

### Structure des Paquets Audio

Chaque frame vidéo contient un paquet audio avec la structure suivante :

```
┌─────────────────────────────────────────┐
│ En-tête Audio (8 bytes)                 │
├─────────────────────────────────────────┤
│ audioAbsolutePosition (int32, 4 bytes)  │  ← Position dans buffer final
│ audioBlockSize        (int32, 4 bytes)  │  ← Taille données compressées
├─────────────────────────────────────────┤
│ Runway (8 samples, 8 bytes)             │  ← Non écrits dans sortie
├─────────────────────────────────────────┤
│ Données DPCM16 compressées              │
│ (audioBlockSize - 8 bytes)              │
└─────────────────────────────────────────┘
```

### audioAbsolutePosition - Explication Critique

**Erreur commune** : Interpréter `audioAbsolutePosition` comme une position dans le canal (EVEN ou ODD).

**Réalité** : `audioAbsolutePosition` est **DÉJÀ** la position dans le buffer final entrelaçé !

**Exemple concret (fichier 1014.RBT)** :

| Frame | Canal | audioAbsolutePosition | Signification |
|-------|-------|-----------------------|---------------|
| F000  | EVEN  | 39844                 | Position paire dans buffer (après primers) |
| F001  | ODD   | 42049                 | Position impaire dans buffer |
| F002  | EVEN  | 44254                 | +4410 depuis F000 (2205 samples + gap) |
| F003  | ODD   | 46459                 | +4410 depuis F001 (2205 samples + gap) |

**Calcul de position** :

```cpp
// ❌ INCORRECT (ancien code, doublait la position)
size_t bufferPos = (audioAbsolutePosition * 2) + channelOffset;

// ✅ CORRECT (code actuel)
size_t bufferPos = audioAbsolutePosition + (sampleIndex * 2);
```

**Explication** :
- `audioAbsolutePosition` = index de départ déjà dans le buffer entrelaçé
- `sampleIndex * 2` = offset relatif (multiplication car entrelacé)
- Résultat : Position exacte pour chaque sample

### Primers Audio

Les primers initialisent les buffers audio avant la lecture des frames :

```
Primers :
├── EVEN : 19922 samples → écrits aux positions 0, 2, 4, 6... (stride 2)
└── ODD  : 21024 samples → écrits aux positions 1, 3, 5, 7... (stride 2)

Total : 40946 samples (primers) + N frames audio
```

**Positions initiales** :
- Premier paquet EVEN : `audioAbsolutePosition = 39844` (≈ 19922 * 2)
- Premier paquet ODD : `audioAbsolutePosition = 42049` (≈ 21024 * 2 + 1)

### Interpolation des Canaux

L'interpolation est **essentielle** pour créer des transitions douces entre EVEN et ODD :

```cpp
static void interpolateChannel(int16_t *buffer, int32_t numSamples, 
                                const int8_t bufferIndex) {
    // bufferIndex = 0 → Canal EVEN (positions paires)
    // bufferIndex = 1 → Canal ODD (positions impaires)
    
    // Interpolation linéaire : moyenne des échantillons voisins
    sample = (*inBuffer + previousSample) >> 1;
}
```

**Pourquoi l'interpolation est nécessaire** :
1. Les deux canaux sont enregistrés indépendamment
2. Il peut y avoir des micro-décalages entre EVEN et ODD
3. L'interpolation lisse ces transitions pour un flux continu
4. **Important** : Elle ne crée PAS d'audio là où il n'y en a pas, elle lisse seulement

### Gaps et Synchronisation

Les "gaps" visibles dans les logs ne sont **pas** des pauses audio :

```
F002 EVEN: pos=44254 delta=4410 exp=2205 gap=2205
```

**Analyse** :
- `delta = 4410` = distance depuis le dernier paquet EVEN (F000)
- `exp = 2205` = nombre de samples écrits (audioBlockSize - 8)
- `gap = 2205` = delta - exp = **alternance normale EVEN↔ODD**

**Explication** :
- Frame F000 (EVEN) écrit 2205 samples
- Frame F001 (ODD) écrit 2205 samples (non compté dans delta EVEN)
- Frame F002 (EVEN) commence 4410 positions plus loin
- C'est **normal** car ODD a pris 2205 positions intermédiaires

### Runway - 8 Samples Magiques

Les 8 premiers samples de chaque paquet ne sont **jamais** écrits dans la sortie :

**Raison technique** :
- Permet au décodeur DPCM16 d'atteindre la valeur correcte
- "Runway" = piste de décollage pour stabiliser le signal
- Au 9ème sample, le signal est à la bonne position

**Code** :
```cpp
const size_t kRunwaySamples = 8;

for (size_t s = kRunwaySamples; s < decompressedSamples.size(); ++s) {
    size_t bufferPos = audioAbsolutePosition + (s - kRunwaySamples) * 2;
    audioBuffer[bufferPos] = decompressedSamples[s];
}
```

### Processus Complet d'Extraction

```
1. Lecture du fichier RBT
   └── Parsing de l'en-tête (version, frames, audioBlockSize)

2. Extraction des Primers
   ├── EVEN : Décompression DPCM16 → 19922 samples
   │   └── Écriture aux positions 0, 2, 4, 6... (stride 2)
   └── ODD : Décompression DPCM16 → 21024 samples
       └── Écriture aux positions 1, 3, 5, 7... (stride 2)

3. Pour chaque frame :
   ├── Lecture en-tête audio (audioAbsolutePosition, audioBlockSize)
   ├── Lecture données compressées (audioBlockSize bytes)
   ├── Décompression DPCM16 (audioBlockSize samples)
   ├── Skip runway (8 premiers samples)
   └── Écriture à audioAbsolutePosition + offset*2

4. Interpolation des canaux
   ├── interpolateChannel(buffer, numSamples/2, 0)  // EVEN
   └── interpolateChannel(buffer, numSamples/2, 1)  // ODD

5. Écriture WAV
   └── Header + samples (22050 Hz mono)
```

### Historique des Corrections

#### v2.2.0 - Correction Synchronisation Audio

**Problème initial** : Distorsion audio (son ralenti/déformé)

**Cause** : Mauvaise interprétation de `audioAbsolutePosition`
- Code incorrect : `bufferPos = (audioAbsolutePosition * 2) + offset`
- Effet : Position doublée → samples écrits trop loin
- Résultat : Grandes zones de silence remplies par interpolation

**Solution** : Utilisation directe de `audioAbsolutePosition`
- Code correct : `bufferPos = audioAbsolutePosition + (sampleIndex * 2)`
- Effet : Position exacte respectée
- Résultat : Synchronisation parfaite

**Tests** :
- Fichier : 1014.RBT
- Durée : 25.8s audio = 25.8s vidéo
- Samples : 568,890 @ 22050 Hz
- Frames : 258 @ 10 fps
- Résultat : ✅ Audio synchronisé sans distorsion

### Références ScummVM

Le code est basé sur l'implémentation ScummVM :
- `engines/sci/graphics/robot.cpp` : Décodeur principal
- `_firstAudioRecordPosition = _evenPrimerSize * 2` (ligne 427)
- Position paquet : `(_position - startOffset) * 2` (ligne 984)

**Différence clé** :
- ScummVM utilise un buffer circulaire avec positions relatives
- Notre implémentation utilise `audioAbsolutePosition` directement
- Résultat identique mais approches différentes

## Compression LZS

### Algorithme

Le format Robot utilise LZS (Lempel-Ziv-Storer) avec paramètres spécifiques :

**Paramètres** :
- Window size : 4096 bytes
- Match length : 3-18 bytes
- Offset encoding : 12 bits
- Length encoding : 4 bits

**Token format** :
```
┌──────────────┬─────────┐
│ Offset (12b) │ Len (4b)│
└──────────────┴─────────┘
     0-4095       3-18
```

### Décompression

```cpp
while (outPos < uncompressedSize) {
    uint8_t controlByte = compressedData[inPos++];
    
    for (int i = 0; i < 8 && outPos < uncompressedSize; ++i) {
        if (controlByte & (1 << i)) {
            // Token : copy from window
            uint16_t token = READ_LE_UINT16(compressedData + inPos);
            inPos += 2;
            
            uint16_t offset = (token >> 4) & 0x0FFF;
            uint8_t length = (token & 0x0F) + 3;
            
            // Copy from sliding window
            for (uint8_t j = 0; j < length; ++j) {
                outBuffer[outPos] = outBuffer[outPos - offset];
                outPos++;
            }
        } else {
            // Literal byte
            outBuffer[outPos++] = compressedData[inPos++];
        }
    }
}
```

## Format Matroska Multi-Pistes

### Structure

```
output/1014/
├── 1014_video.mkv          # MKV avec 4 pistes vidéo + 1 piste audio
├── 1014_audio.wav          # Audio original 22050 Hz
├── 1014_composite.mp4      # Vidéo composite pour lecture standard
├── 1014_metadata.txt       # Métadonnées complètes
└── 1014_frames/            # Frames PNG individuelles
    ├── frame_0000.png
    └── ...
```

### Pistes MKV

| Piste | Type | Codec | Résolution | Description |
|-------|------|-------|------------|-------------|
| 0 | Video | H.264/H.265/VP9 | 320x240 | BASE (pixels 0-235) |
| 1 | Video | H.264/H.265/VP9 | 320x240 | REMAP (pixels 236-254) |
| 2 | Video | H.264/H.265/VP9 | 320x240 | ALPHA (transparence) |
| 3 | Video | H.264/H.265/VP9 | 320x240 | LUMINANCE (grayscale) |
| 4 | Audio | PCM | - | 48 kHz mono |

### Lecture du MKV

VLC, mpv, FFplay peuvent tous lire les pistes multiples :

```bash
# Voir toutes les pistes
ffprobe output/1014/1014_video.mkv

# Lire piste spécifique dans VLC
# VLC > Audio/Video > Video Track > Sélectionner la piste
```

## Performances

### Benchmarks (fichier 1014.RBT)

- **Frames** : 258 @ 10 fps
- **Résolution** : 320x240
- **Durée** : 25.8 secondes

**Temps d'extraction** (Intel i7-10700K) :
- Parsing : ~50ms
- Décompression vidéo : ~800ms
- Décompression audio : ~100ms
- Export PNG : ~2.5s
- Encodage MKV H.264 : ~3.5s
- **Total** : ~7 secondes

**Optimisations possibles** :
- Parallélisation de l'export PNG (thread pool)
- Cache des palettes (éviter recomputation)
- Encodage vidéo GPU (NVENC, QuickSync)
