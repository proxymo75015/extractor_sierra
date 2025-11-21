# GfxRemap (SCI16) - Documentation complète

## Vue d'ensemble

**GfxRemap** est le système de remappage de palette pour les jeux **SCI16** (SCI1.1 et SCI2 early). Cette classe permet de modifier dynamiquement les couleurs affichées à l'écran sans changer la palette elle-même, en remplaçant certaines couleurs par d'autres au moment du rendu.

> **Note importante** : GfxRemap est utilisé principalement dans **QFG4 Demo** et potentiellement dans d'autres jeux SCI11+ ayant la ressource **Vocab 184**.

---

## Jeux concernés

### Utilisation confirmée

D'après le code source ScummVM (`engines/sci/engine/kernel.cpp` lignes 768-777) :

```cpp
} else if (g_sci->getGameId() == GID_QFG4DEMO) {
    _kernelNames[0x7b] = "RemapColors"; // QFG4 Demo has this SCI2 function instead of StrSplit
```

**Jeux avec GfxRemap** :
- **QFG4 Demo** (SCI1.1) : utilisation principale confirmée
- **Phantasmagoria** (SCI2) : potentiel si Vocab 184 présent
- **King's Quest 7** (SCI2) : potentiel si Vocab 184 présent
- **Police Quest: SWAT** (SCI2) : potentiel si Vocab 184 présent
- **Lighthouse** (SCI2) : potentiel si Vocab 184 présent
- **Catdate** (SCI11+) : mention dans le code pour remappage spécial

### Condition d'activation

D'après `engines/sci/sci.cpp` (lignes 598-619) :

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

**Conditions d'activation** :
1. **QFG4 Demo** : toujours activé
2. **Autres jeux SCI11+** : seulement si la ressource **Vocab 184** existe

---

## Architecture de la classe

### Fichiers sources

Source : `engines/sci/graphics/remap.h` et `remap.cpp`

```cpp
/* ScummVM - Graphic Adventure Engine
 *
 * This class handles color remapping for the QFG4 demo.
 */

#ifndef SCI_GRAPHICS_REMAP_H
#define SCI_GRAPHICS_REMAP_H

#include "common/array.h"
#include "common/serializer.h"

namespace Sci {

class GfxScreen;

/**
 * This class handles color remapping for the QFG4 demo.
 */
class GfxRemap {
private:
    enum ColorRemappingType {
        kRemapNone = 0,
        kRemapByRange = 1,
        kRemapByPercent = 2
    };

public:
    GfxRemap(GfxPalette *_palette);

    void resetRemapping();
    void setRemappingPercent(byte color, byte percent);
    void setRemappingRange(byte color, byte from, byte to, byte base);
    bool isRemapped(byte color) const {
        return _remapOn && (_remappingType[color] != kRemapNone);
    }
    byte remapColor(byte remappedColor, byte screenColor);
    void updateRemapping();

private:
    GfxPalette *_palette;

    bool _remapOn;
    ColorRemappingType _remappingType[256];
    byte _remappingByPercent[256];
    byte _remappingByRange[256];
    uint16 _remappingPercentToSet;
};

} // End of namespace Sci

#endif // SCI_GRAPHICS_REMAP_H
```

### Membres de la classe

#### Données privées

```cpp
private:
    GfxPalette *_palette;              // Pointeur vers la palette 16 couleurs
    
    bool _remapOn;                      // Flag d'activation du remappage
    
    ColorRemappingType _remappingType[256];  // Type de remappage pour chaque couleur
    
    byte _remappingByPercent[256];      // Table de remappage par pourcentage
    
    byte _remappingByRange[256];        // Table de remappage par plage
    
    uint16 _remappingPercentToSet;      // Pourcentage à appliquer (différé)
```

#### Types de remappage

```cpp
enum ColorRemappingType {
    kRemapNone = 0,        // Pas de remappage
    kRemapByRange = 1,     // Remappage par plage de couleurs
    kRemapByPercent = 2    // Remappage par pourcentage d'intensité
};
```

---

## Fonctions publiques

### 1. Constructeur

```cpp
GfxRemap::GfxRemap(GfxPalette *palette)
    : _palette(palette) {
    _remapOn = false;
    resetRemapping();
}
```

**Description** :
- Initialise le système de remappage
- Désactive le remappage par défaut
- Réinitialise toutes les tables

---

### 2. resetRemapping()

```cpp
void GfxRemap::resetRemapping() {
    _remapOn = false;
    _remappingPercentToSet = 0;

    for (int i = 0; i < 256; i++) {
        _remappingType[i] = kRemapNone;
        _remappingByPercent[i] = i;      // Identité : couleur → même couleur
        _remappingByRange[i] = i;        // Identité : couleur → même couleur
    }
}
```

**Description** :
- Désactive complètement le remappage
- Réinitialise toutes les tables à l'identité (chaque couleur pointe vers elle-même)
- Remet le pourcentage à 0

**Utilisation** :
- Appelé avant d'appliquer un nouveau remappage
- Utilisé dans QFG4 Demo avant chaque opération de remappage

---

### 3. setRemappingPercent()

```cpp
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
    
    _remappingType[color] = kRemapByPercent;
}
```

**Paramètres** :
- `color` : la couleur à remapper (généralement 254 dans QFG4 Demo)
- `percent` : pourcentage d'intensité (0-100, peut être > 100 pour oversaturation)

**Description** :
1. Active le remappage
2. Mémorise le pourcentage pour mise à jour ultérieure
3. Pour chaque couleur de la palette :
   - Réduit R, G, B par le pourcentage
   - Trouve la couleur la plus proche dans la palette avec `kernelFindColor()`
   - Stocke le résultat dans `_remappingByPercent[]`
4. Marque la couleur comme étant remappée par pourcentage

**Effet** :
- Assombrit/éclaircit toutes les couleurs selon le pourcentage
- `percent = 100` : pas de changement
- `percent = 50` : couleurs à 50% d'intensité (plus sombres)
- `percent = 150` : couleurs à 150% d'intensité (oversaturées)

---

### 4. setRemappingRange()

```cpp
void GfxRemap::setRemappingRange(byte color, byte from, byte to, byte base) {
    _remapOn = true;

    for (int i = from; i <= to; i++) {
        _remappingByRange[i] = i + base;
    }

    _remappingType[color] = kRemapByRange;
}
```

**Paramètres** :
- `color` : la couleur à remapper (généralement 254 dans QFG4 Demo)
- `from` : première couleur de la plage source
- `to` : dernière couleur de la plage source (inclusive)
- `base` : décalage à appliquer

**Description** :
1. Active le remappage
2. Pour chaque couleur dans la plage `[from, to]` :
   - Mappe vers `couleur + base`
3. Marque la couleur comme étant remappée par plage

**Exemple** :
```cpp
setRemappingRange(254, 10, 20, 5);
// Couleur 10 → 15
// Couleur 11 → 16
// ...
// Couleur 20 → 25
```

**Utilisation** :
- Décaler une plage de couleurs (par exemple pour créer des effets de lumière)
- Permuter des couleurs entre différentes plages

---

### 5. isRemapped()

```cpp
bool isRemapped(byte color) const {
    return _remapOn && (_remappingType[color] != kRemapNone);
}
```

**Paramètres** :
- `color` : couleur à tester

**Retour** :
- `true` si la couleur est actuellement remappée
- `false` sinon

**Description** :
- Vérifie si le remappage est activé ET si la couleur spécifique a un type de remappage actif

---

### 6. remapColor()

```cpp
byte GfxRemap::remapColor(byte remappedColor, byte screenColor) {
    assert(_remapOn);
    
    if (_remappingType[remappedColor] == kRemapByRange)
        return _remappingByRange[screenColor];
    else if (_remappingType[remappedColor] == kRemapByPercent)
        return _remappingByPercent[screenColor];
    else
        error("remapColor(): Color %d isn't remapped", remappedColor);

    return 0;  // should never reach here
}
```

**Paramètres** :
- `remappedColor` : la couleur source (celle qui a été configurée pour le remappage)
- `screenColor` : la couleur cible à l'écran

**Retour** :
- La couleur finale après remappage

**Description** :
1. Vérifie le type de remappage pour `remappedColor`
2. Si `kRemapByRange` : utilise la table `_remappingByRange[]`
3. Si `kRemapByPercent` : utilise la table `_remappingByPercent[]`
4. Retourne la couleur remappée

**Utilisation** :
- Appelé pendant le rendu pour transformer les couleurs à la volée
- Utilisé dans `GfxView::getMappedColor()` (voir section Intégration)

---

### 7. updateRemapping()

```cpp
void GfxRemap::updateRemapping() {
    // Vérifie si on doit recalculer le remappage par pourcentage avec les nouvelles couleurs
    if (_remappingPercentToSet) {
        for (int i = 0; i < 256; i++) {
            byte r = _palette->_sysPalette.colors[i].r * _remappingPercentToSet / 100;
            byte g = _palette->_sysPalette.colors[i].g * _remappingPercentToSet / 100;
            byte b = _palette->_sysPalette.colors[i].b * _remappingPercentToSet / 100;
            _remappingByPercent[i] = _palette->kernelFindColor(r, g, b);
        }
    }
}
```

**Description** :
- Recalcule la table de remappage par pourcentage si nécessaire
- Appelé après un changement de palette système
- Nécessaire car `kernelFindColor()` dépend de la palette courante

**Raison d'être** :
- Le remappage par pourcentage doit trouver la couleur la plus proche après réduction RGB
- Si la palette change, les couleurs les plus proches peuvent changer aussi
- Cette fonction met à jour la table pour rester cohérente

---

## Kernel Functions

### kRemapColors (QFG4 Demo)

Source : `engines/sci/engine/kgraphics.cpp` (lignes 1334-1360)

```cpp
// Early variant of the SCI32 kRemapColors kernel function, used in the demo of QFG4
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
        
    default:
        break;
    }

    return s->r_acc;
}
```

**Signature** :
```cpp
{ MAP_CALL(RemapColors), SIG_SCI11, SIGFOR_ALL, "i(i)(i)(i)(i)", NULL, NULL }
```

**Paramètres** :
- `operation` : type d'opération (0 = byPercent, 1 = byRange, 2 = off)
- Arguments variables selon l'opération

**Opérations** :

#### Operation 0 : Remap by Percent
```
kRemapColors(0, percent)
```
- Réinitialise le remappage
- Applique un remappage par pourcentage sur la couleur 254
- Utilisé pour assombrir/éclaircir l'écran

#### Operation 1 : Remap by Range
```
kRemapColors(1, from, to, base)
```
- Réinitialise le remappage
- Applique un remappage par plage sur la couleur 254
- Décale les couleurs `[from, to]` par `base`

#### Operation 2 : Turn off
- **Non utilisé** dans QFG4 Demo
- Provoque une erreur si appelé

---

### kRemapColorsKawa (SCI11+ style SCI32)

Source : `engines/sci/engine/kgraphics.cpp` (lignes 1364-1389)

```cpp
// Later SCI32-style kRemapColors, but in SCI11+.
reg_t kRemapColorsKawa(EngineState *s, int argc, reg_t *argv) {
    uint16 operation = argv[0].toUint16();

    switch (operation) {
    case 0: // off
        break;
        
    case 1: { // remap by percent
        uint16 from = argv[1].toUint16();
        uint16 percent = argv[2].toUint16();
        g_sci->_gfxRemap16->resetRemapping();
        g_sci->_gfxRemap16->setRemappingPercent(from, percent);
        }
        break;
        
    case 2: { // remap by range
        uint16 from = argv[1].toUint16();
        uint16 to = argv[2].toUint16();
        uint16 base = argv[3].toUint16();
        g_sci->_gfxRemap16->resetRemapping();
        g_sci->_gfxRemap16->setRemappingRange(254, from, to, base);
        }
        break;
        
    default:
        error("Unsupported SCI32-style kRemapColors(%d) has been called", operation);
        break;
    }
    
    return s->r_acc;
}
```

**Signature** :
```cpp
{ MAP_CALL(RemapColorsKawa), SIG_SCI11, SIGFOR_ALL, "i(i)(i)(i)(i)(i)", NULL, NULL }
```

**Différence avec kRemapColors** :
- Operation 0 : désactive (au lieu de provoquer une erreur)
- Operation 1 : prend `from` comme premier paramètre (plus flexible)
- Operation 2 : similaire mais pour la couleur 254 uniquement

---

## Intégration avec le rendu

### Application du remappage dans GfxView

Source : `engines/sci/graphics/view.cpp` (lignes 777-786)

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

**Pipeline de transformation** :
1. `color` (palette source) → `palette->mapping[color]` (palette mappée)
2. Si GfxRemap16 existe et la couleur est remappée :
   - Appelle `remapColor(outputColor, screenColor)`
   - Utilise la couleur actuelle à l'écran comme référence
3. Si `scaleSignal` spécial (Catdate) :
   - Applique un remappage spécial (noir total ou assombrissement)

**Flux complet** :
```
Pixel source → Palette mapping → GfxRemap16 → Signal spécial → Couleur finale
```

---

## Exemples d'utilisation

### Exemple 1 : Assombrir l'écran (QFG4 Demo)

```cpp
// Assombrir à 50% d'intensité
kRemapColors(0, 50);

// Résultat :
// - Toutes les couleurs sont réduites à 50% de leur luminosité
// - La couleur 254 devient une couleur "remap" qui transforme les pixels
```

**Effet visuel** :
- Transition jour → nuit
- Effet de flash (temporaire)
- Zone d'ombre

---

### Exemple 2 : Décalage de palette (QFG4 Demo)

```cpp
// Décaler les couleurs 10-20 par +5
kRemapColors(1, 10, 20, 5);

// Résultat :
// - Couleur 10 → 15
// - Couleur 11 → 16
// - ...
// - Couleur 20 → 25
```

**Effet visuel** :
- Permutation de couleurs pour animation
- Cycle de couleurs (feu, eau, etc.)
- Changement de palette partiel

---

### Exemple 3 : Remappage flexible (kRemapColorsKawa)

```cpp
// Remapper la couleur 128 à 75% d'intensité
kRemapColorsKawa(1, 128, 75);

// Résultat :
// - La couleur 128 devient une couleur "remap"
// - Tous les pixels de couleur 128 sont remappés selon la table
```

**Différence** :
- Plus flexible que `kRemapColors` (QFG4 Demo)
- Permet de choisir la couleur source (pas seulement 254)

---

## Comparaison GfxRemap (SCI16) vs GfxRemap32 (SCI32)

| Aspect | GfxRemap (SCI16) | GfxRemap32 (SCI32) |
|--------|------------------|---------------------|
| **Jeux** | QFG4 Demo, SCI11+ avec Vocab 184 | RAMA, tous les jeux SCI32 |
| **Version SCI** | SCI1.1, SCI2 early | SCI2, SCI2.1, SCI3 |
| **Types de remap** | 2 types : byPercent, byRange | 5 types : byPercent, byRange, toGray, toPercentGray, blockRange |
| **Complexité** | Simple : 2 tables de 256 bytes | Avancé : SingleRemap avec calcul de distance RGB |
| **Calcul** | Pré-calculé dans tables | Calculé dynamiquement avec matching de couleurs |
| **Classe** | `GfxRemap` | `GfxRemap32` avec `SingleRemap` |
| **Plage de remap** | Toutes les 256 couleurs | 236-245 (PC) ou 237-245 (Mac) |
| **Kernel functions** | `kRemapColors`, `kRemapColorsKawa` | `kRemapColors32`, `kRemapColorsByPercent`, etc. |
| **Activation** | QFG4 Demo OU Vocab 184 | Toujours activé en SCI32 |
| **Utilisation** | Rare, principalement QFG4 Demo | Commune dans jeux SCI32 |

---

## Différences techniques majeures

### Architecture

**GfxRemap (SCI16)** :
```cpp
class GfxRemap {
    byte _remappingByPercent[256];   // Table pré-calculée
    byte _remappingByRange[256];     // Table pré-calculée
    ColorRemappingType _remappingType[256];
};
```

**GfxRemap32 (SCI32)** :
```cpp
class GfxRemap32 {
    Common::Array<SingleRemap> _remaps;  // Liste de SingleRemap
};

class SingleRemap {
    uint8 _remapColors[237];        // Table finale
    Color _originalColors[237];      // Couleurs d'origine
    Color _idealColors[237];         // Couleurs cibles idéales
    int _matchDistances[237];        // Distances de matching
};
```

### Algorithme de remappage

**GfxRemap (SCI16)** :
1. Pré-calcule toute la table `_remappingByPercent[]`
2. Pour chaque couleur : `R*percent/100, G*percent/100, B*percent/100`
3. Trouve la couleur la plus proche avec `kernelFindColor()`
4. Stocke dans la table
5. Au moment du rendu : simple lookup dans la table

**GfxRemap32 (SCI32)** :
1. Stocke les paramètres de remappage (`_percent`, `_gray`, `_delta`, etc.)
2. Au moment de `update()` : calcule les `_idealColors[]`
3. Lors de `apply()` :
   - Bloque certaines couleurs (cycling, blocked range)
   - Calcule la distance RGB pour chaque couleur
   - Trouve la meilleure correspondance
   - Stocke dans `_remapColors[]`
4. Au moment du rendu : lookup dans `_remapColors[]`

**Avantage SCI16** :
- Plus simple et rapide
- Tout est pré-calculé

**Avantage SCI32** :
- Plus précis (distance RGB)
- Plus flexible (5 types de remap)
- Gère les couleurs bloquées (cycling)
- Peut gérer plusieurs remaps simultanés

---

## Limitations et contraintes

### Limitations de GfxRemap (SCI16)

1. **Seulement 2 types de remappage** :
   - byPercent
   - byRange
   - Pas de conversion en niveaux de gris

2. **Pas de gestion des couleurs bloquées** :
   - Toutes les couleurs peuvent être utilisées comme cibles
   - Pas de protection pour les couleurs cyclées

3. **Un seul remap actif à la fois** :
   - `resetRemapping()` appelé avant chaque opération
   - Impossible d'avoir plusieurs remaps simultanés

4. **Pré-calcul nécessaire** :
   - Toute la table doit être recalculée si la palette change
   - `updateRemapping()` doit être appelé manuellement

5. **Dépendance à kernelFindColor()** :
   - La qualité du remappage dépend de cet algorithme
   - Peut donner des résultats imprécis si la palette est limitée

### Contraintes d'utilisation

1. **Vocab 184 requis** (sauf QFG4 Demo) :
   - Jeu doit avoir cette ressource
   - Si absente, `_gfxRemap16` n'est pas créé

2. **Couleur 254 hardcodée** :
   - `kRemapColors` utilise toujours la couleur 254
   - Pas flexible pour d'autres couleurs

3. **Réinitialisation systématique** :
   - Chaque appel à kernel function réinitialise tout
   - Impossible de cumuler les effets

---

## Architecture technique complète

### Pipeline de rendu avec GfxRemap

```
┌─────────────────────────────────────────────────────────────┐
│                    1. INITIALISATION                         │
│  • SciEngine::initGraphics()                                │
│  • Vérifie : QFG4 Demo OU Vocab 184 existe                  │
│  • Si oui : _gfxRemap16 = new GfxRemap(_gfxPalette16)      │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│              2. CONFIGURATION DU REMAPPAGE                   │
│  • Script appelle kRemapColors() ou kRemapColorsKawa()      │
│  • resetRemapping() : réinitialise tout                     │
│  • setRemappingPercent() OU setRemappingRange()            │
│  • Pré-calcul des tables de remappage                       │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│                3. RENDU D'UNE VIEW                           │
│  • GfxView::getMappedColor(color, ...)                     │
│  • outputColor = palette->mapping[color]                    │
│  • Si isRemapped(outputColor) :                             │
│    - remapColor(outputColor, screenColor)                   │
│    - Lookup dans _remappingByPercent[] ou _remappingByRange[]│
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│              4. AFFICHAGE DU PIXEL FINAL                     │
│  • Couleur finale affichée à l'écran                        │
└─────────────────────────────────────────────────────────────┘
```

### Flux de données

```
Script SCI
    ↓
kRemapColors(operation, ...)
    ↓
resetRemapping()
    ↓
setRemappingPercent(254, percent)
    ↓
┌─────────────────────────────────────────┐
│ Pour chaque couleur i (0-255) :         │
│   R' = R[i] * percent / 100             │
│   G' = G[i] * percent / 100             │
│   B' = B[i] * percent / 100             │
│   _remappingByPercent[i] = findColor(R',G',B') │
└─────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────┐
│ Au moment du rendu :                    │
│   pixel_source = 128                    │
│   pixel_mapped = palette->mapping[128]  │
│   if (isRemapped(pixel_mapped))         │
│     pixel_final = _remappingByPercent[pixel_écran] │
└─────────────────────────────────────────┘
```

---

## Exemples de code ScummVM

### Initialisation complète

```cpp
// engines/sci/sci.cpp (lignes 598-619)
void SciEngine::initGraphics() {
    #ifdef ENABLE_SCI32
    if (getSciVersion() >= SCI_VERSION_2) {
        _gfxPalette32 = new GfxPalette32(_resMan);
        _gfxRemap32 = new GfxRemap32();
    } else {
    #endif
        _gfxPalette16 = new GfxPalette(_resMan, _gfxScreen);
        
        // Création conditionnelle de GfxRemap16
        if (getGameId() == GID_QFG4DEMO || 
            _resMan->testResource(ResourceId(kResourceTypeVocab, 184)))
            _gfxRemap16 = new GfxRemap(_gfxPalette16);
    #ifdef ENABLE_SCI32
    }
    #endif
}
```

### Utilisation dans QFG4 Demo

```cpp
// engines/sci/engine/kgraphics.cpp (lignes 1335-1360)
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

### Application dans le rendu

```cpp
// engines/sci/graphics/view.cpp (lignes 777-786)
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
            outputColor = g_sci->_gfxRemap16->remapColor(outputColor, 
                                                         _screen->getVisual(x2, y2));
    }
    
    return outputColor;
}
```

---

## Conclusion

### Points clés

1. **GfxRemap est pour SCI16** : principalement QFG4 Demo et jeux avec Vocab 184
2. **2 types de remappage** : byPercent (intensité) et byRange (décalage)
3. **Système simple** : tables pré-calculées de 256 bytes
4. **Un seul remap actif** : chaque kernel call réinitialise tout
5. **Dépend de kernelFindColor()** : qualité du remappage limitée par cet algorithme
6. **Couleur 254 hardcodée** : dans QFG4 Demo kernel functions

### Différences avec GfxRemap32

- **Plus simple** : 2 types vs 5 types
- **Moins flexible** : un seul remap vs multiples simultanés
- **Pas de blocage** : toutes couleurs utilisables vs blocked ranges
- **Pré-calculé** : tables fixes vs calcul dynamique
- **Moins précis** : kernelFindColor() vs distance RGB exacte

### Implications pour notre projet

- GfxRemap n'est **PAS stocké dans les fichiers de ressources**
- C'est un **système runtime du moteur SCI**
- Notre extracteur **ne peut pas** reproduire les effets de remappage
- Les frames extraites auront les **couleurs originales non remappées**
- Pour voir le remappage réel : **il faut jouer dans ScummVM**

### Ressources

**Code source ScummVM** :
- `engines/sci/graphics/remap.h` : interface GfxRemap
- `engines/sci/graphics/remap.cpp` : implémentation complète
- `engines/sci/engine/kgraphics.cpp` : kernel functions kRemapColors et kRemapColorsKawa
- `engines/sci/graphics/view.cpp` : application dans getMappedColor()
- `engines/sci/sci.cpp` : initialisation conditionnelle dans initGraphics()
- `engines/sci/engine/kernel_tables.h` : signatures des kernel functions

**Jeux concernés** :
- **QFG4 Demo** : utilisation confirmée principale
- **Phantasmagoria, KQ7, PQ:SWAT, Lighthouse** : potentiel si Vocab 184 présent
- **Catdate** : mention spéciale pour remappage via scaleSignal

---

*Documentation créée à partir du code source ScummVM (https://github.com/scummvm/scummvm)*  
*Dernière mise à jour : 21 novembre 2024*
