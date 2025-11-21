# Décodage DPCM16 (Sierra SOL Audio) - Documentation Technique

**Source**: Code source ScummVM (`engines/sci/sound/decoders/sol.cpp` et `sol.h`)  
**Révision**: Basée sur l'implémentation ScummVM de référence  
**Date**: Novembre 2025  

---

## Table des Matières

1. [Vue d'ensemble](#vue-densemble)
2. [Principe du DPCM](#principe-du-dpcm)
3. [Table de différentiel DPCM16](#table-de-différentiel-dpcm16)
4. [Algorithme de décodage DPCM16](#algorithme-de-décodage-dpcm16)
5. [Gestion de l'overflow (débordement)](#gestion-de-loverflow)
6. [Décodage mono vs stéréo](#décodage-mono-vs-stéréo)
7. [DPCM8 (8-bit)](#dpcm8-8-bit)
8. [Format de fichier SOL](#format-de-fichier-sol)
9. [Implémentation de référence](#implémentation-de-référence)
10. [Notes d'optimisation](#notes-doptimisation)

---

## Vue d'ensemble

### Qu'est-ce que le DPCM16 ?

**DPCM** = **Differential Pulse Code Modulation** (Modulation par impulsion codée différentielle)

Le **DPCM16** est un algorithme de compression audio **avec perte** utilisé par Sierra dans leurs fichiers audio SOL (Sierra Online Audio) et dans les vidéos Robot (.RBT).

### Caractéristiques

- **Format** : 16-bit signé (int16)
- **Compression** : ~2:1 (chaque octet compressé → 1 échantillon 16-bit)
- **Type** : Compression différentielle (encode les différences entre échantillons)
- **Perte** : Minimale, principalement due à la quantification
- **Utilisations** :
  - Fichiers audio SOL (`.SOL`)
  - Audio dans les vidéos Robot (`.RBT`)
  - Audio dans certaines vidéos Coktelvision

### Avantages

✅ **Compression efficace** : Ratio 2:1 sans perte perceptible  
✅ **Décompression rapide** : Algorithme simple, pas de calculs complexes  
✅ **Faible latence** : Pas besoin de buffer lookahead  
✅ **Séquentiel** : Décodage byte par byte  

### Inconvénients

❌ **Pas de recherche aléatoire** : Nécessite de décoder depuis le début  
❌ **Sensible aux erreurs** : Une erreur se propage aux échantillons suivants  
❌ **Nécessite un état** : La valeur du dernier échantillon doit être conservée  

---

## Principe du DPCM

### Compression différentielle

Au lieu d'encoder la **valeur absolue** de chaque échantillon audio, le DPCM encode la **différence** (delta) avec l'échantillon précédent.

**Exemple** :

```
Échantillons originaux : [0, 100, 250, 300, 280]
Différences (deltas)   : [0, +100, +150, +50, -20]
```

### Pourquoi c'est efficace ?

Les signaux audio ont généralement une **forte corrélation temporelle** : deux échantillons consécutifs ont des valeurs similaires. Les différences sont donc **plus petites** que les valeurs absolues et peuvent être encodées avec **moins de bits**.

### Processus de compression (encodage)

1. Calculer la différence : `delta = sample[i] - sample[i-1]`
2. Quantifier le delta en utilisant la table DPCM16
3. Encoder l'index de la table + le signe dans 1 octet

### Processus de décompression (décodage)

1. Lire 1 octet compressé
2. Extraire le signe (bit 7) et l'index (bits 0-6)
3. Chercher la valeur du delta dans la table DPCM16
4. Appliquer le delta : `sample[i] = sample[i-1] + delta`
5. Gérer l'overflow 16-bit

---

## Table de différentiel DPCM16

La table DPCM16 contient **128 valeurs** représentant les deltas possibles.

### Table complète

```cpp
static const uint16 tableDPCM16[128] = {
    0x0000, 0x0008, 0x0010, 0x0020, 0x0030, 0x0040, 0x0050, 0x0060, 
    0x0070, 0x0080, 0x0090, 0x00A0, 0x00B0, 0x00C0, 0x00D0, 0x00E0, 
    0x00F0, 0x0100, 0x0110, 0x0120, 0x0130, 0x0140, 0x0150, 0x0160, 
    0x0170, 0x0180, 0x0190, 0x01A0, 0x01B0, 0x01C0, 0x01D0, 0x01E0, 
    0x01F0, 0x0200, 0x0208, 0x0210, 0x0218, 0x0220, 0x0228, 0x0230, 
    0x0238, 0x0240, 0x0248, 0x0250, 0x0258, 0x0260, 0x0268, 0x0270, 
    0x0278, 0x0280, 0x0288, 0x0290, 0x0298, 0x02A0, 0x02A8, 0x02B0, 
    0x02B8, 0x02C0, 0x02C8, 0x02D0, 0x02D8, 0x02E0, 0x02E8, 0x02F0, 
    0x02F8, 0x0300, 0x0308, 0x0310, 0x0318, 0x0320, 0x0328, 0x0330, 
    0x0338, 0x0340, 0x0348, 0x0350, 0x0358, 0x0360, 0x0368, 0x0370, 
    0x0378, 0x0380, 0x0388, 0x0390, 0x0398, 0x03A0, 0x03A8, 0x03B0, 
    0x03B8, 0x03C0, 0x03C8, 0x03D0, 0x03D8, 0x03E0, 0x03E8, 0x03F0, 
    0x03F8, 0x0400, 0x0440, 0x0480, 0x04C0, 0x0500, 0x0540, 0x0580, 
    0x05C0, 0x0600, 0x0640, 0x0680, 0x06C0, 0x0700, 0x0740, 0x0780, 
    0x07C0, 0x0800, 0x0900, 0x0A00, 0x0B00, 0x0C00, 0x0D00, 0x0E00, 
    0x0F00, 0x1000, 0x1400, 0x1800, 0x1C00, 0x2000, 0x3000, 0x4000
};
```

### Analyse de la table

La table utilise une **échelle non-linéaire** :

| Plage d'index | Incrément | Plage de valeurs | Observation                    |
|---------------|-----------|------------------|--------------------------------|
| 0-31          | 0x0008    | 0x0000-0x01F0    | Petites variations (linéaire)  |
| 32-95         | 0x0008    | 0x0200-0x03F8    | Variations moyennes            |
| 96-111        | 0x0040    | 0x0400-0x07C0    | Grandes variations             |
| 112-119       | 0x0100    | 0x0800-0x0F00    | Très grandes variations        |
| 120-127       | Variable  | 0x1000-0x4000    | Variations extrêmes            |

**Observation** : La table est conçue pour :
- **Précision fine** pour les petits mouvements (musique, parole)
- **Grandes plages** pour les variations brutales (percussions, bruits)

---

## Algorithme de décodage DPCM16

### Format de l'octet compressé

Chaque octet compressé contient :

```
Bit 7     : Signe (0 = positif, 1 = négatif)
Bits 6-0  : Index dans la table (0-127)
```

**Exemple** :
```
0x2A = 0b00101010
  → Signe = 0 (positif)
  → Index = 0x2A = 42
  → Delta = +tableDPCM16[42] = +0x0248 = +584

0xA5 = 0b10100101
  → Signe = 1 (négatif)
  → Index = 0x25 = 37
  → Delta = -tableDPCM16[37] = -0x0230 = -560
```

### Pseudo-code de décodage

```
Initialisation :
  sample = 0  (valeur initiale)

Pour chaque octet compressé :
  1. Lire delta_byte
  2. Extraire signe = (delta_byte & 0x80) ? -1 : +1
  3. Extraire index = delta_byte & 0x7F
  4. delta = tableDPCM16[index]
  5. nextSample = sample + (signe * delta)
  6. Gérer overflow 16-bit (voir section suivante)
  7. Écrire nextSample dans le buffer de sortie
  8. sample = nextSample
```

### Implémentation C++ (ScummVM)

```cpp
static void deDPCM16Channel(int16 *out, int16 &sample, uint8 delta) {
    int32 nextSample = sample;
    
    // Appliquer le delta (positif ou négatif)
    if (delta & 0x80) {
        nextSample -= tableDPCM16[delta & 0x7f];
    } else {
        nextSample += tableDPCM16[delta];
    }

    // Émulation de l'overflow du registre 16-bit x86
    if (nextSample > 32767) {
        nextSample -= 65536;
    } else if (nextSample < -32768) {
        nextSample += 65536;
    }

    *out = sample = nextSample;
}
```

### Fonction de décodage mono

```cpp
void deDPCM16Mono(int16 *out, const byte *in, const uint32 numBytes, int16 &sample) {
    for (uint32 i = 0; i < numBytes; ++i) {
        const uint8 delta = *in++;
        deDPCM16Channel(out++, sample, delta);
    }
}
```

**Paramètres** :
- `out` : Buffer de sortie (échantillons 16-bit décodés)
- `in` : Buffer d'entrée (octets compressés)
- `numBytes` : Nombre d'octets à décoder
- `sample` : Référence à la valeur du dernier échantillon (état persistant)

**Important** : Le paramètre `sample` est passé **par référence** et doit être **conservé** entre les appels pour un décodage correct.

---

## Gestion de l'overflow

### Pourquoi gérer l'overflow ?

Les échantillons 16-bit signés vont de **-32768 à +32767**. Lors de l'ajout d'un delta, le résultat peut **dépasser ces limites**.

### Méthode : Émulation du comportement x86

Le code ScummVM émule le comportement des **registres 16-bit x86** qui effectuent un **wrapping** (bouclage) automatique.

```cpp
if (nextSample > 32767) {
    nextSample -= 65536;    // Wrap vers les valeurs négatives
} else if (nextSample < -32768) {
    nextSample += 65536;    // Wrap vers les valeurs positives
}
```

**Exemples** :

```
nextSample = 32800 (> 32767)
  → nextSample = 32800 - 65536 = -32736

nextSample = -32900 (< -32768)
  → nextSample = -32900 + 65536 = 32636
```

### Alternative : Clipping (utilisé dans audio.cpp)

Une version alternative utilise le **clipping** (saturation) :

```cpp
s = CLIP<int32>(s, -32768, 32767);
```

**Différence** :
- **Wrapping** : Boucle vers l'autre extrême (plus fidèle au matériel d'origine)
- **Clipping** : Sature à la limite (évite les artefacts audio)

**Note** : Le wrapping est plus fidèle au comportement d'origine mais peut causer des "pops" audio.

---

## Décodage mono vs stéréo

### Mono

**Format** : 1 octet → 1 échantillon

```cpp
void deDPCM16Mono(int16 *out, const byte *in, const uint32 numBytes, int16 &sample) {
    for (uint32 i = 0; i < numBytes; ++i) {
        const uint8 delta = *in++;
        deDPCM16Channel(out++, sample, delta);
    }
}
```

### Stéréo

**Format** : Octets alternés pour canal gauche (L) et canal droit (R)

```
Byte 0: Canal L
Byte 1: Canal R
Byte 2: Canal L
Byte 3: Canal R
...
```

**Implémentation** :

```cpp
static void deDPCM16Stereo(int16 *out, Common::ReadStream &audioStream, 
                           const uint32 numBytes, int16 &sampleL, int16 &sampleR) {
    assert((numBytes % 2) == 0);  // Doit être pair
    
    for (uint32 i = 0; i < numBytes / 2; ++i) {
        deDPCM16Channel(out++, sampleL, audioStream.readByte());
        deDPCM16Channel(out++, sampleR, audioStream.readByte());
    }
}
```

**Points importants** :
- Chaque canal maintient **son propre état** (`sampleL`, `sampleR`)
- Le nombre d'octets **doit être pair**
- Les échantillons sont entrelacés dans le buffer de sortie : L, R, L, R, ...

---

## DPCM8 (8-bit)

Le DPCM8 est une variante 8-bit utilisée dans les anciens jeux SCI.

### Table DPCM8

```cpp
static const int8 tableDPCM8[16] = {
    0, 1, 2, 3, 6, 10, 15, 21, 
    -21, -15, -10, -6, -3, -2, -1, 0
};
```

### Différences avec DPCM16

| Caractéristique      | DPCM8                          | DPCM16                        |
|----------------------|--------------------------------|-------------------------------|
| **Taille échantillon** | 8-bit (0-255)                | 16-bit (-32768 à 32767)       |
| **Compression**      | 4:1 (1 byte → 2 samples)       | 2:1 (1 byte → 1 sample)       |
| **Format nibble**    | 2 deltas par octet (4 bits)    | 1 delta par octet (8 bits)    |
| **Table**            | 16 entrées (4-bit index)       | 128 entrées (7-bit index)     |
| **Qualité**          | Moyenne                        | Élevée                        |

### Décodage DPCM8

```cpp
template <bool OLD>
static void deDPCM8Nibble(int16 *out, uint8 &sample, uint8 delta) {
    const uint8 lastSample = sample;
    
    if (delta & 8) {
        sample -= tableDPCM8[OLD ? (7 - (delta & 7)) : (delta & 7)];
    } else {
        sample += tableDPCM8[delta & 7];
    }
    
    // Conversion 8-bit → 16-bit avec interpolation
    *out = ((lastSample + sample) << 7) ^ 0x8000;
}
```

**Note** : Chaque octet contient **2 nibbles** (4 bits) :
- **Nibble haut** (bits 4-7) : Premier échantillon
- **Nibble bas** (bits 0-3) : Deuxième échantillon

### Réparation d'overflow DPCM8

Le DPCM8 peut souffrir d'overflows dans certains jeux (ex: Gabriel Knight). ScummVM implémente une **réparation automatique** :

```cpp
static void deDPCM8NibbleWithRepair(int16 *const out, uint8 &sample, const uint8 delta,
                                    uint8 &repairState, uint8 &preRepairSample) {
    // Détection d'overflow
    const int16 newSampleOverflow = (int16)sample + tableDPCM8[delta & 15];
    
    if (newSampleOverflow > 255) {
        // Overflow positif → commencer une pente négative artificielle
        repairState = 1;
        sample = lastSample - REPAIR_SLOPE;
    } else if (newSampleOverflow < 0) {
        // Overflow négatif → commencer une pente positive artificielle
        repairState = 2;
        sample = lastSample + REPAIR_SLOPE;
    }
    // ... (détection de la fin de réparation)
}
```

**Principe** : Lors d'un overflow, au lieu de wrapper, créer une **pente artificielle** pour ramener progressivement le signal vers une valeur valide.

---

## Format de fichier SOL

### En-tête SOL

Les fichiers audio Sierra (`.SOL`) contiennent un en-tête décrivant le format audio.

#### Structure de l'en-tête

```
Offset | Taille | Description
-------|--------|------------------------------------------
0-3    | 4      | Signature 'SOL\0' (MKTAG('S','O','L',0))
4-5    | 2      | Sample rate (Hz) - Little Endian
6      | 1      | Flags (compression, bit depth, stereo)
7-10   | 4      | Data size (taille des données audio)
```

**Tailles d'en-tête** : 7, 11 ou 12 octets selon la version.

#### Flags SOL

```cpp
enum SOLFlags {
    kCompressed = 1,     // 0x01 = Audio compressé DPCM
    k16Bit      = 4,     // 0x04 = 16-bit (sinon 8-bit)
    kStereo     = 16     // 0x10 = Stéréo (sinon mono)
};
```

**Exemples** :

```
flags = 0x05 (0b00000101)
  → kCompressed | k16Bit
  → DPCM16 mono compressé

flags = 0x15 (0b00010101)
  → kCompressed | k16Bit | kStereo
  → DPCM16 stéréo compressé

flags = 0x04 (0b00000100)
  → k16Bit (pas kCompressed)
  → PCM 16-bit non compressé
```

### Lecture d'un fichier SOL

```cpp
Audio::SeekableAudioStream *makeSOLStream(Common::SeekableReadStream *stream, 
                                          DisposeAfterUse::Flag disposeAfterUse) {
    // Lire l'en-tête
    byte header[6];
    stream->read(header, sizeof(header));
    
    // Vérifier la signature
    if (READ_BE_UINT32(header + 2) != MKTAG('S', 'O', 'L', 0)) {
        return nullptr;
    }
    
    // Lire les métadonnées
    const uint16 sampleRate = stream->readUint16LE();
    const byte flags = stream->readByte();
    const uint32 dataSize = stream->readUint32LE();
    
    // Créer le stream approprié selon les flags
    if (flags & kCompressed) {
        if (flags & kStereo && flags & k16Bit) {
            return new SOLStream<true, true, false>(stream, sampleRate, dataSize);
        } else if (flags & k16Bit) {
            return new SOLStream<false, true, false>(stream, sampleRate, dataSize);
        }
        // ... autres variantes
    }
}
```

---

## Implémentation de référence

### Classe SOLStream (Template)

ScummVM utilise une classe template pour gérer toutes les variantes :

```cpp
template <bool STEREO, bool S16BIT, bool OLDDPCM8>
class SOLStream : public Audio::SeekableAudioStream {
private:
    Common::DisposablePtr<Common::SeekableReadStream> _stream;
    uint16 _sampleRate;
    int32 _rawDataSize;
    
    // État DPCM (valeur du dernier échantillon)
    union {
        struct { int16 l; int16 r; } _dpcmCarry16;
        struct { uint8 l; uint8 r; } _dpcmCarry8;
    };
    
public:
    int readBuffer(int16 *buffer, const int numSamples) override {
        if (S16BIT) {
            if (STEREO) {
                deDPCM16Stereo(buffer, *_stream, bytesToRead, 
                               _dpcmCarry16.l, _dpcmCarry16.r);
            } else {
                deDPCM16Mono(buffer, *_stream, bytesToRead, 
                             _dpcmCarry16.l);
            }
        } else {
            // DPCM8 ...
        }
    }
};
```

**Paramètres template** :
- `STEREO` : true = stéréo, false = mono
- `S16BIT` : true = 16-bit, false = 8-bit
- `OLDDPCM8` : true = ancien format DPCM8 (SCI < 2.1)

### État persistant (Carry)

L'état du décodeur DPCM doit être **conservé entre les appels** à `readBuffer()` :

```cpp
union {
    struct { int16 l; int16 r; } _dpcmCarry16;  // Pour DPCM16
    struct { uint8 l; uint8 r; } _dpcmCarry8;   // Pour DPCM8
};
```

**Initialisation** :
- **DPCM16** : `_dpcmCarry16.l = _dpcmCarry16.r = 0;`
- **DPCM8** : `_dpcmCarry8.l = _dpcmCarry8.r = 0x80;` (point milieu)

### Limitation : Pas de seek

```cpp
bool SOLStream::seek(const Audio::Timestamp &where) {
    if (where != 0) {
        // Le DPCM est différentiel : tous les octets précédents
        // doivent être décodés pour obtenir la valeur correcte.
        // Seul le retour au début (0) est supporté.
        return false;
    }
    
    // Réinitialiser l'état
    _dpcmCarry16.l = _dpcmCarry16.r = 0;
    return _stream->seek(0, SEEK_SET);
}
```

---

## Notes d'optimisation

### 1. Table lookup

La table DPCM16 est une **simple lookup** : aucun calcul complexe nécessaire.

```cpp
const uint16 delta = tableDPCM16[index];  // O(1)
```

### 2. Opérations bit à bit

Extraction efficace du signe et de l'index :

```cpp
const bool negative = (delta & 0x80) != 0;  // Bit 7
const uint8 index = delta & 0x7F;           // Bits 0-6
```

### 3. Calcul en 32-bit

Utiliser `int32` pour le calcul intermédiaire évite les overflows :

```cpp
int32 nextSample = sample;  // Promotion 16→32 bit
nextSample += tableDPCM16[delta];  // Calcul sûr
// Puis wrapping 16-bit
```

### 4. Inlining

Les fonctions de décodage devraient être **inline** pour éviter l'overhead d'appel :

```cpp
inline void deDPCM16Channel(int16 *out, int16 &sample, uint8 delta) {
    // ...
}
```

### 5. Vectorisation (SIMD)

Pour des performances maximales, le décodage peut être vectorisé :
- **SSE2** : Traiter 8 échantillons simultanément
- **AVX2** : Traiter 16 échantillons simultanément

**Défi** : La nature **séquentielle** du DPCM (chaque échantillon dépend du précédent) limite la vectorisation.

### 6. Cache-friendly

Accès séquentiel aux données :
- ✅ Lecture linéaire du buffer d'entrée
- ✅ Écriture linéaire du buffer de sortie
- ✅ Petite table (128 × 2 octets = 256 octets) → cache-friendly

---

## Exemples d'utilisation

### Exemple 1 : Décodage simple

```cpp
#include "decoders/sol.h"

// Données compressées
const byte compressedData[] = {0x2A, 0x15, 0x8C, 0x3F, ...};
const uint32 numBytes = sizeof(compressedData);

// Buffer de sortie
int16 outputBuffer[numBytes];

// État du décodeur
int16 sample = 0;

// Décodage
deDPCM16Mono(outputBuffer, compressedData, numBytes, sample);

// outputBuffer contient maintenant les échantillons décodés
```

### Exemple 2 : Lecture d'un fichier SOL

```cpp
#include "audio/audiostream.h"
#include "decoders/sol.h"

// Ouvrir le fichier
Common::File *file = new Common::File();
file->open("speech.sol");

// Créer le stream audio
Audio::SeekableAudioStream *audioStream = makeSOLStream(file, DisposeAfterUse::YES);

if (audioStream) {
    // Utiliser le stream (lecture, playback, etc.)
    g_system->getMixer()->playStream(Audio::Mixer::kSpeechSoundType, 
                                     &handle, audioStream);
}
```

### Exemple 3 : Décodage par blocs (streaming)

```cpp
int16 sample = 0;  // État persistant

while (!stream.eos()) {
    byte block[1024];
    int bytesRead = stream.read(block, sizeof(block));
    
    int16 outputBlock[1024];
    deDPCM16Mono(outputBlock, block, bytesRead, sample);
    
    // Traiter outputBlock...
    audioQueue.add(outputBlock, bytesRead);
}
```

**Important** : La variable `sample` doit être **réutilisée** entre les blocs pour un décodage correct.

---

## Références

### Code source ScummVM

- **Fichier principal** : `engines/sci/sound/decoders/sol.cpp`
- **Header** : `engines/sci/sound/decoders/sol.h`
- **Audio générique** : `engines/sci/sound/audio.cpp`
- **Robot decoder** : `engines/sci/video/robot_decoder.cpp`

### Documentation externe

- **Wiki Multimedia** : https://wiki.multimedia.cx/index.php?title=Sierra_Audio
- **ScummVM Wiki** : https://wiki.scummvm.org/index.php/SCI
- **SCI Wiki** : http://sciwiki.sierrahelp.com/

### Jeux utilisant DPCM16

- **King's Quest 7**
- **Phantasmagoria**
- **Gabriel Knight: Sins of the Fathers** (DPCM8)
- **Police Quest: SWAT**
- **Lighthouse**
- **RAMA**
- Tous les jeux SCI2.1 et SCI3

---

## Annexe : Comparaison avec d'autres codecs

| Codec       | Ratio    | Qualité | Complexité | Seek    | Usage                |
|-------------|----------|---------|------------|---------|----------------------|
| **DPCM16**  | 2:1      | Haute   | Très faible| Non     | Jeux Sierra SCI      |
| **ADPCM**   | 4:1      | Moyenne | Faible     | Limité  | Jeux, téléphonie     |
| **MP3**     | 10:1+    | Variable| Haute      | Oui     | Musique, streaming   |
| **FLAC**    | ~2:1     | Parfaite| Haute      | Oui     | Archivage audio      |
| **PCM**     | 1:1      | Parfaite| Nulle      | Oui     | Audio non compressé  |

**Avantages du DPCM16 pour le jeu vidéo rétro** :
- ✅ Décodage extrêmement rapide (important sur matériel limité)
- ✅ Faible latence (pas de buffer requis)
- ✅ Qualité suffisante pour la parole et la musique de jeu
- ✅ Implémentation simple (petit code)

---

**Auteur** : Documentation générée à partir du code source ScummVM  
**Contributeurs** : Équipe ScummVM  
**Licence** : GPL v3+  
**Date** : Novembre 2025  
**Version** : 1.0
