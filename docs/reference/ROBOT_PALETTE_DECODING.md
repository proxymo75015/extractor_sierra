# Décodage de la Palette Robot (.rbt)

## Vue d'ensemble

Les fichiers Robot SCI32 peuvent contenir une palette intégrée au format HunkPalette, qui est une structure de données spécifique à SCI permettant de stocker des palettes de taille variable (< 256 couleurs). Cette documentation décrit le processus complet de décodage de cette palette.

## Localisation de la Palette dans le Fichier RBT

### Position dans l'En-tête Robot

```
Offset | Taille | Description
-------|--------|------------------------------------------
16-17  | uint16 | Taille de la palette intégrée (en bytes)
24     | uint8  | Flag hasPalette (≠0 si palette présente)
```

**Référence**: `robot_decoder.cpp` lignes 555-568
```cpp
const uint16 paletteSize = _stream->readUint16();  // Offset 16-17
const bool hasPalette = (bool)_stream->readByte(); // Offset 24
```

### Lecture de la Palette

La palette est lue après l'en-tête principal du Robot, mais avant les index de taille de frames:

**Référence**: `robot_decoder.cpp` lignes 458-468
```cpp
void RobotDecoder::initVideo(..., const bool hasPalette, const uint16 paletteSize) {
    // ...
    
    if (hasPalette) {
        _stream->read(_rawPalette, paletteSize);
    } else {
        _stream->seek(paletteSize, SEEK_CUR);  // Sauter l'espace réservé
    }
}
```

**Taille du buffer**: La constante `kRawPaletteSize = 1200` bytes est utilisée pour allouer le buffer de palette brute.

**Référence**: `robot_decoder.h` ligne 519
```cpp
/**
 * The size of a hunk palette in the Robot stream.
 */
kRawPaletteSize = 1200,
```

## Format HunkPalette

### Structure Générale

Une HunkPalette SCI32 suit cette structure hiérarchique:

```
[En-tête HunkPalette]
    ↓
[Table d'offsets des palettes]
    ↓
[Données de Palette 1..N]
```

**Note**: Dans les jeux SCI32 (Robot v5/v6), il n'y a **toujours qu'une seule palette** par HunkPalette.

### En-tête HunkPalette (13 bytes)

```
Offset | Taille | Description
-------|--------|------------------------------------------
0-9    | 10 B   | Données d'en-tête (format variable)
10     | uint8  | Nombre de palettes (toujours 1 pour SCI32)
11-12  | 2 B    | Réservé/Inutilisé
```

**Référence**: `palette32.h` lignes 77-82
```cpp
enum {
    kNumPaletteEntriesOffset = 10,  // Offset du nombre de palettes
    kHunkPaletteHeaderSize = 13      // Taille totale de l'en-tête
};
```

**Important**: La taille d'en-tête stockée dans certaines palettes peut être incorrecte (0 dans KQ7 2.00b, 14 ailleurs), mais la taille réelle utilisée est **toujours 13 bytes**.

**Référence**: `palette32.cpp` lignes 41-46
```cpp
HunkPalette::HunkPalette(const SciSpan<const byte> &rawPalette) :
    _version(0),
    // The header size in palettes is garbage. In at least KQ7 2.00b and Phant1,
    // the 999.pal sets this value to 0. In most other palettes it is set to 14,
    // but the *actual* size of the header structure used in SSCI is 13, which
    // is reflected by `kHunkPaletteHeaderSize`.
    _numPalettes(rawPalette.getUint8At(kNumPaletteEntriesOffset)),
```

### Table d'Offsets (2 bytes par palette)

Après l'en-tête, il y a 2 bytes de padding, puis la table d'offsets:

```
Offset | Taille | Description
-------|--------|------------------------------------------
13-14  | 2 B    | Bytes de remplissage (slack bytes)
15-16  | uint16 | Offset de la première palette (relatif au début du HunkPalette)
```

Pour une seule palette (cas standard SCI32), l'offset pointe généralement vers l'offset 15 + 2 = 17.

## En-tête d'Entrée de Palette (22 bytes)

Chaque palette dans le HunkPalette commence par un en-tête de 22 bytes:

```
Offset | Taille | Description
-------|--------|------------------------------------------
0-9    | 10 B   | Données d'en-tête (format interne)
10     | uint8  | Couleur de départ (startColor)
11-13  | 3 B    | Données intermédiaires
14-15  | uint16 | Nombre de couleurs (numColors)
16     | uint8  | Flag "used" par défaut
17     | uint8  | Flag sharedUsed (partage du flag "used")
18-21  | uint32 | Version de la palette
```

**Référence**: `palette32.h` lignes 88-110
```cpp
enum {
    kEntryStartColorOffset = 10,
    kEntryNumColorsOffset = 14,
    kEntryUsedOffset = 16,
    kEntrySharedUsedOffset = 17,
    kEntryVersionOffset = 18
};
```

### Structure EntryHeader

**Référence**: `palette32.h` lignes 119-146
```cpp
struct EntryHeader {
    /**
     * The start color.
     */
    uint8 startColor;

    /**
     * The number of palette colors in this entry.
     */
    uint16 numColors;

    /**
     * The default `used` flag.
     */
    bool used;

    /**
     * Whether or not all palette entries share the same `used` value in
     * `defaultFlag`.
     */
    bool sharedUsed;

    /**
     * The palette version.
     */
    uint32 version;
};
```

### Lecture de l'En-tête

**Référence**: `palette32.cpp` lignes 93-105
```cpp
const HunkPalette::EntryHeader HunkPalette::getEntryHeader() const {
    const SciSpan<const byte> data(getPalPointer());

    EntryHeader header;
    header.startColor = data.getUint8At(kEntryStartColorOffset);
    header.numColors = data.getUint16SEAt(kEntryNumColorsOffset);
    header.used = data.getUint8At(kEntryUsedOffset);
    header.sharedUsed = data.getUint8At(kEntrySharedUsedOffset);
    header.version = data.getUint32SEAt(kEntryVersionOffset);

    return header;
}
```

**Note sur l'Endianness**: Les données sont lues avec `getUint16SEAt` et `getUint32SEAt`, ce qui signifie que l'endianness dépend du format du fichier Robot (déterminé à l'ouverture).

## Données de Palette (Après offset 22)

Les données de palette commencent immédiatement après l'en-tête d'entrée (offset 22 dans l'entrée de palette).

### Deux Formats Possibles

#### Format 1: sharedUsed = true (3 bytes par couleur)

Toutes les couleurs partagent le même flag "used" défini dans l'en-tête.

```
Pour chaque couleur (startColor → startColor + numColors - 1):
    Offset | Taille | Description
    -------|--------|------------------------------------------
    +0     | uint8  | Composante Rouge (0-255)
    +1     | uint8  | Composante Verte (0-255)
    +2     | uint8  | Composante Bleue (0-255)
```

**Taille totale**: `numColors * 3` bytes

#### Format 2: sharedUsed = false (4 bytes par couleur)

Chaque couleur a son propre flag "used".

```
Pour chaque couleur (startColor → startColor + numColors - 1):
    Offset | Taille | Description
    -------|--------|------------------------------------------
    +0     | uint8  | Flag "used" (0 = non utilisée, 1 = utilisée)
    +1     | uint8  | Composante Rouge (0-255)
    +2     | uint8  | Composante Verte (0-255)
    +3     | uint8  | Composante Bleue (0-255)
```

**Taille totale**: `numColors * 4` bytes

### Décodage des Données

**Référence**: `palette32.cpp` lignes 127-149
```cpp
const Palette HunkPalette::toPalette() const {
    Palette outPalette;
    // Initialisation...

    if (_numPalettes) {
        const EntryHeader header = getEntryHeader();
        const uint32 dataSize = header.numColors * (/* RGB */ 3 + (header.sharedUsed ? 0 : 1));
        const byte *data = getPalPointer().getUnsafeDataAt(kEntryHeaderSize, dataSize);

        const int16 end = header.startColor + header.numColors;
        assert(end <= 256);

        if (header.sharedUsed) {
            for (int16 i = header.startColor; i < end; ++i) {
                outPalette.colors[i].used = header.used;
                outPalette.colors[i].r = *data++;
                outPalette.colors[i].g = *data++;
                outPalette.colors[i].b = *data++;
            }
        } else {
            for (int16 i = header.startColor; i < end; ++i) {
                outPalette.colors[i].used = *data++;
                outPalette.colors[i].r = *data++;
                outPalette.colors[i].g = *data++;
                outPalette.colors[i].b = *data++;
            }
        }
    }

    return outPalette;
}
```

## Calcul de la Taille Totale

La taille totale d'une HunkPalette peut être calculée avec la formule suivante:

**Référence**: `palette32.h` lignes 46-51
```cpp
static uint32 calculateHunkPaletteSize(const uint16 numIndexes = 256, const bool sharedUsed = true) {
    const int numPalettes = 1;
    return kHunkPaletteHeaderSize +
        /* slack bytes between hunk header & palette offset table */ 2 +
        /* palette offset table */ 2 * numPalettes +
        /* palette data */ (kEntryHeaderSize + numIndexes * (/* RGB */ 3 + !sharedUsed)) * numPalettes;
}
```

### Exemple de Calcul

Pour une palette complète de 256 couleurs avec sharedUsed = true:

```
Taille = 13 (en-tête HunkPalette)
       + 2  (slack bytes)
       + 2  (offset table, 1 palette)
       + 22 (en-tête d'entrée)
       + (256 * 3) (données RGB)
       = 13 + 2 + 2 + 22 + 768
       = 807 bytes
```

Pour une palette complète avec sharedUsed = false:

```
Taille = 13 + 2 + 2 + 22 + (256 * 4)
       = 13 + 2 + 2 + 22 + 1024
       = 1063 bytes
```

**Note**: Le buffer alloué dans Robot est de 1200 bytes (`kRawPaletteSize`), ce qui est suffisant pour les deux cas.

## Utilisation dans les Robots

### Copie vers les Cels

Lorsque la palette est utilisée (flag `usePalette` activé), elle est copiée dans chaque bitmap de cel:

**Référence**: `robot_decoder.cpp` lignes 1551-1559
```cpp
uint32 RobotDecoder::createCel5(const byte *rawVideoData, const int16 screenItemIndex, const bool usePalette) {
    // ... décompression des données de cel ...

    if (usePalette) {
        Common::copy(_rawPalette, _rawPalette + kRawPaletteSize, bitmap.getHunkPalette());
    }

    return kCelHeaderSize + dataSize;
}
```

### Allocation de Mémoire pour les Bitmaps

Lors de l'allocation des bitmaps, l'espace pour la palette est réservé:

**Référence**: `robot_decoder.cpp` lignes 1595-1612
```cpp
void RobotDecoder::preallocateCelMemory(const byte *rawVideoData, const int16 numCels) {
    // ...
    _segMan->allocateBitmap(&celHandle.bitmapId, 
                           celWidth, celHeight, 
                           255,  // skipColor
                           0, 0, 
                           _xResolution, _yResolution, 
                           kRawPaletteSize,  // ← Taille de la palette
                           remap, false);
    // ...
}
```

## Structure de Données Palette en Mémoire

Une fois décodée, la palette est stockée dans une structure `Palette`:

```cpp
struct Palette {
    uint8 mapping[256];         // Mapping des couleurs
    uint32 timestamp;           // Horodatage
    struct {
        bool used;              // Couleur utilisée?
        uint8 r, g, b;         // Composantes RGB
    } colors[256];
    uint8 intensity[256];       // Intensité de chaque couleur
};
```

**Initialisation**: `palette32.cpp` lignes 107-121
```cpp
const Palette HunkPalette::toPalette() const {
    Palette outPalette;

    // Set outPalette structures to 0
    for (int16 i = 0; i < ARRAYSIZE(outPalette.mapping); ++i) {
        outPalette.mapping[i] = 0;
    }
    outPalette.timestamp = 0;
    for (int16 i = 0; i < ARRAYSIZE(outPalette.colors); ++i) {
        outPalette.colors[i].used = false;
        outPalette.colors[i].r = 0;
        outPalette.colors[i].g = 0;
        outPalette.colors[i].b = 0;
    }
    for (int16 i = 0; i < ARRAYSIZE(outPalette.intensity); ++i) {
        outPalette.intensity[i] = 0;
    }
    // ... décodage ...
}
```

## Offset de la Palette dans les Bitmaps

Dans les bitmaps SciBitmap, la palette (HunkPalette) est stockée immédiatement après les données de pixels:

**Référence**: `robot_decoder.cpp` ligne 1514
```cpp
assert(bitmap.getHunkPaletteOffset() == (uint32)bitmap.getWidth() * bitmap.getHeight() + SciBitmap::getBitmapHeaderSize());
```

**Formule**:
```
HunkPaletteOffset = (Width * Height) + BitmapHeaderSize
```

Où `BitmapHeaderSize` est défini dans `segment.h` (structure SciBitmap).

## Gestion des Versions

Chaque palette a un numéro de version utilisé pour éviter de soumettre plusieurs fois la même palette:

**Référence**: `palette32.cpp` lignes 76-92
```cpp
void HunkPalette::setVersion(const uint32 version) const {
    if (_numPalettes != _data.getUint8At(kNumPaletteEntriesOffset)) {
        error("Invalid HunkPalette");
    }

    if (_numPalettes) {
        const EntryHeader header = getEntryHeader();
        if (header.version != _version) {
            error("Invalid HunkPalette");
        }

        byte *palette = const_cast<byte *>(getPalPointer().getUnsafeDataAt(kEntryVersionOffset, sizeof(uint32)));
        WRITE_SCI11ENDIAN_UINT32(palette, version);
        _version = version;
    }
}
```

## Palettes Partielles

Les HunkPalettes peuvent définir des palettes partielles (moins de 256 couleurs) grâce aux champs `startColor` et `numColors`:

- **startColor**: Premier index de couleur à modifier (0-255)
- **numColors**: Nombre de couleurs à partir de startColor

**Exemple**:
```
startColor = 100
numColors = 50

→ Modifie les couleurs d'index 100 à 149
→ Les couleurs 0-99 et 150-255 restent inchangées
```

## Processus Complet de Décodage

### Étape 1: Lecture de l'En-tête Robot

```cpp
const uint16 paletteSize = _stream->readUint16();  // Offset 16-17
const bool hasPalette = (bool)_stream->readByte(); // Offset 24
```

### Étape 2: Lecture de la Palette Brute

```cpp
if (hasPalette) {
    _stream->read(_rawPalette, paletteSize);  // Lecture de 'paletteSize' bytes
}
```

### Étape 3: Validation du HunkPalette

```cpp
_numPalettes = rawPalette.getUint8At(kNumPaletteEntriesOffset);  // Offset 10
assert(_numPalettes == 0 || _numPalettes == 1);  // SCI32: toujours 0 ou 1
```

### Étape 4: Lecture de l'En-tête d'Entrée

```cpp
const byte *entryData = rawPalette + kHunkPaletteHeaderSize + 2 + (2 * numPalettes);

EntryHeader header;
header.startColor = entryData[10];
header.numColors = READ_UINT16(entryData + 14);
header.used = entryData[16];
header.sharedUsed = entryData[17];
header.version = READ_UINT32(entryData + 18);
```

### Étape 5: Décodage des Couleurs

```cpp
const byte *colorData = entryData + kEntryHeaderSize;  // +22

if (header.sharedUsed) {
    // Format 3 bytes/couleur
    for (int i = 0; i < header.numColors; i++) {
        int index = header.startColor + i;
        palette.colors[index].used = header.used;
        palette.colors[index].r = *colorData++;
        palette.colors[index].g = *colorData++;
        palette.colors[index].b = *colorData++;
    }
} else {
    // Format 4 bytes/couleur
    for (int i = 0; i < header.numColors; i++) {
        int index = header.startColor + i;
        palette.colors[index].used = *colorData++;
        palette.colors[index].r = *colorData++;
        palette.colors[index].g = *colorData++;
        palette.colors[index].b = *colorData++;
    }
}
```

### Étape 6: Copie vers les Bitmaps

```cpp
if (usePalette) {
    byte *bitmapPalette = bitmap.getPixels() + (width * height);
    memcpy(bitmapPalette, _rawPalette, kRawPaletteSize);
}
```

## Cas Particuliers et Notes

### Palettes Vides

Si `_numPalettes == 0`, la HunkPalette est vide et aucune donnée de palette n'est présente. Dans ce cas:

**Référence**: `palette32.cpp` lignes 50-52
```cpp
if (_numPalettes) {
    _data = rawPalette;
    _version = getEntryHeader().version;
}
```

### Endianness

L'endianness des données de palette dépend de la plateforme du fichier Robot:
- **PC (x86)**: Little-endian
- **Mac (68k/PPC)**: Big-endian

**Référence**: `robot_decoder.cpp` lignes 388-395
```cpp
const uint16 version = stream->readUint16BE();
const bool bigEndian = (0 < version && version <= 0x00ff);

_stream = new Common::SeekableReadStreamEndianWrapper(stream, bigEndian, DisposeAfterUse::YES);
```

Les lectures utilisent les macros `READ_SCI11ENDIAN_UINT16` et `READ_SCI11ENDIAN_UINT32` qui s'adaptent automatiquement.

### Palettes Corrompues

Certains fichiers Robot contiennent des en-têtes de palette incorrects:

**Exemple KQ7 2.00b et Phantasmagoria 1**:
- Le fichier `999.pal` a une taille d'en-tête de **0** au lieu de 14
- ScummVM ignore cette valeur et utilise toujours `kHunkPaletteHeaderSize = 13`

## Exemple Pratique

### Palette Complète (256 couleurs, sharedUsed = true)

```
Offset | Données                    | Description
-------|----------------------------|----------------------------------
0-9    | [En-tête HunkPalette]      | Données internes
10     | 0x01                       | numPalettes = 1
11-12  | 0x00 0x00                  | Réservé
13-14  | 0x00 0x00                  | Slack bytes
15-16  | 0x11 0x00                  | Offset palette = 17 (little-endian)
17-26  | [En-tête entrée interne]   | Données internes
27     | 0x00                       | startColor = 0
28-30  | [...]                      | Données intermédiaires
31-32  | 0x00 0x01                  | numColors = 256 (little-endian)
33     | 0x01                       | used = true
34     | 0x01                       | sharedUsed = true
35-38  | 0x01 0x00 0x00 0x00        | version = 1
39     | 0x00                       | Couleur 0: R = 0
40     | 0x00                       | Couleur 0: G = 0
41     | 0x00                       | Couleur 0: B = 0
42     | 0xFF                       | Couleur 1: R = 255
43     | 0xFF                       | Couleur 1: G = 255
44     | 0xFF                       | Couleur 1: B = 255
...    | ...                        | 254 couleurs restantes
806    | [Dernière composante B]    | Fin des données
```

**Taille totale**: 807 bytes

## Références du Code Source

### Fichiers Principaux

1. **engines/sci/video/robot_decoder.h**: Définitions des constantes
   - Lignes 497-530: Énumération des constantes dont `kRawPaletteSize`

2. **engines/sci/video/robot_decoder.cpp**: Implémentation
   - Lignes 350-362: Constructeur RobotDecoder
   - Lignes 458-476: Fonction `initVideo` (lecture de la palette)
   - Lignes 1551-1559: Fonction `createCel5` (copie de la palette)
   - Lignes 1595-1612: Fonction `preallocateCelMemory` (allocation)

3. **engines/sci/graphics/palette32.h**: Structure HunkPalette
   - Lignes 37-176: Définition complète de la classe HunkPalette
   - Lignes 62-88: Énumérations des offsets
   - Lignes 119-146: Structure EntryHeader

4. **engines/sci/graphics/palette32.cpp**: Implémentation HunkPalette
   - Lignes 38-52: Constructeur HunkPalette
   - Lignes 54-74: Fonction `write` (écriture)
   - Lignes 76-92: Fonction `setVersion`
   - Lignes 93-105: Fonction `getEntryHeader`
   - Lignes 107-150: Fonction `toPalette` (conversion)

## Résumé

Le décodage d'une palette Robot suit ce processus:

1. **Lecture de l'en-tête Robot** pour obtenir `paletteSize` et `hasPalette`
2. **Lecture des données brutes** de la palette (jusqu'à 1200 bytes)
3. **Validation** du nombre de palettes (toujours 0 ou 1)
4. **Décodage de l'en-tête d'entrée** pour obtenir startColor, numColors, used, sharedUsed, version
5. **Décodage des couleurs** selon le format (3 ou 4 bytes par couleur)
6. **Copie vers les bitmaps** si nécessaire

La palette résultante est une structure de 256 entrées RGB avec flags "used", stockée soit en format brut (1200 bytes), soit en format décodé (structure Palette).
