# Format RBT (Robot Video) - Documentation Technique Complète

**Source**: Code source ScummVM (robot_decoder.h et robot_decoder.cpp)  
**Révision**: Basée sur l'implémentation ScummVM de référence  
**Date**: 2025  

---

## Table des Matières

1. [Vue d'ensemble](#vue-densemble)
2. [Versions du format](#versions-du-format)
3. [Structure générale du fichier](#structure-générale-du-fichier)
4. [En-tête principal (Header)](#en-tête-principal)
5. [Section Audio Primer](#section-audio-primer)
6. [Section Palette](#section-palette)
7. [Index des frames](#index-des-frames)
8. [Table des Cues](#table-des-cues)
9. [Paquets de données (Frames)](#paquets-de-données)
10. [Format vidéo (Cels)](#format-vidéo)
11. [Format audio](#format-audio)
12. [Algorithmes de décompression](#algorithmes-de-décompression)
13. [Notes d'implémentation](#notes-dimplémentation)

---

## Vue d'ensemble

### Qu'est-ce que le format Robot ?

Robot est un format de conteneur audio-vidéo paquetisé (packetized streaming AV format) développé par Sierra pour les jeux SCI (Sierra Creative Interpreter). Il encode :

- **Plusieurs bitmaps** (appelés "cels") avec données de positionnement
- **Audio synchronisé** pour un rendu dans le système graphique SCI
- **Métadonnées de timing** (cuepoints, frame rate)

### Caractéristiques principales

- **Pas de compression inter-frames** : Contrairement aux formats vidéo modernes, la compression est locale à chaque cel
- **Coordonnées dépendantes du contexte** : Certaines informations (résolution, arrière-plan) dépendent de données externes au fichier Robot
- **Palette remapping** (v6) : Les robots v6 peuvent participer au remapping de palette (pixels remap)
- **Audio multicanal** : Audio encodé en deux canaux ('even' et 'odd') à 11025Hz chacun

### Applications

Les vidéos Robot nécessitent **presque toujours** d'être lues dans le moteur de jeu car :
- La résolution des coordonnées Robot dépend de la configuration du jeu
- L'arrière-plan pour la vidéo est stocké ailleurs
- Les informations de remapping de palette (v6) ne sont pas dans le fichier Robot

---

## Versions du format

Il existe plusieurs versions connues du format Robot :

| Version | Date          | Jeux utilisant cette version                    | Statut       |
|---------|---------------|-------------------------------------------------|--------------|
| v2      | Avant nov 1994| Aucun exemple connu                             | Obsolète     |
| v3      | Avant nov 1994| Aucun exemple connu                             | Obsolète     |
| v4      | Janvier 1995  | King's Quest 7 v1.65, Police Quest: SWAT demo  | Fonctionnel  |
| v5      | Mars 1995     | Jeux SCI2.1 et SCI3 (KQ7, Phantasmagoria, PQSWAT, Lighthouse) | Principal |
| v6      | SCI3          | RAMA                                           | Avancé       |

**Note** : Cette documentation se concentre sur les versions **v5 et v6**, qui sont les plus courantes.

---

## Structure générale du fichier

Un fichier Robot (.RBT) est organisé selon la structure suivante :

```
┌─────────────────────────────────────┐
│ En-tête principal (60 octets)       │  ← Métadonnées globales
├─────────────────────────────────────┤
│ Section Audio Primer (optionnelle)  │  ← Données d'amorçage audio
├─────────────────────────────────────┤
│ Palette (optionnelle)               │  ← Palette de couleurs HunkPalette
├─────────────────────────────────────┤
│ Index des tailles vidéo             │  ← Taille vidéo de chaque frame
│ (numFrames × 2 ou 4 octets)         │
├─────────────────────────────────────┤
│ Index des tailles de paquets        │  ← Taille totale (vidéo+audio)
│ (numFrames × 2 ou 4 octets)         │
├─────────────────────────────────────┤
│ Table des temps de cue              │  ← 256 × 4 octets (int32)
├─────────────────────────────────────┤
│ Table des valeurs de cue            │  ← 256 × 2 octets (uint16)
├─────────────────────────────────────┤
│ Padding d'alignement (2048 octets)  │  ← Alignement sur secteur CD
├─────────────────────────────────────┤
│ Frame 0 (Paquet vidéo + audio)      │
│ Frame 1 (Paquet vidéo + audio)      │
│ ...                                 │
│ Frame N (Paquet vidéo + audio)      │
└─────────────────────────────────────┘
```

### Endianness (ordre des octets)

Les entiers dans les fichiers Robot sont codés en **endianness native** :
- **Little-endian (LSB)** pour les versions x86/PC
- **Big-endian (MSB)** pour les versions 68k/PPC/Mac

**Important** : Certains jeux (comme Lighthouse) étaient distribués sur CD dual PC/Mac partageant les **mêmes fichiers Robot little-endian**. L'endianness ne correspond donc **pas toujours** à la plateforme.

#### Détection de l'endianness

Pour déterminer l'endianness d'un fichier Robot :

1. Lire les 2 octets à l'offset **6** (champ `version`) en **big-endian**
2. Si `0 < version <= 0x00FF` → le fichier est **big-endian**
3. Sinon → le fichier est **little-endian**

**Code de référence** (ScummVM) :
```cpp
stream->seek(6, SEEK_SET);
const uint16 version = stream->readUint16BE();
const bool bigEndian = (0 < version && version <= 0x00ff);
```

---

## En-tête principal

L'en-tête du fichier Robot (v5/v6) fait **60 octets** et contient toutes les métadonnées nécessaires au décodage.

### Tableau des champs

| Offset | Taille | Type   | Description                                                               |
|--------|--------|--------|---------------------------------------------------------------------------|
| 0      | 1      | uint8  | **Signature** : `0x16` (obligatoire)                                      |
| 1      | 1      | uint8  | Non utilisé                                                               |
| 2-5    | 4      | char[4]| **Signature** : `'SOL\0'` (obligatoire)                                   |
| 6-7    | 2      | uint16 | **Version** : 4, 5 ou 6                                                   |
| 8-9    | 2      | uint16 | **Taille des blocs audio** (en octets)                                    |
| 10-11  | 2      | uint16 | **Flag "primer is compressed"**                                           |
| 12-13  | 2      | uint16 | Non utilisé                                                               |
| 14-15  | 2      | uint16 | **Nombre total de frames** vidéo                                          |
| 16-17  | 2      | uint16 | **Taille de la palette embarquée** (en octets, 0 si pas de palette)      |
| 18-19  | 2      | uint16 | **Primer reserved size**                                                  |
| 20-21  | 2      | int16  | **Résolution X des coordonnées** (0 = utiliser résolution du jeu)        |
| 22-23  | 2      | int16  | **Résolution Y des coordonnées** (0 = utiliser résolution du jeu)        |
| 24     | 1      | uint8  | **Flag palette** : ≠0 si le Robot inclut une palette                     |
| 25     | 1      | uint8  | **Flag audio** : ≠0 si le Robot inclut de l'audio                        |
| 26-27  | 2      | uint16 | Non utilisé                                                               |
| 28-29  | 2      | int16  | **Frame rate** (images par seconde)                                       |
| 30-31  | 2      | int16  | **Coordinate conversion flag** (HiRes)                                    |
| 32-33  | 2      | int16  | **Max skippable packets** (sans audio drop-out)                           |
| 34-35  | 2      | int16  | **Nombre max de cels par frame**                                          |
| 36-39  | 4      | int32  | **Taille max du 1er cel fixe** (en pixels)                                |
| 40-43  | 4      | int32  | **Taille max du 2e cel fixe** (en pixels)                                 |
| 44-47  | 4      | int32  | **Taille max du 3e cel fixe** (en pixels)                                 |
| 48-51  | 4      | int32  | **Taille max du 4e cel fixe** (en pixels)                                 |
| 52-59  | 8      | -      | Non utilisé (réservé)                                                     |

### Validation de l'en-tête

**Vérifications obligatoires** :

1. **Signature 1** : L'octet à l'offset 0 doit être `0x16`
2. **Signature 2** : Les octets 2-5 doivent être `'SOL\0'` (ou `MKTAG('S','O','L',0)`)
3. **Version** : Doit être 4, 5 ou 6 (autres versions non supportées)

**Exemple de code** :
```cpp
const uint16 id = stream->readUint16LE();
if (id != 0x16) {
    error("Invalid robot file");
}

stream->seek(2, SEEK_SET);
if (stream->readUint32BE() != MKTAG('S', 'O', 'L', 0)) {
    error("Resource is not Robot type!");
}
```

### Champs importants

#### Frame Rate
- **Valeur typique** : 10-15 fps
- **Si = 0** : Utiliser le frame rate par défaut du jeu

#### Coordinate Conversion Flag (HiRes)
- **Si `true`** : Les coordonnées du Robot doivent être utilisées telles quelles pour l'affichage d'une frame spécifique
- **Si `false`** : Les coordonnées doivent être converties selon la résolution du jeu

#### Max Skippable Packets
- Nombre maximum de paquets pouvant être sautés sans causer de perte audio (audio drop-out)
- Utilisé pour la synchronisation AV

#### Cels fixes (Fixed Cels)
- Les robots peuvent avoir jusqu'à **4 cels "fixes"** (persistent toute la durée du robot)
- Les tailles max (en pixels) sont pré-allouées pour optimiser la mémoire

---

## Section Audio Primer

Le "primer" audio est une section d'amorçage qui initialise les buffers audio avant la lecture des frames.

### Logique de traitement

**Si `flag audio` = false** :
- Sauter `primer reserved size` octets depuis la fin de l'en-tête

**Si `flag audio` = true ET `primer reserved size` ≠ 0** :
- Lire l'en-tête du primer audio (14 octets)
- Lire les données audio compressées

**Si `flag audio` = true ET `primer reserved size` = 0 ET `primer is compressed flag` est activé** :
- Even primer size = **19922** octets
- Odd primer size = **21024** octets
- Les buffers "even" et "odd" doivent être **remplis de zéros**

**Toute autre combinaison** → **Erreur (flags corrompus)**

### En-tête du Primer Audio

Si présent, l'en-tête du primer audio (14 octets) :

| Offset | Taille | Type  | Description                                              |
|--------|--------|-------|----------------------------------------------------------|
| 0-3    | 4      | int32 | **Taille totale de la section primer** (en octets)       |
| 4-5    | 2      | int16 | **Format de compression** (doit être 0)                  |
| 6-9    | 4      | int32 | **Taille du primer "even"** (en octets)                  |
| 10-13  | 4      | int32 | **Taille du primer "odd"** (en octets)                   |

**Après l'en-tête** :
- Données compressées "even" (`even primer size` octets)
- Données compressées "odd" (`odd primer size` octets)

### Validation

Si `even primer size + odd primer size ≠ primer reserved size` :
- Le prochain bloc d'en-tête se trouve à `primer reserved size` octets du **début** de l'en-tête audio primer

Sinon :
- Le prochain bloc suit immédiatement les données primer

---

## Section Palette

Si le **flag palette** (offset 24) est activé, une palette HunkPalette SCI suit le primer audio.

### Format

- **Taille** : Indiquée par le champ `palette size` (offset 16-17)
- **Format** : HunkPalette SCI (format propriétaire Sierra)
- **Lecture** : Lire `palette size` octets

**Si le flag palette = false** :
- Sauter `palette size` octets (padding)

**Code de référence** :
```cpp
if (hasPalette) {
    stream->read(_rawPalette, paletteSize);
} else {
    stream->seek(paletteSize, SEEK_CUR);
}
```

---

## Index des frames

Après la section palette, deux index consécutifs décrivent les tailles des frames.

### 1. Index des tailles vidéo

Contient la **taille compressée de la vidéo** pour chaque frame.

**Format** :
- **Version 5** : `numFrames × 2 octets` (uint16)
- **Version 6** : `numFrames × 4 octets` (uint32/int32)

**Ordre** : Frame 0, Frame 1, ..., Frame N-1

### 2. Index des tailles de paquets

Contient la **taille totale (vidéo + audio)** pour chaque frame.

**Format** :
- **Version 5** : `numFrames × 2 octets` (uint16)
- **Version 6** : `numFrames × 4 octets` (uint32/int32)

**Ordre** : Frame 0, Frame 1, ..., Frame N-1

**Code de référence** :
```cpp
switch(_version) {
case 5: // 16-bit sizes
    for (int i = 0; i < _numFramesTotal; ++i) {
        _videoSizes.push_back(_stream->readUint16());
    }
    for (int i = 0; i < _numFramesTotal; ++i) {
        recordSizes.push_back(_stream->readUint16());
    }
    break;
case 6: // 32-bit sizes
    for (int i = 0; i < _numFramesTotal; ++i) {
        _videoSizes.push_back(_stream->readSint32());
    }
    for (int i = 0; i < _numFramesTotal; ++i) {
        recordSizes.push_back(_stream->readSint32());
    }
    break;
}
```

---

## Table des Cues

Les cuepoints permettent de synchroniser des événements de jeu avec la vidéo Robot.

### Structure

Deux tables consécutives de **256 entrées** chacune :

#### 1. Table des temps de cue
- **Taille** : 256 × 4 octets = **1024 octets**
- **Type** : int32
- **Signification** : Nombre de ticks depuis le début de la lecture où le cue point se déclenche

#### 2. Table des valeurs de cue
- **Taille** : 256 × 2 octets = **512 octets**
- **Type** : uint16
- **Signification** : Valeurs renvoyées au moteur de jeu quand un cue est demandé

**Code de référence** :
```cpp
for (int i = 0; i < kCueListSize; ++i) {
    _cueTimes[i] = _stream->readSint32();
}

for (int i = 0; i < kCueListSize; ++i) {
    _cueValues[i] = _stream->readUint16();
}
```

### Utilisation

Les cues permettent au moteur de jeu de savoir quand déclencher des actions (dialogues, effets, transitions) synchronisées avec la vidéo.

---

## Alignement et début des frames

Après la table des cues, le fichier doit être **aligné sur un secteur de 2048 octets** (alignement CD-ROM).

**Calcul du padding** :
```cpp
int bytesRemaining = (stream->pos() - _fileOffset) % kRobotFrameSize;
if (bytesRemaining != 0) {
    stream->seek(kRobotFrameSize - bytesRemaining, SEEK_CUR);
}
```

Où `kRobotFrameSize = 2048`

**Résultat** : Le premier paquet de frame commence à une position alignée.

---

## Paquets de données

Chaque frame du Robot est un **paquet** composé de données vidéo suivies (optionnellement) de données audio.

### Structure d'un paquet

```
┌─────────────────────────────────────┐
│ Données vidéo (taille variable)     │  ← Voir "Index des tailles vidéo"
├─────────────────────────────────────┤
│ Données audio (optionnelle)         │  ← Si flag audio = true
└─────────────────────────────────────┘
```

### Calcul de la position d'une frame

Pour effectuer un **seek aléatoire** vers la frame N :

```cpp
int position = _firstFramePosition; // Position après alignement
for (int i = 0; i < N; i++) {
    position += recordSizes[i]; // Taille totale (vidéo+audio)
}
stream->seek(position, SEEK_SET);
```

**Note** : Le seek aléatoire désactive normalement la lecture audio, car les données audio d'un paquet ne correspondent pas toujours à la vidéo du même paquet.

---

## Format vidéo

### Structure des données vidéo

```
┌─────────────────────────────────────┐
│ Nombre de cels (2 octets, uint16)   │
├─────────────────────────────────────┤
│ Cel 0                               │
│ Cel 1                               │
│ ...                                 │
│ Cel N                               │
└─────────────────────────────────────┘
```

**Maximum** : 10 cels par frame (constante `kScreenItemListSize`)

### Structure d'un Cel

Chaque cel est composé d'un **en-tête** (18 octets) suivi de **chunks de données**.

#### En-tête de cel (18 octets)

| Offset | Taille | Type   | Description                                                          |
|--------|--------|--------|----------------------------------------------------------------------|
| 0      | 1      | uint8  | Non utilisé                                                          |
| 1      | 1      | uint8  | **Facteur de décimation verticale** (%)                              |
|        |        |        | 100 = pas de compression, 50 = 50% des lignes retirées               |
| 2-3    | 2      | uint16 | **Largeur du cel** (en pixels)                                       |
| 4-5    | 2      | uint16 | **Hauteur du cel** (en pixels)                                       |
| 6-9    | 4      | -      | Non utilisé                                                          |
| 10-11  | 2      | int16  | **Position X du cel** (coordonnées Robot)                            |
| 12-13  | 2      | int16  | **Position Y du cel** (coordonnées Robot)                            |
| 14-15  | 2      | uint16 | **Taille totale des data chunks** (en octets)                        |
| 16-17  | 2      | uint16 | **Nombre de data chunks**                                            |

**Taille totale de l'en-tête** : **18 octets** (pas 22 comme indiqué dans certaines constantes obsolètes)

#### Décimation verticale (Line Decimation)

- **Valeur = 100** : Aucune compression verticale
- **Valeur < 100** : Des lignes ont été supprimées lors de la compression
- **Décompression** : Les lignes manquantes sont reconstruites par **interpolation**

**Exemple** :
- `vertical scale factor = 50` → 50% des lignes ont été retirées
- Lors du décodage, reconstruire les lignes manquantes en interpolant les lignes adjacentes

### Data Chunks

Un cel est construit à partir d'un ou plusieurs **blocs de données contigus** (data chunks).

#### Structure d'un data chunk

```
┌─────────────────────────────────────┐
│ En-tête du chunk (10 octets)        │
├─────────────────────────────────────┤
│ Données du cel (compressées ou non) │
└─────────────────────────────────────┘
```

#### En-tête du data chunk (10 octets)

| Offset | Taille | Type   | Description                                          |
|--------|--------|--------|------------------------------------------------------|
| 0-3    | 4      | uint32 | **Taille compressée** (en octets)                    |
| 4-7    | 4      | uint32 | **Taille décompressée** (en octets)                  |
| 8-9    | 2      | uint16 | **Type de compression**                              |
|        |        |        | 0 = LZS, 2 = non compressé                           |

#### Types de compression

| Valeur | Constante          | Description                                    |
|--------|--------------------|------------------------------------------------|
| 0      | `kCompressionLZS`  | Données compressées avec l'algorithme LZS      |
| 2      | `kCompressionNone` | Données non compressées (brutes)               |

### Décodage d'un cel

**Algorithme** :

1. Lire l'en-tête du cel (18 octets)
2. Pour chaque data chunk :
   - Lire l'en-tête du chunk (10 octets)
   - Lire les données (taille = `compressed size`)
   - Si `compression type = 0` (LZS) → Décompresser avec LZS
   - Si `compression type = 2` → Copier tel quel
   - Concaténer au buffer de sortie
3. Si `vertical scale factor < 100` :
   - Appliquer l'interpolation verticale pour reconstruire les lignes manquantes

**Note** : Tous les chunks d'un même cel sont **contigus** et doivent être assemblés dans l'ordre.

---

## Format audio

L'audio Robot utilise un encodage **SOL DPCM16** avec un système de canaux alternés.

### Caractéristiques

- **Codec** : DPCM16 (Differential Pulse Code Modulation 16-bit)
- **Canaux** : 2 canaux ('even' et 'odd')
- **Fréquence de chaque canal** : **11025 Hz**
- **Fréquence finale** : **22050 Hz** (après entrelacement)
- **Taille des blocs** : Fixe (sauf primer), définie dans l'en-tête

### Structure des données audio

Si une frame contient de l'audio, les données audio suivent immédiatement les données vidéo.

```
┌─────────────────────────────────────┐
│ En-tête audio (8 octets)            │
├─────────────────────────────────────┤
│ DPCM runway (8 octets)              │  ← Échantillons de démarrage
├─────────────────────────────────────┤
│ Données audio compressées           │
└─────────────────────────────────────┘
```

#### En-tête audio (8 octets)

| Offset | Taille | Type  | Description                                                    |
|--------|--------|-------|----------------------------------------------------------------|
| 0-3    | 4      | int32 | **Position absolue** de l'audio dans le flux (compressé)       |
| 4-7    | 4      | int32 | **Taille du bloc audio**, excluant l'en-tête (en octets)       |

#### DPCM Runway (8 octets)

- **Fonction** : Échantillons de démarrage pour déplacer le signal à la position correcte
- **Traitement** : Ces 8 octets sont décompressés mais **jamais écrits** dans le flux de sortie
- **Raison** : La compression DPCM nécessite un état initial ; le 9e échantillon est le premier valide

### Canaux 'Even' et 'Odd'

L'audio est divisé en **deux canaux alternés** :

- **Canal 'Even'** (pair) : Paquets dont `absolute position of audio` est **divisible par 2** (pair)
- **Canal 'Odd'** (impair) : Paquets dont `absolute position of audio` n'est **PAS divisible par 2** (impair)

**Note importante** : La spécification officielle utilise la divisibilité par 2 (pair/impair). Cependant, le code ScummVM utilise `position % 4 == 0` pour des raisons d'implémentation interne de leur buffer audio. Pour une implémentation conforme à la spécification, utilisez `position % 2 == 0`.

**Reconstruction du signal** :
1. Décompresser chaque canal séparément
2. Entrelacer les échantillons : Even[0], Odd[0], Even[1], Odd[1], ...

**Exemple avec positions réelles** :
```
Position absolue = 39844 (paire)   → Canal Even
Position absolue = 42049 (impaire) → Canal Odd
Position absolue = 44254 (paire)   → Canal Even
Position absolue = 46459 (impaire) → Canal Odd
...
```

**Distribution** : Dans un fichier Robot standard, la distribution est équilibrée 50/50 entre les canaux EVEN et ODD.

### Algorithme de décodage audio

**Pour chaque bloc audio** :

1. **Lire l'en-tête** (8 octets)
   - `position` = position absolue
   - `size` = taille du bloc

2. **Déterminer le canal** :
   - **Méthode spécification** : Si `position % 2 == 0` → Canal Even, sinon → Canal Odd
   - **Méthode ScummVM** : Si `position % 4 == 0` → Canal Even, sinon → Canal Odd
   - **Recommandation** : Utilisez la méthode spécification (% 2) pour une distribution équilibrée

3. **Vérifier si le bloc doit être ignoré** :
   - Calculer `packetEndByte = position + (size × (sizeof(int16) + kEOSExpansion))`
   - Si `packetEndByte ≤ MAX(readHead, jointMin[canal])` → **Ignorer** (déjà lu)

4. **Décompresser DPCM16** :
   - Appliquer la décompression DPCM sur **tout le bloc** (y compris runway)
   - Valeur initiale = 0
   - Résultat : échantillons 16-bit PCM

5. **Copier dans le buffer final** :
   - **Ignorer les 8 premiers octets** (DPCM runway)
   - Copier chaque échantillon dans **une position sur deux** du buffer final
   - Échantillon 1 → Position 2, Échantillon 2 → Position 4, etc.

6. **Interpoler les zones manquantes** :
   - Pour les positions non écrites par le canal opposé
   - Interpolation : `(sample[i-1] + sample[i+1]) / 2`
   - **Ne PAS écraser** les données déjà écrites par l'autre canal

**Note importante** : Les données des paquets ultérieurs ne doivent **jamais** écraser les données du même canal déjà écrites par un paquet précédent (les 8 premiers octets du paquet suivant sont du bruit utilisé pour repositionner la forme d'onde DPCM).

### Gestion de la perte de qualité

En cas de décodage lent, la qualité audio peut se dégrader à **11kHz** en utilisant uniquement l'interpolation d'un canal. Cela évite les drop-outs audio complets.

---

## Algorithmes de décompression

### LZS (Lempel-Ziv-Storer)

**Usage** : Compression des données vidéo (cels)

**Caractéristiques** :
- Compression sans perte
- Basée sur la recherche de motifs répétés (sliding window)
- Décompression rapide et peu gourmande en mémoire

**Implémentation** : Voir `DecompressorLZS` dans ScummVM ou le fichier `decompressor_lzs.cpp` du projet.

**Code de référence** :
```cpp
DecompressorLZS _decompressor;
_decompressor.unpack(outputBuffer, inputBuffer, compressedSize, decompressedSize);
```

### DPCM16 (Differential Pulse Code Modulation 16-bit)

**Usage** : Compression audio

**Principe** :
- Au lieu d'encoder la valeur absolue de chaque échantillon, encode la **différence** avec l'échantillon précédent
- Réduit la plage de valeurs nécessaires → meilleure compression

**Algorithme de décompression** :
```cpp
void deDPCM16Mono(int16 *out, const byte *in, const uint32 numBytes, int16 &sample) {
    for (uint32 i = 0; i < numBytes; i++) {
        sample += (int8)in[i]; // Ajouter le delta (signé)
        out[i] = sample;       // Écrire l'échantillon décodé
    }
}
```

**Paramètres** :
- `out` : Buffer de sortie (échantillons 16-bit)
- `in` : Buffer d'entrée (deltas 8-bit signés)
- `numBytes` : Nombre d'octets à lire
- `sample` : Valeur initiale (modifiée par la fonction, utilisée comme état)

**Note** : La valeur initiale `sample` doit être **0** au début d'un bloc.

---

## Notes d'implémentation

### 1. Seek aléatoire (Random frame seeking)

**Algorithme** :
```cpp
int position = firstFramePosition;
for (int i = 0; i < targetFrame; i++) {
    position += packetSizes[i];
}
stream->seek(position, SEEK_SET);
```

**Conséquences** :
- Désactive normalement la lecture audio
- L'audio dans un paquet ne correspond pas forcément à la vidéo du même paquet

### 2. Synchronisation Audio-Vidéo

**Problème** : Le décodage vidéo peut être plus lent ou plus rapide que le temps réel.

**Solution** : Ajustement dynamique du frame rate

- **Si la vidéo est en avance** (> 1 frame) → Ralentir (`frameRate = minFrameRate`)
- **Si la vidéo est en retard** (> 1 frame) → Accélérer (`frameRate = maxFrameRate`)
- **Sinon** → Frame rate normal

**Limites** :
```cpp
_minFrameRate = _frameRate - kMaxFrameRateDrift; // -1 fps
_maxFrameRate = _frameRate + kMaxFrameRateDrift; // +1 fps
```

### 3. Gestion mémoire des cels

**Cels "Fixed"** (persistants) :
- Alloués une fois pour toute la durée du robot
- Maximum : 4 cels fixes (`kFixedCelListSize`)
- Tailles pré-allouées indiquées dans l'en-tête

**Cels "Frame Lifetime"** :
- Alloués pour une seule frame
- Libérés après le rendu de la frame

**Code de référence** :
```cpp
enum CelHandleLifetime {
    kNoCel         = 0,
    kFrameLifetime = 1,
    kRobotLifetime = 2
};
```

### 4. Constantes importantes

```cpp
kScreenItemListSize    = 10    // Max cels par frame
kAudioListSize         = 10    // Max blocs audio en queue
kCueListSize           = 256   // Nombre de cues
kFixedCelListSize      = 4     // Max cels fixes
kRawPaletteSize        = 1200  // Taille palette HunkPalette
kRobotFrameSize        = 2048  // Alignement secteur CD
kRobotZeroCompressSize = 2048  // Taille audio zero-fill
kAudioBlockHeaderSize  = 8     // Taille en-tête audio
kRobotSampleRate       = 22050 // Fréquence audio finale
kEOSExpansion          = 2     // Facteur expansion (even/odd)
```

### 5. Validation et erreurs

**Vérifications recommandées** :

1. **Signature** : `0x16` et `'SOL\0'`
2. **Version** : 4, 5 ou 6 uniquement
3. **Nombre de cels** : ≤ 10 par frame
4. **Type de compression** : 0 ou 2 uniquement
5. **Format compression audio primer** : Doit être 0
6. **Cohérence primer sizes** : Vérifier `even + odd = reserved` si non-zéro

**Gestion des erreurs** :
- Fichiers corrompus → Arrêt immédiat avec message clair
- Flags invalides → Erreur "Flags corrupt"
- Version non supportée → Erreur "Unsupported version X"

### 6. Optimisations

**Pré-allocation** :
- Utiliser les champs `max cel area` pour pré-allouer les buffers de décompression
- Réduire les allocations dynamiques en réutilisant les buffers

**Cache de décompression** :
- Stocker la position du dernier paquet décompressé
- Éviter de re-décompresser si lecture partielle

**Code de référence** :
```cpp
if (_decompressionBufferPosition != packet.position) {
    deDPCM16Mono((int16 *)_decompressionBuffer, packet.data, packet.dataSize, carry);
    _decompressionBufferPosition = packet.position;
}
```

---

## Annexe : Flux d'implémentation recommandé

Pour implémenter un décodeur Robot complet :

### Phase 1 : Lecture de l'en-tête
1. Valider les signatures (`0x16`, `'SOL\0'`)
2. Détecter l'endianness (lecture du champ version)
3. Lire tous les champs de l'en-tête (60 octets)
4. Valider la version (4, 5 ou 6)

### Phase 2 : Initialisation audio
1. Si `hasAudio = true` :
   - Traiter le primer audio (selon les flags)
   - Initialiser les buffers even/odd

### Phase 3 : Lecture des métadonnées
1. Lire la palette (si présente)
2. Lire l'index des tailles vidéo
3. Lire l'index des tailles de paquets
4. Lire les tables de cues (temps et valeurs)

### Phase 4 : Alignement
1. Calculer le padding nécessaire (alignement 2048)
2. Seek vers le début du premier paquet

### Phase 5 : Décodage des frames
Pour chaque frame :
1. Lire le nombre de cels
2. Pour chaque cel :
   - Lire l'en-tête du cel
   - Pour chaque data chunk :
     - Décompresser (LZS ou copie directe)
   - Appliquer l'interpolation verticale si nécessaire
3. Si audio présent :
   - Lire et décompresser les blocs audio
   - Entrelacer les canaux even/odd

### Phase 6 : Rendu et synchronisation
1. Afficher les cels aux positions spécifiées
2. Ajuster le frame rate selon l'AV sync
3. Traiter les cuepoints

---

## Références

- **Code source ScummVM** : `engines/sci/video/robot_decoder.h` et `robot_decoder.cpp`
- **Projet** : https://github.com/scummvm/scummvm
- **Licence** : GPL v3+
- **Documentation SCI** : http://sciwiki.sierrahelp.com/

---

**Auteur** : Documentation générée à partir des commentaires du code source ScummVM  
**Date** : Novembre 2025  
**Version** : 1.0
