# Format Robot v5/v6 - Référence ScummVM

> Documentation extraite de ScummVM `engines/sci/video/robot_decoder.h`  
> Source : https://github.com/scummvm/scummvm/blob/master/engines/sci/video/robot_decoder.h

## Vue d'ensemble

Le format Robot est utilisé par Sierra SCI pour les vidéos haute résolution (320×240 ou 640×480) avec audio DPCM16 compressé.

## Caractéristiques

- **Résolutions** : 320×240 (Robot v5), 640×480 (Robot v6)
- **Compression vidéo** : LZS (Lempel-Ziv-Storer)
- **Compression audio** : Sierra SOL DPCM16
- **Palette** : HunkPalette indexée (256 couleurs)
- **Framerate** : Variable (généralement 10-15 fps)
- **Audio** : MONO 22050 Hz (deux canaux 11025 Hz entrelacés)

## Structure du fichier

### Header (v5/v6)

```
Offset | Size | Description
-------|------|-------------
0x00   | 4    | Signature "RBT\0" (v5) ou version number (v6)
0x04   | 2    | Version (v5: 5, v6: 6)
0x06   | 2    | Width en pixels
0x08   | 2    | Height en pixels
0x0A   | 4    | Frame count total
0x0E   | 2    | Frames per second (si 0, utiliser defaut: 10-15)
0x10   | 1    | Has palette (1=oui, 0=non)
0x11   | 1    | Has audio (1=oui, 0=non)
0x12   | 2    | Palette size (bytes)
0x14   | 4    | Total primer size (audio initialization)
0x18   | 4    | Even primer size
0x1C   | 4    | Odd primer size
0x20   | ... | Palette data (si has palette)
       | ... | Audio primers (si has audio)
       | ... | Frame index table
```

### Audio

#### Codage DPCM16

L'audio est encodé avec **Sierra SOL DPCM16** (Differential Pulse Code Modulation 16-bit) :

- Format **MONO 22050 Hz** composé de deux canaux à 11025 Hz
- Canal **EVEN** : échantillons aux positions 0, 2, 4, 6... (divisibles par 4)
- Canal **ODD** : échantillons aux positions 2, 6, 10, 14... (divisibles par 2, mais pas par 4)
- Signal restauré en **entrelacant** les échantillons des deux canaux

#### Classification des canaux

```cpp
const int8 bufferIndex = packet.position % 4 ? 1 : 0;
```

- `position % 4 == 0` → buffer 0 (EVEN)
- `position % 4 != 0` → buffer 1 (ODD)

**Note** : Le commentaire ScummVM dit "divisible by 2 (even)" mais c'est **imprécis**. La formule correcte est `% 4`.

#### Runway DPCM

Chaque bloc audio contient un **runway de 8 bytes** au début :

- **Objectif** : Initialiser le décodeur DPCM pour atteindre la bonne amplitude au 9ème sample
- **Primers** : Le runway est **INCLUS** et utilisé (19922 et 21024 samples incluant runway)
- **Packets réguliers** : Le runway est décompressé mais **EXCLU** du placement final
  - Taille compressée : 2213 bytes
  - Décompressé : 2213 samples
  - `audioPos` avance de **2205** → les 8 premiers samples sont automatiquement exclus

#### Primers (Audio initialization)

Les primers sont des blocs audio spéciaux pour initialiser le flux :

- **Even primer** : Position 0, taille 19922 bytes (si compressed flag set)
- **Odd primer** : Position 2, taille 21024 bytes (si compressed flag set)
- Décompressés avec DPCM16 en utilisant un prédictor initial de 0
- Total : ~40946 samples (~1.86s @ 22050Hz)

### Vidéo

#### Compression LZS

Les frames vidéo utilisent la compression **LZS** (Lempel-Ziv-Storer) :

- Décompression vers un buffer indexé (palette 8-bit)
- Optimisé pour les animations (référence aux pixels précédents)

#### Structure des frames

```
Offset | Size | Description
-------|------|-------------
0x00   | 4    | Frame data size (compressed)
0x04   | 2    | X position
0x06   | 2    | Y position  
0x08   | 2    | Width
0x0A   | 2    | Height
0x0C   | 2    | Color depth (généralement 8)
0x0E   | 4    | Unknown flags
0x12   | 4    | Audio position (position dans le flux audio final)
0x16   | 4    | Audio size (compressed DPCM data size)
0x1A   | ... | Compressed video data (LZS)
       | ... | Compressed audio data (DPCM16)
```

## Buffer circulaire audio

ScummVM utilise un **buffer circulaire** pour gérer le streaming audio :

```cpp
const int32 bufferSize = ((2 * 1 * 22050 * 2000) / 1000) & ~3;
// = 88200 bytes (2 seconds @ 22050Hz stereo)
```

### Fonctionnement

1. Les packets audio arrivent de manière asynchrone (par frame vidéo)
2. Chaque packet est placé à sa position `audioPos` dans le buffer circulaire
3. Les canaux EVEN/ODD sont entrelacés (1 sample sur 2)
4. Les gaps (échantillons manquants) sont **interpolés** : `(prev + next) / 2`
5. Le lecteur audio lit séquentiellement depuis `_readHead`

### Variables clés

- `_readHeadAbs` : Position absolue de lecture
- `_writeHeadAbs` : Position absolue d'écriture
- `_jointMin[2]` : Positions minimales écrites pour chaque canal (EVEN/ODD)
- `_maxWriteAbs` : Position maximale autorisée pour l'écriture
- `kEOSExpansion = 2` : Multiplicateur pour l'entrelacement (every other sample)

## Interpolation

Pour les échantillons manquants où le canal opposé n'a pas encore écrit :

```cpp
sample = (*inBuffer + previousSample) >> 1;  // Moyenne des voisins
```

Cela permet à la qualité audio de dégrader gracieusement à 11kHz si le décodage vidéo prend du retard.

## Endianness

- **v5 (PC)** : Little-endian
- **v6 (Mac)** : Big-endian (sauf audio headers qui restent little-endian)

## Références

- **Source** : `scummvm/engines/sci/video/robot_decoder.h`
- **Implémentation** : `scummvm/engines/sci/video/robot_decoder.cpp`
- **DPCM Decoder** : `scummvm/engines/sci/sound/decoders/sol.cpp`
