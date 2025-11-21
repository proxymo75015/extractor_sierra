# Format de fichier SOL (Sierra Online Audio) - Documentation Technique

**Source**: Code source ScummVM (`engines/sci/sound/audio.cpp`, `engines/sci/sound/decoders/sol.cpp`, `engines/sci/resource/resource_audio.cpp`)  
**Révision**: Basée sur l'implémentation ScummVM de référence  
**Date**: Novembre 2025  

---

## Table des Matières

1. [Vue d'ensemble](#vue-densemble)
2. [Structure générale du fichier](#structure-générale-du-fichier)
3. [En-tête de ressource SCI](#en-tête-de-ressource-sci)
4. [En-tête SOL](#en-tête-sol)
5. [Flags SOL](#flags-sol)
6. [Données audio](#données-audio)
7. [Variantes de format](#variantes-de-format)
8. [Lecture et parsing](#lecture-et-parsing)
9. [Exemples de fichiers](#exemples-de-fichiers)
10. [Compatibilité entre versions SCI](#compatibilité-entre-versions-sci)
11. [Référence des constantes](#référence-des-constantes)

---

## Vue d'ensemble

### Qu'est-ce qu'un fichier SOL ?

**SOL** = **Sierra Online Audio**

Les fichiers SOL sont le format audio propriétaire utilisé par Sierra pour stocker l'audio (parole, effets sonores, musique) dans les jeux SCI (Sierra Creative Interpreter) à partir de **SCI1.1**.

### Caractéristiques

- **Extension** : `.SOL` (rarement utilisée directement, généralement dans des archives)
- **Versions** : SCI1.1, SCI2, SCI2.1, SCI3
- **Compression** : DPCM8 ou DPCM16 (optionnelle)
- **Formats** : Mono/Stéréo, 8-bit/16-bit
- **Échantillonnage** : Variable (typiquement 11025 Hz, 22050 Hz, ou 44100 Hz)

### Jeux utilisant SOL

- **King's Quest 7** (SCI2)
- **Phantasmagoria** (SCI2)
- **Gabriel Knight: Sins of the Fathers CD** (SCI1.1)
- **Quest for Glory 3** (SCI1.1)
- **Police Quest: SWAT** (SCI2.1)
- **Lighthouse** (SCI2.1)
- **RAMA** (SCI3)
- Et tous les jeux SCI1.1+

---

## Structure générale du fichier

Un fichier SOL complet se compose de plusieurs sections :

```
┌────────────────────────────────────────┐
│ En-tête de ressource SCI (2 octets)    │  ← Type de ressource + taille en-tête
├────────────────────────────────────────┤
│ En-tête SOL (5, 9 ou 10 octets)        │  ← Métadonnées audio
├────────────────────────────────────────┤
│ Données audio (compressées ou brutes)  │  ← Contenu audio
└────────────────────────────────────────┘
```

### Taille totale

La taille totale du fichier peut être calculée :

```
Taille totale = kResourceHeaderSize (2) + headerSize + dataSize
```

---

## En-tête de ressource SCI

Tous les fichiers de ressources SCI commencent par un **en-tête de 2 octets**.

### Structure (2 octets)

| Offset | Taille | Type  | Description                                      |
|--------|--------|-------|--------------------------------------------------|
| 0      | 1      | uint8 | **Type de ressource** (avec bit de patch)        |
| 1      | 1      | uint8 | **Taille de l'en-tête SOL** (octets suivants)    |

### Détails du byte 0 (Type de ressource)

```
Bits 6-0 : Type de ressource (kResourceTypeAudio = 12)
Bit 7    : Flag de patch (1 = fichier patch, 0 = fichier normal)
```

**Extraction du type** :
```cpp
ResourceType type = header[0] & 0x7f;  // Masquer le bit 7
```

**Valeur pour audio** :
```cpp
kResourceTypeAudio = 12  // (0x0C)
```

**Exemple** :
```
header[0] = 0x0C  → Type audio, pas un patch
header[0] = 0x8C  → Type audio, fichier patch
```

### Détails du byte 1 (Taille de l'en-tête)

Ce byte indique le **nombre d'octets** de l'en-tête SOL qui suit (sans compter les 2 octets de l'en-tête de ressource).

**Valeurs connues** :
- **7** : Format QFG3 demo (ancien)
- **11** : Format standard SCI1.1 et SCI2
- **12** : Format étendu (rare)

**Constante ScummVM** :
```cpp
kResourceHeaderSize = 2
```

---

## En-tête SOL

L'en-tête SOL suit immédiatement l'en-tête de ressource et contient les métadonnées audio.

### Format court (7 octets) - QFG3 Demo

**Utilisé dans** : Quest for Glory 3 Demo

| Offset | Taille | Type   | Description                              |
|--------|--------|--------|------------------------------------------|
| 0-3    | 4      | char[4]| Signature `'SOL\0'` (MKTAG)              |
| 4-5    | 2      | uint16 | **Sample rate** (Hz) - Little Endian     |
| 6      | 1      | uint8  | **Flags** (compression, format)          |

**Particularité** : La taille des données audio n'est **pas incluse** dans l'en-tête. Elle doit être déduite de la taille totale de la ressource.

### Format standard (11 octets) - SCI1.1+

**Utilisé dans** : La plupart des jeux SCI1.1, SCI2, SCI2.1

| Offset | Taille | Type   | Description                              |
|--------|--------|--------|------------------------------------------|
| 0-3    | 4      | char[4]| Signature `'SOL\0'` (MKTAG)              |
| 4-5    | 2      | uint16 | **Sample rate** (Hz) - Little Endian     |
| 6      | 1      | uint8  | **Flags** (compression, format)          |
| 7-10   | 4      | uint32 | **Data size** (octets) - Little Endian   |

### Format étendu (12 octets) - Rare

**Utilisé dans** : Quelques rares jeux SCI2+

| Offset | Taille | Type   | Description                              |
|--------|--------|--------|------------------------------------------|
| 0-3    | 4      | char[4]| Signature `'SOL\0'` (MKTAG)              |
| 4-5    | 2      | uint16 | **Sample rate** (Hz) - Little Endian     |
| 6      | 1      | uint8  | **Flags** (compression, format)          |
| 7-10   | 4      | uint32 | **Data size** (octets) - Little Endian   |
| 11     | 1      | uint8  | **Champ additionnel** (usage inconnu)    |

### Signature 'SOL\0'

La signature est un **FourCC** (Four Character Code) qui identifie le format.

**Représentation** :
```
Bytes : 'S' 'O' 'L' 0x00
Hexa  : 53 4F 4C 00
MKTAG : MKTAG('S', 'O', 'L', 0)
```

**Vérification** :
```cpp
uint32 tag = stream->readUint32BE();
if (tag != MKTAG('S', 'O', 'L', 0)) {
    // Pas un fichier SOL valide
}
```

### Sample Rate (Fréquence d'échantillonnage)

Fréquence d'échantillonnage en **Hertz**, encodée en **little-endian**.

**Valeurs typiques** :
- **11025 Hz** : Qualité voix/effets (standard SCI)
- **22050 Hz** : Qualité musicale moyenne
- **44100 Hz** : Qualité CD (rare dans SCI)

**Lecture** :
```cpp
uint16 sampleRate = stream->readUint16LE();
```

### Data Size (Taille des données)

Taille des données audio **compressées** (ou brutes si non compressées) en octets.

**Important** : Cette taille est celle des données **sur disque**, pas la taille décompressée.

**Lecture** :
```cpp
uint32 dataSize = stream->readUint32LE();
```

**Note** : Pour le format court (7 octets), ce champ n'existe pas et doit être calculé :
```cpp
dataSize = resourceSize - kResourceHeaderSize - headerSize;
```

---

## Flags SOL

Le byte de flags (offset 6) encode plusieurs informations sur le format audio.

### Définition des flags

```cpp
enum SolFlags {
    kSolFlagCompressed = 1 << 0,  // 0x01 (bit 0)
    kSolFlagUnknown    = 1 << 1,  // 0x02 (bit 1)
    kSolFlag16Bit      = 1 << 2,  // 0x04 (bit 2)
    kSolFlagIsSigned   = 1 << 3   // 0x08 (bit 3)
};
```

### Détails des flags

#### Bit 0 : kSolFlagCompressed (0x01)

- **0** : Audio **non compressé** (PCM brut)
- **1** : Audio **compressé** avec DPCM

**Codec utilisé** :
- Si 16-bit : **DPCM16**
- Si 8-bit : **DPCM8**

#### Bit 1 : kSolFlagUnknown (0x02)

- **Usage** : Inconnu
- **Observation** : Rarement activé dans les jeux
- **Effet** : Aucun dans ScummVM (ignoré)

#### Bit 2 : kSolFlag16Bit (0x04)

- **0** : Audio **8-bit** (1 octet par échantillon)
- **1** : Audio **16-bit** (2 octets par échantillon)

**Important** : Détermine le codec DPCM (DPCM8 vs DPCM16) si compressé.

#### Bit 3 : kSolFlagIsSigned (0x08)

- **0** : Échantillons **non signés** (unsigned)
- **1** : Échantillons **signés** (signed)

**Note** : Ce flag s'applique uniquement à l'audio **non compressé**. L'audio DPCM16 est toujours signé (int16), et DPCM8 utilise une conversion spéciale.

### Combinaisons courantes

| Flags (hex) | Bits       | Description                                    |
|-------------|------------|------------------------------------------------|
| `0x00`      | `00000000` | PCM 8-bit non signé, non compressé            |
| `0x01`      | `00000001` | DPCM8 compressé                                |
| `0x04`      | `00000100` | PCM 16-bit non compressé                       |
| `0x05`      | `00000101` | DPCM16 compressé (le plus courant)             |
| `0x08`      | `00001000` | PCM 8-bit signé, non compressé                 |
| `0x0C`      | `00001100` | PCM 16-bit signé, non compressé                |
| `0x0D`      | `00001101` | DPCM16 compressé avec flag signed (redondant) |

### Extraction des flags

```cpp
byte flags = stream->readByte();

bool isCompressed = (flags & kSolFlagCompressed) != 0;
bool is16Bit      = (flags & kSolFlag16Bit) != 0;
bool isSigned     = (flags & kSolFlagIsSigned) != 0;
```

### Détermination du format

**Algorithme de décision** :

```cpp
if (flags & kSolFlagCompressed) {
    if (flags & kSolFlag16Bit) {
        // DPCM16 compressé
        codec = DPCM16;
    } else {
        // DPCM8 compressé
        codec = DPCM8;
    }
} else {
    // PCM brut (non compressé)
    if (flags & kSolFlag16Bit) {
        codec = PCM16;
    } else {
        codec = PCM8;
    }
}
```

---

## Données audio

Les données audio suivent immédiatement l'en-tête SOL.

### Position des données

```
Position = kResourceHeaderSize + headerSize
         = 2 + headerSize
```

**Exemple** :
- Header size = 11 → Position des données = 2 + 11 = **13**
- Header size = 7 → Position des données = 2 + 7 = **9**

### Format des données

Le format dépend des flags :

#### Audio compressé (DPCM)

**DPCM16** :
- 1 octet compressé → 1 échantillon 16-bit (int16)
- Taille décompressée = `dataSize * 2` octets
- Voir [DPCM16_DECODER_DOCUMENTATION.md](DPCM16_DECODER_DOCUMENTATION.md)

**DPCM8** :
- 1 octet compressé → 2 échantillons 8-bit (2 nibbles)
- Taille décompressée = `dataSize * 2` octets
- Conversion finale en 16-bit pour playback

#### Audio non compressé (PCM brut)

**PCM 16-bit** :
- Format : Signed ou Unsigned selon flag
- Endianness : **Little Endian**
- Taille = `dataSize` octets (déjà décompressé)

**PCM 8-bit** :
- Format : Signed ou Unsigned selon flag
- Taille = `dataSize` octets

### Stéréo vs Mono

**Note importante** : Le format SOL de base (SCI1.1 et SCI2) ne supporte **que le mono**.

Le support **stéréo** a été ajouté dans **SCI2.1** :

```cpp
if (flags & kStereo) {
    if (getSciVersion() < SCI_VERSION_2_1_EARLY) {
        error("SCI2 and earlier did not support stereo SOL audio");
    }
}
```

**Flag stéréo** (dans sol.h) :
```cpp
enum SOLFlags {
    kCompressed = 1,
    k16Bit      = 4,
    kStereo     = 16  // 0x10 (bit 4)
};
```

**Format stéréo** :
- Les échantillons sont **entrelacés** : L, R, L, R, ...
- Chaque canal maintient son propre état DPCM

---

## Variantes de format

### SCI1.1 : Format standard

**Caractéristiques** :
- En-tête : 2 + 11 octets
- Mono uniquement
- DPCM8 ou DPCM16
- Sample rate typique : 11025 Hz

**Jeux** : Gabriel Knight, Quest for Glory 3, etc.

### SCI2 : Identique à SCI1.1

Pas de changements majeurs dans le format SOL.

### SCI2.1 : Ajout du stéréo

**Nouveautés** :
- Support stéréo (flag 0x10)
- En-tête toujours 2 + 11 octets
- DPCM8 stéréo supporté

**Jeux** : Police Quest: SWAT, Lighthouse

### SCI3 : Format mature

**Caractéristiques** :
- Identique à SCI2.1
- Possibilité d'en-tête étendu (12 octets)

**Jeux** : RAMA

### QFG3 Demo : Format court

**Spécificité** :
- En-tête court (2 + 7 octets)
- Pas de champ data size
- Nécessite calcul depuis resource size

---

## Lecture et parsing

### Algorithme complet de lecture

```cpp
// 1. Vérifier l'en-tête de ressource SCI
byte header[2];
stream->read(header, 2);

ResourceType type = header[0] & 0x7f;
if (type != kResourceTypeAudio) {
    return nullptr;  // Pas un fichier audio
}

uint8 headerSize = header[1];

// 2. Lire la signature SOL
uint32 tag = stream->readUint32BE();
if (tag != MKTAG('S', 'O', 'L', 0)) {
    return nullptr;  // Pas un fichier SOL
}

// 3. Lire le sample rate
uint16 sampleRate = stream->readUint16LE();

// 4. Lire les flags
byte flags = stream->readByte();

// 5. Lire la taille des données (si présente)
uint32 dataSize;
if (headerSize == 7) {
    // Format court : calculer depuis la taille de ressource
    dataSize = resourceSize - kResourceHeaderSize - headerSize;
} else {
    // Format standard
    dataSize = stream->readUint32LE();
}

// 6. Positionner le stream au début des données audio
int32 dataPosition = kResourceHeaderSize + headerSize;
stream->seek(dataPosition, SEEK_SET);

// 7. Créer le stream audio approprié
if (flags & kSolFlagCompressed) {
    if (flags & kSolFlag16Bit) {
        return new SOLStream<false, true, false>(stream, sampleRate, dataSize);
    } else {
        return new SOLStream<false, false, false>(stream, sampleRate, dataSize);
    }
} else {
    // PCM brut
    byte rawFlags = Audio::FLAG_LITTLE_ENDIAN;
    if (flags & kSolFlag16Bit) {
        rawFlags |= Audio::FLAG_16BITS;
    }
    if (!(flags & kSolFlagIsSigned)) {
        rawFlags |= Audio::FLAG_UNSIGNED;
    }
    return Audio::makeRawStream(stream, sampleRate, rawFlags);
}
```

### Validation des en-têtes

**Vérifications recommandées** :

1. **Type de ressource** :
   ```cpp
   if ((header[0] & 0x7f) != kResourceTypeAudio) {
       error("Not an audio resource");
   }
   ```

2. **Taille d'en-tête** :
   ```cpp
   if (headerSize != 7 && headerSize != 11 && headerSize != 12) {
       warning("Unsupported audio header size %d", headerSize);
   }
   ```

3. **Signature SOL** :
   ```cpp
   if (tag != MKTAG('S', 'O', 'L', 0)) {
       warning("No 'SOL' FourCC found");
   }
   ```

4. **Sample rate** :
   ```cpp
   if (sampleRate == 0 || sampleRate > 48000) {
       warning("Invalid sample rate %d", sampleRate);
   }
   ```

### Gestion des erreurs

**Code de référence ScummVM** :

```cpp
static bool readSOLHeader(Common::SeekableReadStream *audioStream, 
                          int headerSize, uint32 &size, 
                          uint16 &audioRate, byte &audioFlags, 
                          uint32 resSize) {
    if (headerSize != 7 && headerSize != 11 && headerSize != 12) {
        warning("SOL audio header of size %i not supported", headerSize);
        return false;
    }

    uint32 tag = audioStream->readUint32BE();

    if (tag != MKTAG('S','O','L',0)) {
        warning("No 'SOL' FourCC found");
        return false;
    }

    audioRate = audioStream->readUint16LE();
    audioFlags = audioStream->readByte();

    // Pour le format QFG3 demo, utiliser la taille de ressource
    // Sinon, lire depuis l'en-tête
    if (headerSize == 7)
        size = resSize;
    else
        size = audioStream->readUint32LE();
    
    return true;
}
```

---

## Exemples de fichiers

### Exemple 1 : SOL DPCM16 mono (standard)

```
Offset  Hexa                            Description
------  ------------------------------  ---------------------------
0x00    0C                              Type = kResourceTypeAudio (12)
0x01    0B                              Header size = 11 bytes
0x02    53 4F 4C 00                     Signature 'SOL\0'
0x06    11 2B                           Sample rate = 11025 Hz (LE)
0x08    05                              Flags = 0x05 (compressed, 16-bit)
0x09    40 1F 00 00                     Data size = 8000 bytes (LE)
0x0D    [audio data ...]                DPCM16 compressed data
```

**Analyse** :
- Type : Audio (12)
- Format : DPCM16 compressé
- Fréquence : 11025 Hz
- Taille compressée : 8000 octets
- Taille décompressée : 16000 octets (8000 × 2)
- Durée approximative : ~1.45 secondes

### Exemple 2 : SOL PCM 8-bit non compressé

```
Offset  Hexa                            Description
------  ------------------------------  ---------------------------
0x00    0C                              Type = kResourceTypeAudio
0x01    0B                              Header size = 11 bytes
0x02    53 4F 4C 00                     Signature 'SOL\0'
0x06    11 2B                           Sample rate = 11025 Hz
0x08    00                              Flags = 0x00 (no compression, 8-bit, unsigned)
0x09    E8 03 00 00                     Data size = 1000 bytes
0x0D    [audio data ...]                Raw PCM unsigned 8-bit
```

**Analyse** :
- Type : Audio
- Format : PCM 8-bit non signé, non compressé
- Fréquence : 11025 Hz
- Taille : 1000 octets
- Durée : ~0.09 secondes

### Exemple 3 : SOL DPCM8 (ancien format)

```
Offset  Hexa                            Description
------  ------------------------------  ---------------------------
0x00    0C                              Type = kResourceTypeAudio
0x01    0B                              Header size = 11 bytes
0x02    53 4F 4C 00                     Signature 'SOL\0'
0x06    11 2B                           Sample rate = 11025 Hz
0x08    01                              Flags = 0x01 (compressed, 8-bit)
0x09    00 10 00 00                     Data size = 4096 bytes
0x0D    [audio data ...]                DPCM8 compressed data
```

**Analyse** :
- Type : Audio
- Format : DPCM8 compressé
- Fréquence : 11025 Hz
- Taille compressée : 4096 octets
- Taille décompressée : 8192 octets (4096 × 2)
- Nombre d'échantillons : 8192
- Durée : ~0.74 secondes

### Exemple 4 : QFG3 Demo (format court)

```
Offset  Hexa                            Description
------  ------------------------------  ---------------------------
0x00    0C                              Type = kResourceTypeAudio
0x01    07                              Header size = 7 bytes
0x02    53 4F 4C 00                     Signature 'SOL\0'
0x06    11 2B                           Sample rate = 11025 Hz
0x08    05                              Flags = 0x05 (compressed, 16-bit)
0x09    [audio data ...]                DPCM16 compressed data
                                        (data size déduite de resource size)
```

**Particularité** : Pas de champ data size explicite.

---

## Compatibilité entre versions SCI

### Tableau de compatibilité

| Version SCI    | Header Size | Formats supportés           | Stéréo | Notes                    |
|----------------|-------------|-----------------------------|--------|--------------------------|
| **SCI1.1**     | 11          | DPCM8, DPCM16, PCM          | Non    | Format standard          |
| **SCI2**       | 11          | DPCM8, DPCM16, PCM          | Non    | Identique à SCI1.1       |
| **SCI2.1**     | 11          | DPCM8, DPCM16, PCM          | Oui    | Ajout stéréo             |
| **SCI3**       | 11-12       | DPCM8, DPCM16, PCM          | Oui    | Header étendu rare       |
| **QFG3 Demo**  | 7           | DPCM16 uniquement           | Non    | Format court spécial     |

### Évolution du format

**SCI0 et SCI1 Early** :
- Pas de format SOL
- Audio brut (raw PCM 8-bit unsigned)
- Pas d'en-tête structuré

**SCI1.1** (1992) :
- Introduction du format SOL
- DPCM8 et DPCM16
- En-tête structuré

**SCI2** (1993-1994) :
- Aucun changement

**SCI2.1** (1995) :
- Ajout du support stéréo

**SCI3** (1996-1997) :
- Format mature, stable

### Détection de version

**Code ScummVM** :

```cpp
if (getSciVersion() < SCI_VERSION_2_1_EARLY) {
    // SCI1.1 et SCI2 : mono uniquement
    if (flags & kStereo) {
        error("SCI2 and earlier did not support stereo SOL audio");
    }
}
```

**DPCM8 "old" vs "new"** :

```cpp
if (getSciVersion() < SCI_VERSION_2_1_EARLY) {
    // Ancien format DPCM8 (SCI1.1, SCI2)
    return new SOLStream<false, false, true>(stream, sampleRate, dataSize);
} else {
    // Nouveau format DPCM8 (SCI2.1+)
    return new SOLStream<false, false, false>(stream, sampleRate, dataSize);
}
```

---

## Référence des constantes

### Types de ressources

```cpp
enum ResourceType {
    kResourceTypeAudio = 12,     // Type pour fichiers SOL
    kResourceTypeAudio36 = 18,   // Audio SCI3 (format différent)
};
```

### Constantes de taille

```cpp
enum {
    kResourceHeaderSize = 2,     // Taille en-tête ressource SCI
};
```

### Flags SOL (audio.cpp)

```cpp
enum SolFlags {
    kSolFlagCompressed = 1 << 0,  // 0x01
    kSolFlagUnknown    = 1 << 1,  // 0x02
    kSolFlag16Bit      = 1 << 2,  // 0x04
    kSolFlagIsSigned   = 1 << 3   // 0x08
};
```

### Flags SOL (sol.h - version étendue)

```cpp
enum SOLFlags {
    kCompressed = 1,   // 0x01
    k16Bit      = 4,   // 0x04
    kStereo     = 16   // 0x10
};
```

### Tailles d'en-tête supportées

```cpp
static const int SUPPORTED_HEADER_SIZES[] = { 7, 11, 12 };
```

### Sample rates typiques

```cpp
static const int COMMON_SAMPLE_RATES[] = {
    8000,   // Téléphonie
    11025,  // Standard SCI (voix)
    22050,  // Qualité moyenne (musique)
    44100   // Qualité CD (rare)
};
```

---

## Outils et utilitaires

### Extraction depuis archives

Les fichiers SOL ne sont généralement **pas stockés individuellement** mais dans des **archives de ressources SCI**.

**Fichiers d'archive** :
- `RESOURCE.AUD` : Archive audio principale
- `RESSCI.00X` : Volumes de ressources (peuvent contenir audio)

**Outils d'extraction** :
- **ScummVM Tools** : `sci_unpack` peut extraire les ressources
- **SCI Companion** : Éditeur de ressources SCI avec extraction

### Conversion vers formats standards

**ScummVM** peut convertir SOL vers :
- **WAV** (PCM brut)
- **MP3** (avec compression moderne)
- **OGG Vorbis** (avec compression moderne)
- **FLAC** (compression sans perte)

**Processus** :
1. Décoder SOL → PCM brut
2. Encoder PCM → format cible

### Création de fichiers SOL

**Note** : La création de nouveaux fichiers SOL est rarement nécessaire (jeux rétro).

**Algorithme** :
1. Préparer les données PCM (16-bit signed, sample rate désiré)
2. Compresser avec DPCM16 (optionnel)
3. Construire l'en-tête de ressource (2 octets)
4. Construire l'en-tête SOL (11 octets)
5. Écrire les données audio
6. Calculer et valider la taille totale

---

## Limitations et notes

### Limitations du format

1. **Pas de métadonnées** : Pas de titre, artiste, commentaires
2. **Pas de chapitres** : Pas de marqueurs de position
3. **Compression fixe** : Pas de sélection de qualité DPCM
4. **Mono dominant** : Stéréo uniquement en SCI2.1+

### Notes d'implémentation

**Alignement** :
- ScummVM aligne la taille des données SOL sur **4 octets** (32 bits) :
  ```cpp
  _rawDataSize = rawDataSize & ~3;
  ```

**Seek limité** :
- Le DPCM étant différentiel, le **seek aléatoire** n'est pas possible
- Seul le retour au début (position 0) est supporté

**Endianness** :
- Signature : **Big Endian** (MKTAG)
- Sample rate : **Little Endian**
- Data size : **Little Endian**
- Audio data : **Little Endian** (si 16-bit)

### Bugs connus

**Gabriel Knight CD** :
- Certains fichiers DPCM8 ont des overflows
- ScummVM implémente un système de réparation (popfix)
- Voir [DPCM16_DECODER_DOCUMENTATION.md](DPCM16_DECODER_DOCUMENTATION.md)

---

## Exemples de code

### Exemple 1 : Lecture basique

```cpp
#include "sci/sound/decoders/sol.h"

// Ouvrir le fichier
Common::File *file = new Common::File();
file->open("speech.001");

// Créer le stream SOL
Audio::SeekableAudioStream *stream = makeSOLStream(file, DisposeAfterUse::YES);

if (stream) {
    int sampleRate = stream->getRate();
    bool stereo = stream->isStereo();
    uint32 lengthMs = stream->getLength().msecs();
    
    printf("Sample rate: %d Hz\n", sampleRate);
    printf("Stereo: %s\n", stereo ? "Yes" : "No");
    printf("Duration: %d ms\n", lengthMs);
}
```

### Exemple 2 : Extraction vers WAV

```cpp
Audio::SeekableAudioStream *sol = makeSOLStream(file, DisposeAfterUse::YES);

if (sol) {
    int16 *pcmBuffer = new int16[sol->getLength().totalNumberOfFrames()];
    int samplesRead = sol->readBuffer(pcmBuffer, sol->getLength().totalNumberOfFrames());
    
    // Écrire dans un fichier WAV
    Common::File wavFile;
    wavFile.open("output.wav", Common::File::kFileWriteMode);
    
    writeWAVHeader(wavFile, samplesRead, sol->getRate(), sol->isStereo());
    wavFile.write(pcmBuffer, samplesRead * sizeof(int16));
    
    delete[] pcmBuffer;
}
```

### Exemple 3 : Vérification de format

```cpp
bool isValidSOL(Common::SeekableReadStream *stream) {
    int32 startPos = stream->pos();
    
    byte header[6];
    if (stream->read(header, 6) != 6) {
        stream->seek(startPos, SEEK_SET);
        return false;
    }
    
    // Vérifier type audio
    if ((header[0] & 0x7f) != kResourceTypeAudio) {
        stream->seek(startPos, SEEK_SET);
        return false;
    }
    
    // Vérifier signature SOL
    if (READ_BE_UINT32(header + 2) != MKTAG('S', 'O', 'L', 0)) {
        stream->seek(startPos, SEEK_SET);
        return false;
    }
    
    stream->seek(startPos, SEEK_SET);
    return true;
}
```

---

## Références

### Documentation

- **Wiki Multimedia** : https://wiki.multimedia.cx/index.php?title=Sierra_Audio
- **ScummVM Wiki** : https://wiki.scummvm.org/index.php/SCI
- **SCI Wiki** : http://sciwiki.sierrahelp.com/

### Code source ScummVM

- **Décodeur principal** : `engines/sci/sound/decoders/sol.cpp`
- **Header** : `engines/sci/sound/decoders/sol.h`
- **Lecteur audio** : `engines/sci/sound/audio.cpp`
- **Gestion ressources** : `engines/sci/resource/resource_audio.cpp`
- **Types ressources** : `engines/sci/resource/resource.h`

### Spécifications connexes

- [FORMAT_RBT_DOCUMENTATION.md](FORMAT_RBT_DOCUMENTATION.md) - Format Robot (utilise SOL/DPCM16)
- [DPCM16_DECODER_DOCUMENTATION.md](DPCM16_DECODER_DOCUMENTATION.md) - Algorithme DPCM

---

## Glossaire

- **SOL** : Sierra Online Audio (format audio propriétaire)
- **SCI** : Sierra Creative Interpreter (moteur de jeu)
- **DPCM** : Differential Pulse Code Modulation (compression audio)
- **PCM** : Pulse Code Modulation (audio non compressé)
- **FourCC** : Four Character Code (identifiant 4 caractères)
- **Little Endian** : Ordre d'octets (LSB en premier)
- **Big Endian** : Ordre d'octets (MSB en premier)

---

**Auteur** : Documentation générée à partir du code source ScummVM  
**Contributeurs** : Équipe ScummVM  
**Licence** : GPL v3+  
**Date** : Novembre 2025  
**Version** : 1.0
