# Robot Format - Palette Remapping (Versions 5 et 6)

## Vue d'ensemble

Le **palette remapping** est une fonctionnalité disponible dans les versions 5 et 6 du format Robot. Cette technique permet aux vidéos Robot de participer au système de remappage de couleurs du moteur SCI.

- **Robot v5** (Phantasmagoria, KQ7, etc.) : Utilise **GfxRemap** (SCI16) si la ressource Vocab 184 est présente
- **Robot v6** (RAMA) : Utilise **GfxRemap32** (SCI32) toujours disponible

> **Note importante** : Les informations de remappage ne sont **PAS stockées dans le fichier Robot lui-même**. Le remappage est géré entièrement par le moteur SCI (SCI16 pour v5, SCI32 pour v6) au moment de l'exécution.

---

## Historique des versions Robot

### Versions et jeux concernés

| Version | Jeux utilisant cette version | Palette Remapping |
|---------|------------------------------|-------------------|
| v4 | PQ:SWAT demo | Non |
| v5 | KQ7 DOS, Phantasmagoria, PQ:SWAT, Lighthouse | **Oui (SCI16/GfxRemap)** |
| v6 | RAMA | **Oui (SCI32/GfxRemap32)** |

---

## Concept du palette remapping

### Définition

Le palette remapping est un système qui permet de **transformer dynamiquement** les couleurs d'une image en remplaçant certaines entrées de palette par d'autres valeurs, calculées en temps réel par le moteur graphique.

### Spécificité Robot v5 vs v6

**Robot v5** (Phantasmagoria, KQ7, etc.) :
- Utilise `GfxRemap` (SCI16) si la ressource Vocab 184 est présente
- Remappage plus simple : byPercent et byRange uniquement
- Kernel functions : `kRemapColors` et `kRemapColorsKawa`

**Robot v6** (RAMA) :
- Utilise `GfxRemap32` (SCI32)
- Remappage avancé : byPercent, byRange, toGray, toPercentGray, blockRange
- Peut dessiner des "remap pixels" directement dans les bitmaps
- Kernel functions : `kRemapColors32`, `kRemapColorsByPercent`, `kRemapColorsToGray`, etc.

D'après les commentaires du code source ScummVM (`robot_decoder.h`, lignes 36-58) :

```
// Unlike traditional AV formats, Robot videos almost always require playback
// within the game engine because certain information (like the resolution of
// the Robot coordinates and the background for the video) is dependent on data
// that does not exist within the Robot file itself. In version 6, robots could
// also participate in palette remapping by drawing remap pixels, and the
// information for processing these pixels is also not stored within the Robot
// file.
```

**Traduction et analyse** :
- Les vidéos Robot nécessitent le moteur de jeu pour être lues correctement
- Les informations de résolution et de fond ne sont pas dans le fichier Robot
- **En version 6** : les robots peuvent dessiner des "remap pixels" (pixels remappés)
- **Les informations de remappage ne sont PAS stockées dans le fichier Robot** (ni v5 ni v6)
- **En version 5** : le remappage via GfxRemap (SCI16) est optionnel (dépend de Vocab 184)

---

## Système de remappage SCI

Le moteur SCI utilise **deux systèmes distincts** de remappage de palette :

### 1. GfxRemap (SCI16) - Pour Robot v5

Utilisé dans les jeux SCI11+ utilisant Robot v5 :
- **QFG4 Demo** (SCI1.1) : utilisation principale
- **Phantasmagoria** (SCI2) : potentiellement disponible
- **KQ7**, **PQ:SWAT**, **Lighthouse** : potentiellement disponible
- Jeux ayant la ressource **Vocab 184**

Source : `engines/sci/graphics/remap.cpp` et `remap.h`

**Classe** : `GfxRemap`

**Types de remappage disponibles** (SCI16) :
```cpp
enum ColorRemappingType {
    kRemapNone = 0,
    kRemapByRange = 1,    // Remappage par plage
    kRemapByPercent = 2   // Remappage par pourcentage
};
```

**Fonctions** :
- `setRemappingPercent(byte color, byte percent)` : ajuste l'intensité
- `setRemappingRange(byte color, byte from, byte to, byte base)` : plage de couleurs
- `remapColor(byte remappedColor, byte screenColor)` : applique le remappage
- `resetRemapping()` : réinitialise

**Initialisation** (d'après `sci.cpp` lignes 598-619) :
```cpp
void SciEngine::initGraphics() {
    #ifdef ENABLE_SCI32
    if (getSciVersion() >= SCI_VERSION_2) {
        _gfxPalette32 = new GfxPalette32(_resMan);
        _gfxRemap32 = new GfxRemap32();
    } else {
    #endif
        _gfxPalette16 = new GfxPalette(_resMan, _gfxScreen);
        // GfxRemap créé seulement si QFG4 demo OU ressource Vocab 184 existe
        if (getGameId() == GID_QFG4DEMO || 
            _resMan->testResource(ResourceId(kResourceTypeVocab, 184)))
            _gfxRemap16 = new GfxRemap(_gfxPalette16);
    #ifdef ENABLE_SCI32
    }
    #endif
}
```

**Kernel Functions** (d'après `kernel_tables.h` ligne 781) :
```cpp
{ MAP_CALL(RemapColors),     SIG_SCI11, SIGFOR_ALL, "i(i)(i)(i)(i)", NULL, NULL },
{ MAP_CALL(RemapColorsKawa), SIG_SCI11, SIGFOR_ALL, "i(i)(i)(i)(i)(i)", NULL, NULL },
```

**Utilisation dans QFG4 Demo** (d'après `kgraphics.cpp` lignes 1335-1360) :
```cpp
// Early variant of the SCI32 kRemapColors kernel function, 
// used in the demo of QFG4
reg_t kRemapColors(EngineState *s, int argc, reg_t *argv) {
    uint16 operation = argv[0].toUint16();

    switch (operation) {
    case 0: { // remap by percent
        uint16 percent = argv[1].toUint16();
        g_sci->_gfxRemap16->resetRemapping();
        g_sci->_gfxRemap16->setRemappingPercent(254, percent);
        }
        break;
    case 1: { // remap by range
        uint16 from = argv[1].toUint16();
        uint16 to = argv[2].toUint16();
        uint16 base = argv[3].toUint16();
        g_sci->_gfxRemap16->resetRemapping();
        g_sci->_gfxRemap16->setRemappingRange(254, from, to, base);
        }
        break;
    case 2: // turn remapping off (unused)
        error("Unused subop kRemapColors(2) has been called");
        break;
    }
    return s->r_acc;
}
```

### 2. GfxRemap32 (SCI32) - Pour Robot v6

Utilisé exclusivement dans **RAMA** (Robot v6) et autres jeux SCI32 avancés.

Source : `engines/sci/graphics/remap32.cpp`

**Types de remappage disponibles** (SCI32) :
- `remapByRange()` : remappage par plage de couleurs
- `remapByPercent()` : remappage par pourcentage d'intensité
- `remapToGray()` : conversion en niveaux de gris
- `remapToPercentGray()` : mélange gris + pourcentage
- `blockRange()` : blocage d'une plage de couleurs

**Fonctionnement** :
```cpp
// engines/sci/graphics/remap32.cpp lignes 311-464
void GfxRemap32::remapByPercent(const uint8 color, const int16 percent) {
    // Calcule une nouvelle couleur basée sur le pourcentage
    // pour chaque pixel de la couleur spécifiée
}

void GfxRemap32::remapToGray(const uint8 color, const int8 gray) {
    // Convertit une couleur en niveau de gris
}
```

#### 2. SingleRemap (classe de support)
Source : `engines/sci/graphics/remap32.cpp` (lignes 30-46)

```cpp
void SingleRemap::reset() {
    _lastPercent = 100;
    _lastGray = 0;

    const uint8 remapStartColor = g_sci->_gfxRemap32->getStartColor();
    const Palette &currentPalette = g_sci->_gfxPalette32->getCurrentPalette();
    
    for (uint i = 0; i < remapStartColor; ++i) {
        const Color &color = currentPalette.colors[i];
        _remapColors[i] = i;              // Couleur remappée
        _originalColors[i] = color;        // Couleur d'origine
        _idealColors[i] = color;           // Couleur idéale cible
        _matchDistances[i] = 0;            // Distance de correspondance
    }
}
```

**Données maintenues** :
- `_remapColors[]` : table de correspondance (index source → index destination)
- `_originalColors[]` : palette d'origine
- `_idealColors[]` : palette cible souhaitée
- `_originalColorsChanged[]` : flags de modification
- `_idealColorsChanged[]` : flags de modification de cible
- `_matchDistances[]` : distances de correspondance pour chaque couleur

---

## Intégration Robot v6

### Flux de rendu

D'après `robot_decoder.cpp` (ligne ~1551-1559) :

```cpp
uint32 RobotDecoder::createCel5(const byte *rawVideoData, 
                                 const int16 screenItemIndex, 
                                 const bool usePalette) {
    // Décodage du cel...
    
    if (usePalette) {
        // Copie la palette brute dans le hunk palette du bitmap
        Common::copy(_rawPalette, _rawPalette + kRawPaletteSize, 
                     bitmap.getHunkPalette());
    }
    
    return kCelHeaderSize + dataSize;
}
```

### Application du remappage

Le remappage s'applique **après** le décodage de l'image, au moment du rendu :

```cpp
// engines/sci/graphics/palette32.cpp (lignes 463-485)
bool GfxPalette32::updateForFrame() {
    applyAll();
    _needsUpdate = false;
    // Application de TOUTES les tables de remappage
    return g_sci->_gfxRemap32->remapAllTables(_nextPalette != _currentPalette);
}
```

### Pixels remappables

D'après `engines/sci/graphics/view.cpp` (lignes 777-786) :

```cpp
byte GfxView::getMappedColor(byte color, uint16 scaleSignal, 
                              const Palette *palette, int x2, int y2) {
    byte outputColor = palette->mapping[color];
    
    // SCI16 remapping (QFG4 demo)
    if (g_sci->_gfxRemap16 && g_sci->_gfxRemap16->isRemapped(outputColor))
        outputColor = g_sci->_gfxRemap16->remapColor(outputColor, 
                                                     _screen->getVisual(x2, y2));
    
    // SCI11+ remapping (Catdate)
    if ((scaleSignal & 0xFF00) && g_sci->_gfxRemap16 && 
        _resMan->testResource(ResourceId(kResourceTypeVocab, 184))) {
        if ((scaleSignal >> 8) == 1)      // all black
            outputColor = 0;
        else if ((scaleSignal >> 8) == 2) // darken
            // ... application du remappage
    }
    
    return outputColor;
}
```

---

## Détails techniques

### Palette brute (HunkPalette)

**Taille** : 1200 octets (`kRawPaletteSize = 1200`)

D'après `robot_decoder.h` (lignes 497-530) :

```cpp
enum {
    /**
     * The size of a hunk palette in the Robot stream.
     */
    kRawPaletteSize = 1200,
};
```

**Structure de la HunkPalette** :
- Format SCI32 standard
- 3 octets par couleur (R, G, B) pour 400 entrées maximum
- Utilisée uniquement si le flag `usePalette` est activé dans l'en-tête Robot

### Bitmap et remappage

D'après `engines/sci/engine/segment.h` (lignes 1154-1173) :

```cpp
class SciBitmap : public Common::Serializable {
    // ...
    
    void setPalette(const Palette &palette) {
        byte *paletteData = getHunkPalette();
        if (paletteData != nullptr) {
            SciSpan<byte> paletteSpan(paletteData, 
                                      getRawSize() - getHunkPaletteOffset());
            HunkPalette::write(paletteSpan, palette);
        }
    }
    
    void applyRemap(SciArray &clut) {
        const int length = getWidth() * getHeight();
        uint8 *pixel = getPixels();
        for (int i = 0; i < length; ++i) {
            const int16 color = clut.getAsInt16(*pixel);
            assert(color >= 0 && color <= 255);
            *pixel++ = (uint8)color;  // Remplace directement le pixel
        }
    }
};
```

**Fonctionnement de `applyRemap()`** :
1. Parcourt tous les pixels de l'image
2. Utilise une CLUT (Color Look-Up Table) pour transformer chaque valeur
3. Remplace directement la valeur du pixel dans le buffer

---

## Exemples d'utilisation

### Exemple 1 : Remappage par pourcentage

**Cas d'usage** : Assombrir une scène lors d'une transition jour/nuit

```cpp
// Remapper la couleur 50 à 75% de son intensité
g_sci->_gfxRemap32->remapByPercent(50, 75);
```

**Effet** :
- Tous les pixels de couleur 50 seront remplacés par une version à 75% d'intensité
- Appliqué en temps réel pendant le rendu du Robot

### Exemple 2 : Conversion en niveaux de gris

**Cas d'usage** : Flashback en noir et blanc

```cpp
// Convertir la couleur 100 en niveau de gris (128 = gris moyen)
g_sci->_gfxRemap32->remapToGray(100, 128);
```

### Exemple 3 : Inversion de palette (RAMA)

D'après `engines/sci/graphics/palette32.cpp` (lignes 748-762) :

```cpp
void GfxPalette32::kernelPalVarySet(const GuiResourceId paletteId, 
                                     const int16 percent, 
                                     const int32 ticks, 
                                     const int16 fromColor, 
                                     const int16 toColor) {
    Palette palette;

    if (getSciVersion() == SCI_VERSION_3 && paletteId == 0xFFFF) {
        palette = _currentPalette;
        assert(fromColor >= 0 && fromColor < 256);
        assert(toColor >= 0 && toColor < 256);
        
        // Inversion de palette dans RAMA (room 6201)
        // Note: toColor est EXCLUS de l'inversion
        for (int i = fromColor; i < toColor; ++i) {
            palette.colors[i].r = ~palette.colors[i].r;  // Inversion binaire
            palette.colors[i].g = ~palette.colors[i].g;
            palette.colors[i].b = ~palette.colors[i].b;
        }
    }
    // ...
}
```

**Utilisation dans RAMA** :
- Room 6201 : inversion partielle de la palette pour effets d'interface
- Les pixels Robot v6 peuvent participer à cette inversion

---

## Différences avec d'autres formats

### Comparaison avec d'autres moteurs

| Moteur | Format | Transparence | Remappage palette |
|--------|--------|--------------|-------------------|
| **SCI16** | Robot v5 | Non | **Oui (GfxRemap)** si Vocab 184 |
| **SCI32** | Robot v6 | Non | **Oui (GfxRemap32)** |
| Tinsel | PICT | Oui (0 ou 255) | Non |
| Private | BMP | Oui (250) | Oui (distance RGB) |
| Neverhood | Custom | Non | Non |
| SCUMM | AKOS | Non | Oui (AKPL block) |

### Robot vs autres formats SCI

```
┌─────────────────────────────────────────────────────────┐
│                    Format Robot                          │
├─────────────────────────────────────────────────────────┤
│  v4 (PQ:SWAT demo)    : Pas de remappage                │
│  v5 (KQ7, Phant...)   : Remappage optionnel (GfxRemap)  │
│  v6 (RAMA)            : Remappage par GfxRemap32        │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│              Palette dans Robot v5/v6                    │
├─────────────────────────────────────────────────────────┤
│  • Palette optionnelle (flag dans header)               │
│  • Taille : 1200 octets (HunkPalette SCI32)             │
│  • Format : R,G,B pour chaque entrée                    │
│  • Stockée AVANT l'index des frames                     │
│  • Utilisée telle quelle (v5) ou remappée (v6)          │
└─────────────────────────────────────────────────────────┘
```

---

## Limitations et contraintes

### Ce qui N'EST PAS dans le fichier Robot

1. **Tables de remappage** : calculées par le moteur SCI32
2. **Résolution de l'écran** : déduite du plan de rendu
3. **Couleurs de transparence** : non supportées (contrairement à d'autres formats)
4. **Informations de fond** : gérées par le moteur graphique
5. **Plages de couleurs remappables** : définies par le script du jeu

### Ce qui EST dans le fichier Robot

1. **Palette brute** (si flag activé) : 1200 octets
2. **Pixels avec valeurs de palette** : indices 0-255
3. **Flag d'utilisation de palette** : octet 24 de l'en-tête
4. **Données vidéo compressées** : bitmaps LZS

### Contraintes de rendu

D'après les commentaires ScummVM :

```
// robot_decoder.h lignes 36-58
// Unlike traditional AV formats, Robot videos almost always require playback
// within the game engine because certain information (like the resolution of
// the Robot coordinates and the background for the video) is dependent on data
// that does not exist within the Robot file itself.
```

**Implications** :
- **Impossible de lire un Robot v6 hors moteur SCI32**
- Le remappage nécessite le contexte d'exécution du jeu
- Les pixels remappés n'ont de sens qu'avec les tables de remappage actives
- Un extracteur externe (comme notre projet) ne peut pas reproduire le remappage

---

## Architecture technique complète

### Pipeline de rendu Robot v6

```
┌──────────────────────────────────────────────────────────────┐
│                    1. CHARGEMENT FICHIER                      │
│  • Lecture en-tête Robot (offset 0)                          │
│  • Vérification version == 6                                 │
│  • Lecture flag usePalette (offset 24)                       │
│  • Lecture palette brute si flag = 1 (1200 octets)           │
└──────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────┐
│                    2. DÉCODAGE FRAME                          │
│  • Lecture données vidéo compressées                         │
│  • Décompression LZS → bitmap pixels                         │
│  • Copie palette brute → HunkPalette du bitmap               │
└──────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────┐
│              3. PRÉPARATION PALETTE (MOTEUR SCI32)            │
│  • GfxPalette32::submit(HunkPalette)                         │
│  • Copie vers _sourcePalette                                 │
│  • Application des variations de palette actives             │
└──────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────┐
│              4. APPLICATION REMAPPAGE (MOTEUR SCI32)          │
│  • GfxRemap32::remapAllTables()                              │
│  • Pour chaque table de remappage active :                   │
│    - remapByPercent(), remapToGray(), etc.                   │
│  • Création table de correspondance : oldIndex → newIndex    │
└──────────────────────────────────────────────────────────────┘
                            ↓
┌──────────────────────────────────────────────────────────────┐
│                    5. RENDU PIXELS                            │
│  • Pour chaque pixel du bitmap :                             │
│    - Lecture valeur palette (0-255)                          │
│    - Consultation table de remappage                         │
│    - Remplacement par nouvelle valeur si remappé             │
│    - Affichage couleur finale à l'écran                      │
└──────────────────────────────────────────────────────────────┘
```

### Classes impliquées

```cpp
// Hiérarchie des classes de remappage SCI32

GfxRemap32 (remap32.cpp)
├── SingleRemap (remap32.cpp)
│   ├── _remapColors[256]        // Table oldIndex → newIndex
│   ├── _originalColors[256]      // Palette d'origine
│   ├── _idealColors[256]         // Palette cible
│   └── _matchDistances[256]      // Distances de correspondance
│
├── remapByPercent(color, percent)
├── remapByRange(color, from, to, delta)
├── remapToGray(color, gray)
├── remapToPercentGray(color, gray, percent)
├── blockRange(from, count)
└── remapAllTables(paletteUpdated) → applique toutes les tables

GfxPalette32 (palette32.cpp)
├── submit(HunkPalette)           // Soumet une palette pour remappage
├── updateForFrame()               // Met à jour pour la frame courante
├── updateFFrame()                 // Met à jour sans remappage
└── kernelPalVarySet(...)         // Variation de palette (RAMA)

SciBitmap (segment.h)
├── getHunkPalette()              // Retourne pointeur palette du bitmap
├── setPalette(Palette)           // Définit la palette
└── applyRemap(SciArray &clut)    // Applique remappage direct sur pixels
```

---

## Exemples de code ScummVM

### Initialisation du remappage

```cpp
// engines/sci/graphics/remap32.cpp lignes 30-46
void SingleRemap::reset() {
    _lastPercent = 100;
    _lastGray = 0;

    const uint8 remapStartColor = g_sci->_gfxRemap32->getStartColor();
    const Palette &currentPalette = g_sci->_gfxPalette32->getCurrentPalette();
    
    for (uint i = 0; i < remapStartColor; ++i) {
        const Color &color = currentPalette.colors[i];
        _remapColors[i] = i;
        _originalColors[i] = color;
        _originalColorsChanged[i] = true;
        _idealColors[i] = color;
        _idealColorsChanged[i] = false;
        _matchDistances[i] = 0;
    }
}
```

### Remappage par pourcentage

```cpp
// engines/sci/graphics/remap.cpp lignes 58-72
void GfxRemap::setRemappingPercent(byte color, byte percent) {
    _remapOn = true;

    // Différé jusqu'au prochain changement de palette pour que
    // kernelFindColor() puisse trouver les couleurs correctes
    _remappingPercentToSet = percent;

    for (int i = 0; i < 256; i++) {
        byte r = _palette->_sysPalette.colors[i].r * _remappingPercentToSet / 100;
        byte g = _palette->_sysPalette.colors[i].g * _remappingPercentToSet / 100;
        byte b = _palette->_sysPalette.colors[i].b * _remappingPercentToSet / 100;
        _remappingByPercent[i] = _palette->kernelFindColor(r, g, b);
    }
}
```

### Application du remappage sur bitmap

```cpp
// engines/sci/engine/segment.h lignes 1154-1173
void SciBitmap::applyRemap(SciArray &clut) {
    const int length = getWidth() * getHeight();
    uint8 *pixel = getPixels();
    for (int i = 0; i < length; ++i) {
        const int16 color = clut.getAsInt16(*pixel);
        assert(color >= 0 && color <= 255);
        *pixel++ = (uint8)color;
    }
}
```

---

## Conclusion

### Points clés

1. **Robot v5 et v6 ont du remappage** : v5 utilise GfxRemap (SCI16), v6 utilise GfxRemap32 (SCI32)
2. **Géré par le moteur** : aucune information de remappage dans le fichier .rbt
3. **Système SCI16** : GfxRemap avec remapByPercent et remapByRange
4. **Système SCI32** : GfxRemap32 avec 5 types de remappage
5. **Pixels remappables** : tous les pixels peuvent être remappés via tables
6. **Extraction impossible** : un extracteur externe ne peut pas reproduire le remappage

### Implications pour notre projet

- Notre extracteur peut lire la **palette brute** (1200 octets) pour v5 et v6
- Nous **ne pouvons pas** appliquer le remappage (nécessite le moteur SCI)
- Les frames extraites auront les **couleurs originales non remappées**
- Pour voir le remappage réel : il faut jouer la vidéo dans ScummVM
- **Robot v5 (Phantasmagoria)** : peut avoir du remappage si Vocab 184 existe
- **Robot v6 (RAMA)** : a toujours le support de remappage (mais optionnel selon les scènes)

### Ressources

**Code source ScummVM** :
- `engines/sci/video/robot_decoder.h` : documentation format Robot
- `engines/sci/video/robot_decoder.cpp` : implémentation décodeur
- `engines/sci/graphics/remap.h` : interface remappage SCI16 (Robot v5)
- `engines/sci/graphics/remap.cpp` : implémentation remappage SCI16
- `engines/sci/graphics/remap32.h` : interface remappage SCI32 (Robot v6)
- `engines/sci/graphics/remap32.cpp` : implémentation remappage SCI32
- `engines/sci/graphics/palette16.cpp` : gestion palette SCI16
- `engines/sci/graphics/palette32.cpp` : gestion palette SCI32
- `engines/sci/engine/segment.h` : classe SciBitmap
- `engines/sci/engine/kgraphics.cpp` : kernel functions SCI16
- `engines/sci/engine/kgraphics32.cpp` : kernel functions SCI32

**Jeux concernés** :
- **Robot v5 avec remappage** : QFG4 Demo, potentiellement Phantasmagoria/KQ7/PQ:SWAT/Lighthouse
- **Robot v6 avec remappage** : RAMA (1996)

---

*Documentation créée à partir du code source ScummVM (https://github.com/scummvm/scummvm)*  
*Dernière mise à jour : 21 novembre 2024*
