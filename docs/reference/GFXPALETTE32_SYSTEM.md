# Système de Gestion des Palettes GfxPalette32 (SCI32)

Documentation complète du système de gestion des palettes pour SCI32, basée sur le code source de ScummVM.

---

## Table des Matières

1. [Vue d'ensemble](#vue-densemble)
2. [Architecture du Système](#architecture-du-système)
3. [Palettes Internes](#palettes-internes)
4. [Soumission de Palettes (submit)](#soumission-de-palettes-submit)
5. [Mise à Jour pour le Frame (updateForFrame)](#mise-à-jour-pour-le-frame-updateforframe)
6. [Palette Varying (Variation)](#palette-varying-variation)
7. [Palette Cycling (Animation Cyclique)](#palette-cycling-animation-cyclique)
8. [Palette Fading (Atténuation)](#palette-fading-atténuation)
9. [Correction Gamma](#correction-gamma)
10. [Mise à Jour Matérielle (updateHardware)](#mise-à-jour-matérielle-updatehardware)
11. [Processus Complet de Rendu](#processus-complet-de-rendu)
12. [Références du Code Source](#références-du-code-source)

---

## Vue d'ensemble

Le système **GfxPalette32** gère toutes les opérations de palettes pour SCI32, incluant :

- **Soumission** de palettes depuis les ressources ou bitmaps
- **Variation** (varying) : transition progressive entre deux palettes
- **Cyclage** (cycling) : rotation animée de plages de couleurs
- **Atténuation** (fading) : modification de l'intensité des couleurs
- **Correction gamma** : ajustement de la luminosité
- **Versioning** : détection des changements de palette source

### Caractéristiques Principales

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 358-391
GfxPalette32::GfxPalette32(ResourceManager *resMan)
: _resMan(resMan),
  // Palette versioning
  _version(1),
  _needsUpdate(false),
#ifdef USE_RGB_COLOR
  _hardwarePalette(),
#endif
  _currentPalette(),
  _sourcePalette(),
  _nextPalette(),
  
  // Palette varying
  _varyStartPalette(nullptr),
  _varyTargetPalette(nullptr),
  _varyFromColor(0),
  _varyToColor(255),
  _varyLastTick(0),
  _varyTime(0),
  _varyDirection(0),
  _varyPercent(0),
  _varyTargetPercent(0),
  _varyNumTimesPaused(0),
  
  // Palette cycling
  _cycleMap(),
  
  // Gamma correction
  _gammaLevel(g_sci->_features->useMacGammaLevel() ? 2 : -1),
  _gammaChanged(false)
```

---

## Architecture du Système

### Palettes Internes

GfxPalette32 maintient **trois palettes principales** :

1. **`_sourcePalette`** : Palette source non modifiée
2. **`_nextPalette`** : Palette à utiliser pour le prochain frame (après effets)
3. **`_currentPalette`** : Palette actuellement affichée

```cpp
// Fichier : engines/sci/graphics/palette32.h lignes 334-344
/**
 * The unmodified source palette loaded by kPalette. Additional palette
 * entries may be mixed into the source palette by CelObj objects, which
 * contain their own palettes.
 */
Palette _sourcePalette;

/**
 * The palette to be used when the hardware is next updated.
 * On update, `_nextPalette` is transferred to `_currentPalette`.
 */
Palette _nextPalette;
```

### Versioning des Palettes

Le système utilise un **numéro de version** pour détecter les changements :

```cpp
// Fichier : engines/sci/graphics/palette32.h lignes 313-318
/**
 * The palette revision version. Increments once per game loop that changes
 * the source palette.
 */
uint32 _version;

/**
 * Whether or not the hardware palette needs updating.
 */
bool _needsUpdate;
```

---

## Soumission de Palettes (submit)

### Fonction `submit(const Palette &palette)`

Fusionne une palette soumise dans `_sourcePalette`. Seules les entrées marquées comme "used" sont copiées.

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 438-462
void GfxPalette32::submit(const Palette &palette) {
    // If `_needsUpdate` is already set, there is no need to test whether
    // this palette submission causes a change to `_sourcePalette` since it is
    // going to be updated already anyway
    if (_needsUpdate) {
        mergePalette(_sourcePalette, palette);
    } else {
        const Palette oldSourcePalette(_sourcePalette);
        mergePalette(_sourcePalette, palette);

        if (_sourcePalette != oldSourcePalette) {
            ++_version;
            _needsUpdate = true;
        }
    }
}
```

### Fonction `submit(const HunkPalette &hunkPalette)`

Soumission depuis un HunkPalette avec vérification de version :

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 463-473
void GfxPalette32::submit(const HunkPalette &hunkPalette) {
    if (hunkPalette.getVersion() == _version) {
        return;
    }

    submit(hunkPalette.toPalette());
    hunkPalette.setVersion(_version);
}
```

### Fusion des Palettes

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 569-589
void GfxPalette32::mergePalette(Palette &to, const Palette &from) {
    // All colors MUST be copied, even index 255, despite the fact that games
    // cannot actually change index 255 (it is forced to white when generating
    // the hardware palette in updateHardware). While this causes some
    // additional unnecessary source palette invalidations, not doing it breaks
    // some badly programmed rooms, like room 6400 in Phant1 (see Trac#9788).
    // (Note, however, that that specific glitch is fully fixed by ignoring a
    // bad palette in the CelObjView constructor)
    for (int i = 0; i < ARRAYSIZE(to.colors); ++i) {
        if (from.colors[i].used) {
            to.colors[i] = from.colors[i];
        }
    }
}
```

**Note importante** : Toutes les couleurs sont copiées, **y compris l'index 255**, malgré le fait que les jeux ne peuvent pas vraiment modifier cette entrée (elle est forcée en blanc lors de la génération de la palette matérielle).

---

## Mise à Jour pour le Frame (updateForFrame)

### Fonction `updateForFrame()`

Applique tous les effets à `_nextPalette` et met à jour les tables de remapping.

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 475-481
bool GfxPalette32::updateForFrame() {
    applyAll();
    _needsUpdate = false;
    return g_sci->_gfxRemap32->remapAllTables(_nextPalette != _currentPalette);
}
```

### Fonction `applyAll()`

Applique séquentiellement tous les effets de palette :

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 583-589
void GfxPalette32::applyAll() {
    applyVary();
    applyCycles();
    applyFade();
}
```

**Ordre d'application** :
1. **Vary** (variation de palette)
2. **Cycles** (rotation de couleurs)
3. **Fade** (atténuation d'intensité)

---

## Palette Varying (Variation)

Le système de **palette varying** permet de créer des transitions progressives entre deux palettes sur une durée définie.

### Variables de Contrôle

```cpp
// Fichier : engines/sci/graphics/palette32.h lignes 430-490
/**
 * An optional palette used to provide source colors for a palette vary
 * operation. If this palette is not specified, `_sourcePalette` is used
 * instead.
 */
Common::ScopedPtr<Palette> _varyStartPalette;

/**
 * An optional palette used to provide target colors for a palette vary
 * operation.
 */
Common::ScopedPtr<Palette> _varyTargetPalette;

/**
 * The minimum palette index that has been varied from the source palette.
 */
uint8 _varyFromColor;

/**
 * The maximum palette index that has been varied from the source palette.
 */
uint8 _varyToColor;

/**
 * The tick at the last time the palette vary was updated.
 */
uint32 _varyLastTick;

/**
 * The amount of time that should elapse, in ticks, between each cycle of a
 * palette vary animation.
 */
int32 _varyTime;

/**
 * The direction of change: -1, 0, or 1.
 */
int16 _varyDirection;

/**
 * The amount, in percent, that the vary color is currently blended into the
 * source color.
 */
int16 _varyPercent;

/**
 * The target amount that a vary color will be blended into the source
 * color.
 */
int16 _varyTargetPercent;

/**
 * The number of times palette varying has been paused.
 */
uint16 _varyNumTimesPaused;
```

### Initialisation d'une Variation

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 593-607
void GfxPalette32::setVary(const Palette &target, const int16 percent, 
                           const int32 ticks, const int16 fromColor, 
                           const int16 toColor) {
    setTarget(target);
    setVaryTime(percent, ticks);

    if (fromColor > -1) {
        _varyFromColor = fromColor;
    }
    if (toColor > -1) {
        assert(toColor < 256);
        _varyToColor = toColor;
    }
}
```

### Configuration du Temps de Variation

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 621-641
void GfxPalette32::setVaryTime(const int16 percent, const int32 ticks) {
    _varyLastTick = g_sci->getTickCount();
    if (!ticks || _varyPercent == percent) {
        _varyDirection = 0;
        _varyTargetPercent = _varyPercent = percent;
    } else {
        _varyTime = ticks / (percent - _varyPercent);
        _varyTargetPercent = percent;

        if (_varyTime > 0) {
            _varyDirection = 1;
        } else if (_varyTime < 0) {
            _varyDirection = -1;
            _varyTime = -_varyTime;
        } else {
            _varyDirection = 0;
            _varyTargetPercent = _varyPercent = percent;
        }
    }
}
```

### Application de la Variation

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 696-746
void GfxPalette32::applyVary() {
    const uint32 now = g_sci->getTickCount();
    while ((int32)(now - _varyLastTick) > _varyTime && _varyDirection != 0) {
        _varyLastTick += _varyTime;

        if (_varyPercent == _varyTargetPercent) {
            _varyDirection = 0;
        }

        _varyPercent += _varyDirection;
    }

    if (_varyPercent == 0 || !_varyTargetPalette) {
        for (int i = 0; i < ARRAYSIZE(_nextPalette.colors); ++i) {
            if (_varyStartPalette && i >= _varyFromColor && i <= _varyToColor) {
                _nextPalette.colors[i] = _varyStartPalette->colors[i];
            } else {
                _nextPalette.colors[i] = _sourcePalette.colors[i];
            }
        }
    } else {
        for (int i = 0; i < ARRAYSIZE(_nextPalette.colors); ++i) {
            if (i >= _varyFromColor && i <= _varyToColor) {
                Color targetColor = _varyTargetPalette->colors[i];
                Color sourceColor;

                if (_varyStartPalette) {
                    sourceColor = _varyStartPalette->colors[i];
                } else {
                    sourceColor = _sourcePalette.colors[i];
                }

                Color computedColor;

                int color;
                color = targetColor.r - sourceColor.r;
                computedColor.r = ((color * _varyPercent) / 100) + sourceColor.r;
                color = targetColor.g - sourceColor.g;
                computedColor.g = ((color * _varyPercent) / 100) + sourceColor.g;
                color = targetColor.b - sourceColor.b;
                computedColor.b = ((color * _varyPercent) / 100) + sourceColor.b;
                computedColor.used = sourceColor.used;

                _nextPalette.colors[i] = computedColor;
            } else {
                _nextPalette.colors[i] = _sourcePalette.colors[i];
            }
        }
    }
}
```

**Formule d'interpolation** :
```
computedColor.r = ((targetColor.r - sourceColor.r) * _varyPercent / 100) + sourceColor.r
```

### Fonctions de Contrôle

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 642-670

// Désactiver la variation
void GfxPalette32::varyOff() {
    _varyNumTimesPaused = 0;
    _varyPercent = _varyTargetPercent = 0;
    _varyFromColor = 0;
    _varyToColor = 255;
    _varyDirection = 0;
    _varyTargetPalette.reset();
    _varyStartPalette.reset();
}

// Mettre en pause la variation
void GfxPalette32::varyPause() {
    _varyDirection = 0;
    ++_varyNumTimesPaused;
}

// Reprendre la variation
void GfxPalette32::varyOn() {
    if (_varyNumTimesPaused > 0) {
        --_varyNumTimesPaused;
    }

    if (_varyTargetPalette && _varyNumTimesPaused == 0) {
        if (_varyPercent != _varyTargetPercent && _varyTime != 0) {
            _varyDirection = (_varyTargetPercent - _varyPercent > 0) ? 1 : -1;
        } else {
            _varyPercent = _varyTargetPercent;
        }
    }
}
```

### Gestion des Palettes Start/Target

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 671-694

void GfxPalette32::setTarget(const Palette &palette) {
    _varyTargetPalette.reset(new Palette(palette));
}

void GfxPalette32::setStart(const Palette &palette) {
    _varyStartPalette.reset(new Palette(palette));
}

void GfxPalette32::mergeStart(const Palette &palette) {
    if (_varyStartPalette) {
        mergePalette(*_varyStartPalette, palette);
    } else {
        _varyStartPalette.reset(new Palette(palette));
    }
}

void GfxPalette32::mergeTarget(const Palette &palette) {
    if (_varyTargetPalette) {
        mergePalette(*_varyTargetPalette, palette);
    } else {
        _varyTargetPalette.reset(new Palette(palette));
    }
}
```

### Cas Spécial : Inversion de Palette (SCI3)

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 748-767
void GfxPalette32::kernelPalVarySet(const GuiResourceId paletteId, 
                                    const int16 percent, const int32 ticks, 
                                    const int16 fromColor, const int16 toColor) {
    Palette palette;

    if (getSciVersion() == SCI_VERSION_3 && paletteId == 0xFFFF) {
        palette = _currentPalette;
        assert(fromColor >= 0 && fromColor < 256);
        assert(toColor >= 0 && toColor < 256);
        // While palette varying is normally inclusive of `toColor`, the
        // palette inversion code in SSCI excludes `toColor`, and RAMA room
        // 6201 requires this or else parts of the game's UI get inverted
        for (int i = fromColor; i < toColor; ++i) {
            palette.colors[i].r = ~palette.colors[i].r;
            palette.colors[i].g = ~palette.colors[i].g;
            palette.colors[i].b = ~palette.colors[i].b;
        }
    } else {
        palette = getPaletteFromResource(paletteId);
    }

    setVary(palette, percent, ticks, fromColor, toColor);
}
```

**Note** : Pour `paletteId = 0xFFFF` en SCI3, le système inverse les couleurs de la palette courante (utilisé dans RAMA room 6201).

---

## Palette Cycling (Animation Cyclique)

Le **palette cycling** permet de créer des animations en faisant tourner une plage de couleurs.

### Structure PalCycler

```cpp
// Fichier : engines/sci/graphics/palette32.h lignes 190-228
struct PalCycler {
    /**
     * The color index of this palette cycler. This value is used as the unique
     * key for this PalCycler object.
     */
    uint8 fromColor;

    /**
     * The number of palette slots which are to be cycled by this cycler.
     */
    uint16 numColorsToCycle;

    /**
     * The current position of the first palette entry.
     */
    uint8 currentCycle;

    /**
     * The direction of the cycler.
     */
    PalCyclerDirection direction;

    /**
     * The last tick the cycler cycled.
     */
    uint32 lastUpdateTick;

    /**
     * The amount of time in ticks each cycle should take to complete. In other
     * words, the higher the delay, the slower the cycle animation. If delay is
     * 0, the cycler does not automatically cycle and needs to be cycled
     * manually by calling `doCycle`.
     */
    int16 delay;

    /**
     * The number of times this cycler has been paused.
     */
    uint16 numTimesPaused;
};
```

### Initialisation d'un Cycler

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 801-850
void GfxPalette32::setCycle(const uint8 fromColor, const uint8 toColor, 
                            const int16 direction, const int16 delay) {
    assert(fromColor < toColor);

    PalCycler *cycler = getCycler(fromColor);

    if (cycler != nullptr) {
        clearCycleMap(fromColor, cycler->numColorsToCycle);
    } else {
        for (int i = 0; i < kNumCyclers; ++i) {
            if (!_cyclers[i]) {
                cycler = new PalCycler;
                _cyclers[i].reset(cycler);
                break;
            }
        }
    }

    // If there are no free cycler slots, SSCI overrides the first oldest cycler
    // that it finds, where "oldest" is determined by the difference between the
    // tick and now
    if (cycler == nullptr) {
        const uint32 now = g_sci->getTickCount();
        uint32 minUpdateDelta = 0xFFFFFFFF;

        for (int i = 0; i < kNumCyclers; ++i) {
            PalCyclerOwner &candidate = _cyclers[i];

            const uint32 updateDelta = now - candidate->lastUpdateTick;
            if (updateDelta < minUpdateDelta) {
                minUpdateDelta = updateDelta;
                cycler = candidate.get();
            }
        }

        clearCycleMap(cycler->fromColor, cycler->numColorsToCycle);
    }

    uint16 numColorsToCycle = toColor - fromColor;
    if (g_sci->_features->hasMidPaletteCode()) {
        numColorsToCycle += 1;
    }
    cycler->fromColor = fromColor;
    cycler->numColorsToCycle = numColorsToCycle;
    cycler->currentCycle = fromColor;
    cycler->direction = direction < 0 ? kPalCycleBackward : kPalCycleForward;
    cycler->delay = delay;
    cycler->lastUpdateTick = g_sci->getTickCount();
    cycler->numTimesPaused = 0;

    setCycleMap(fromColor, numColorsToCycle);
}
```

**Points clés** :
- Maximum de **10 cyclers** simultanés (`kNumCyclers = 10`)
- Si tous les slots sont occupés, le cycler le plus ancien est remplacé
- Les cyclers ne peuvent **pas se chevaucher** (détecté via `_cycleMap`)

### Carte de Cyclage (Cycle Map)

```cpp
// Fichier : engines/sci/graphics/palette32.h lignes 580-597
/**
 * The cycle map is used to detect overlapping cyclers, and to avoid
 * remapping to palette entries that are being cycled.
 *
 * According to SSCI, when two cyclers overlap, a fatal error has occurred
 * and the engine will display an error and then exit.
 *
 * The color remapping system avoids attempts to remap to palette entries
 * that are cycling because they won't be the expected color once the cycler
 * updates the palette entries.
 */
bool _cycleMap[256];
```

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 926-956
void GfxPalette32::clearCycleMap(const uint16 fromColor, const uint16 numColorsToClear) {
    bool *mapEntry = _cycleMap + fromColor;
    const bool *const lastEntry = _cycleMap + numColorsToClear;
    while (mapEntry < lastEntry) {
        *mapEntry++ = false;
    }
}

void GfxPalette32::setCycleMap(const uint16 fromColor, const uint16 numColorsToSet) {
    bool *mapEntry = _cycleMap + fromColor;
    const bool *const lastEntry = _cycleMap + numColorsToSet;
    while (mapEntry < lastEntry) {
        if (*mapEntry != false) {
            error("Cycles intersect");
        }
        *mapEntry++ = true;
    }
}
```

### Mise à Jour d'un Cycler

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 926-946
void GfxPalette32::updateCycler(PalCycler &cycler, const int16 speed) {
    int16 currentCycle = cycler.currentCycle;
    const uint16 numColorsToCycle = cycler.numColorsToCycle;

    if (cycler.direction == kPalCycleBackward) {
        currentCycle = (currentCycle - (speed % numColorsToCycle)) + numColorsToCycle;
    } else {
        currentCycle = currentCycle + speed;
    }

    cycler.currentCycle = currentCycle % numColorsToCycle;
}
```

### Application des Cycles

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 985-1006
void GfxPalette32::applyCycles() {
    Color paletteCopy[256];
    memcpy(paletteCopy, _nextPalette.colors, sizeof(paletteCopy));

    const uint32 now = g_sci->getTickCount();
    for (int i = 0; i < kNumCyclers; ++i) {
        PalCyclerOwner &cycler = _cyclers[i];
        if (!cycler) {
            continue;
        }

        if (cycler->delay != 0 && cycler->numTimesPaused == 0) {
            while ((cycler->delay + cycler->lastUpdateTick) < now) {
                updateCycler(*cycler, 1);
                cycler->lastUpdateTick += cycler->delay;
            }
        }

        for (int j = 0; j < cycler->numColorsToCycle; j++) {
            _nextPalette.colors[cycler->fromColor + j] = 
                paletteCopy[cycler->fromColor + (cycler->currentCycle + j) % cycler->numColorsToCycle];
        }
    }
}
```

**Processus** :
1. Copie de `_nextPalette` dans un buffer temporaire
2. Pour chaque cycler actif non pausé, mise à jour de `currentCycle` si `delay` écoulé
3. Rotation des couleurs selon `currentCycle` et `direction`

### Application Forcée de Tous les Cycles

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 969-983
void GfxPalette32::applyAllCycles() {
    Color paletteCopy[256];
    memcpy(paletteCopy, _nextPalette.colors, sizeof(paletteCopy));

    for (int i = 0; i < kNumCyclers; ++i) {
        PalCyclerOwner &cycler = _cyclers[i];
        if (cycler) {
            cycler->currentCycle = (cycler->currentCycle + 1) % cycler->numColorsToCycle;
            for (int j = 0; j < cycler->numColorsToCycle; j++) {
                _nextPalette.colors[cycler->fromColor + j] = 
                    paletteCopy[cycler->fromColor + (cycler->currentCycle + j) % cycler->numColorsToCycle];
            }
        }
    }
}
```

**Différence avec `applyCycles()`** : Force l'avancement d'un pas pour **tous** les cyclers, indépendamment du `delay`.

### Contrôle des Cyclers

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 852-925

// Cycle manuel avec vitesse spécifiée
void GfxPalette32::doCycle(const uint8 fromColor, const int16 speed) {
    PalCycler *const cycler = getCycler(fromColor);
    if (cycler != nullptr) {
        cycler->lastUpdateTick = g_sci->getTickCount();
        updateCycler(*cycler, speed);
    }
}

// Reprendre un cycler
void GfxPalette32::cycleOn(const uint8 fromColor) {
    PalCycler *const cycler = getCycler(fromColor);
    if (cycler != nullptr && cycler->numTimesPaused > 0) {
        --cycler->numTimesPaused;
    }
}

// Mettre en pause un cycler
void GfxPalette32::cyclePause(const uint8 fromColor) {
    PalCycler *const cycler = getCycler(fromColor);
    if (cycler != nullptr) {
        ++cycler->numTimesPaused;
    }
}

// Reprendre tous les cyclers
void GfxPalette32::cycleAllOn() {
    for (int i = 0; i < kNumCyclers; ++i) {
        PalCyclerOwner &cycler = _cyclers[i];
        if (cycler && cycler->numTimesPaused > 0) {
            --cycler->numTimesPaused;
        }
    }
}

// Mettre en pause tous les cyclers
void GfxPalette32::cycleAllPause() {
    // SSCI did not check for null pointers in the palette cyclers pointer array
    for (int i = 0; i < kNumCyclers; ++i) {
        PalCyclerOwner &cycler = _cyclers[i];
        if (cycler) {
            // This seems odd, because currentCycle is 0..numColorsPerCycle,
            // but fromColor is 0..255. When applyAllCycles runs, the values
            // end up back in range
            cycler->currentCycle = cycler->fromColor;
        }
    }

    applyAllCycles();

    for (int i = 0; i < kNumCyclers; ++i) {
        PalCyclerOwner &cycler = _cyclers[i];
        if (cycler) {
            ++cycler->numTimesPaused;
        }
    }
}

// Désactiver un cycler
void GfxPalette32::cycleOff(const uint8 fromColor) {
    for (int i = 0; i < kNumCyclers; ++i) {
        PalCyclerOwner &cycler = _cyclers[i];
        if (cycler && cycler->fromColor == fromColor) {
            clearCycleMap(fromColor, cycler->numColorsToCycle);
            _cyclers[i].reset();
            break;
        }
    }
}

// Désactiver tous les cyclers
void GfxPalette32::cycleAllOff() {
    for (int i = 0; i < kNumCyclers; ++i) {
        PalCyclerOwner &cycler = _cyclers[i];
        if (cycler) {
            clearCycleMap(cycler->fromColor, cycler->numColorsToCycle);
            _cyclers[i].reset();
        }
    }
}
```

---

## Palette Fading (Atténuation)

Le système de **fading** ajuste l'intensité des couleurs (de 0% = noir total à >100% = surexposition).

### Table de Fade

```cpp
// Fichier : engines/sci/graphics/palette32.h lignes 637-642
/**
 * An optional fade table used with QFG4 room 530 (Grue's Cave) that contains
 * a lookup table to determine the intensity of the color of each entry in the
 * palette.
 */
int16 _fadeTable[256];
```

### Configuration du Fade

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 1009-1029
void GfxPalette32::setFade(const uint16 percent, const uint8 fromColor, uint16 toColor) {
    if (fromColor > toColor) {
        return;
    }

    // Some game scripts (like LSL6 room 209, PS2 `Plane::SubInit`) accidentally
    // specify a fade range that includes the remap area
    if (toColor > 235) {
        toColor = 235;
    }

    assert(toColor < ARRAYSIZE(_fadeTable));

    for (int i = fromColor; i <= toColor; i++) {
        _fadeTable[i] = percent;
    }
}

void GfxPalette32::fadeOff() {
    setFade(100, 0, 255);
}
```

**Note** : Les scripts de jeu peuvent accidentellement spécifier une plage incluant la zone de remap (>235), donc `toColor` est limité à 235.

### Application du Fade

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 1031-1044
void GfxPalette32::applyFade() {
    for (int i = 0; i < ARRAYSIZE(_fadeTable); ++i) {
        if (_fadeTable[i] == 100) {
            continue;
        }

        Color &color = _nextPalette.colors[i];

        color.r = MIN(255, (uint16)color.r * _fadeTable[i] / 100);
        color.g = MIN(255, (uint16)color.g * _fadeTable[i] / 100);
        color.b = MIN(255, (uint16)color.b * _fadeTable[i] / 100);
    }
}
```

**Formule** :
```
color.r = MIN(255, color.r * fadePercent / 100)
```

---

## Correction Gamma

Le système de correction gamma ajuste la luminosité globale de la palette.

### Tables de Gamma

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 293-347
static const uint8 gammaCorrectionTables[5][256] = {
    // Gamma level 0 (darkest)
    {   0,   2,   3,   5,   6,   7,   8,  10,  11,  12,  13,  14,  15, ...},
    
    // Gamma level 1
    {   0,   4,   7,  10,  13,  15,  18,  20,  23,  25,  27,  29,  32, ...},
    
    // Gamma level 2 (Mac default)
    {   0,   9,  14,  19,  23,  27,  31,  35,  39,  43,  47,  51,  54, ...},
    
    // Gamma level 3
    {   0,  29,  43,  54,  63,  71,  78,  85,  92,  98, 103, 109, 114, ...},
    
    // Gamma level 4 (brightest)
    {   0,  71, 101, 122, 139, 153, 166, 178, 188, 198, 207, 216, 224, ...}
};
```

### Application de la Gamma

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 485-541
void GfxPalette32::updateHardware() {
    if (_gammaLevel == -1) {
        // Gamma correction is not enabled, just copy the palettes
        for (int i = 0; i < ARRAYSIZE(_nextPalette.colors); ++i) {
            Color &color = _nextPalette.colors[i];

            if (color.used) {
                bpal[i * 3    ] = _clut[color.r];
                bpal[i * 3 + 1] = _clut[color.g];
                bpal[i * 3 + 2] = _clut[color.b];
            }
        }

        _currentPalette = _nextPalette;
    } else {
        const uint8 *gammaTable = gammaCorrectionTables[_gammaLevel];

        for (int i = 0; i < ARRAYSIZE(_nextPalette.colors); ++i) {
            Color &color = _nextPalette.colors[i];

            if (color.used) {
                bpal[i * 3    ] = gammaTable[_clut[color.r]];
                bpal[i * 3 + 1] = gammaTable[_clut[color.g]];
                bpal[i * 3 + 2] = gammaTable[_clut[color.b]];
            }
        }

        _currentPalette = _nextPalette;
        applyGammaToCurrentPalette(gammaTable);
    }
}
```

**Processus** :
1. Si gamma = -1, copie directe avec table CLUT
2. Sinon, applique `gammaTable[_clut[color]]`
3. Sur Mac, le niveau par défaut est **2** (niveau moyen)

---

## Mise à Jour Matérielle (updateHardware)

### Fonction Complète

```cpp
// Fichier : engines/sci/graphics/palette32.cpp lignes 485-556
void GfxPalette32::updateHardware() {
    uint8 bpal[256 * 3];

    if (_gammaLevel == -1) {
        // Gamma correction is not enabled, just copy the palettes
        for (int i = 0; i < ARRAYSIZE(_nextPalette.colors); ++i) {
            Color &color = _nextPalette.colors[i];

            if (color.used) {
                bpal[i * 3    ] = _clut[color.r];
                bpal[i * 3 + 1] = _clut[color.g];
                bpal[i * 3 + 2] = _clut[color.b];
            }
        }

        _currentPalette = _nextPalette;
    } else {
        const uint8 *gammaTable = gammaCorrectionTables[_gammaLevel];

        for (int i = 0; i < ARRAYSIZE(_nextPalette.colors); ++i) {
            Color &color = _nextPalette.colors[i];

            if (color.used) {
                bpal[i * 3    ] = gammaTable[_clut[color.r]];
                bpal[i * 3 + 1] = gammaTable[_clut[color.g]];
                bpal[i * 3 + 2] = gammaTable[_clut[color.b]];
            }
        }

        _currentPalette = _nextPalette;
        applyGammaToCurrentPalette(gammaTable);
    }

    // NOTE: If the platform did not need to do endianness conversion then this
    // could be one memcpy instead of 256 * 3
#ifdef USE_RGB_COLOR
    const bool needsUpdate = memcmp(bpal, _hardwarePalette, sizeof(bpal));

    if (needsUpdate) {
        memcpy(_hardwarePalette, bpal, sizeof(bpal));
    }
#else
    const bool needsUpdate = true;
#endif

    // All color entries MUST be copied, not just "used" entries, otherwise
    // uninitialised memory from bpal makes its way into the system palette.
    // This would not normally be a problem, except that games sometimes use
    // unused palette entries. e.g. Phant1 title screen references palette
    // entries outside its own palette, so will render garbage colors where
    // the game expects them to be black
    if (needsUpdate && _hardwarePalette[255 * 3] != 255) {
        _hardwarePalette[255 * 3    ] = 255;
        _hardwarePalette[255 * 3 + 1] = 255;
        _hardwarePalette[255 * 3 + 2] = 255;
        bpal[255 * 3    ] = 255;
        bpal[255 * 3 + 1] = 255;
        bpal[255 * 3 + 2] = 255;
    }

    // When a game is in VESA mode, it expects the system to be able to set
    // the hardware palette without resetting video playback, so we need to
    // skip the call to OSystem when the screenFormat is >8bpp since OSystem
    // will reset the video playback when changing the palette
    // During playback, attempting to send the palette to OSystem is illegal
    // and will result in a crash
    if (g_system->getScreenFormat().bytesPerPixel == 1) {
        g_system->getPaletteManager()->setPalette(bpal, 0, 256);
    }

    _gammaChanged = false;
}
```

**Points clés** :
- **Toutes** les entrées de palette doivent être copiées (pas seulement les "used")
- L'entrée 255 est **forcée à blanc** (255, 255, 255)
- En mode 8bpp, la palette est envoyée à `g_system->getPaletteManager()`
- En mode >8bpp, la mise à jour est ignorée pour éviter de réinitialiser la lecture vidéo

---

## Processus Complet de Rendu

### Séquence Typique d'un Frame

```
1. Game Loop
   ↓
2. submit() - Soumission de palettes depuis ressources/bitmaps
   ↓
3. updateForFrame()
   ├─→ applyAll()
   │   ├─→ applyVary()     - Interpolation source→target
   │   ├─→ applyCycles()   - Rotation de plages de couleurs
   │   └─→ applyFade()     - Atténuation d'intensité
   ↓
4. remapAllTables() - Mise à jour des tables de remapping
   ↓
5. updateHardware()
   ├─→ Application gamma (si activée)
   ├─→ Copie vers _currentPalette
   └─→ Envoi à g_system->getPaletteManager()
```

### Exemple de Code d'Utilisation

```cpp
// Fichier : engines/sci/graphics/video32.cpp lignes 968-987
void VMDPlayer::submitPalette(const uint8 rawPalette[256 * 3]) const {
    // Cas 1 : Palette composée dans un bitmap
    if (_isComposited) {
        SciBitmap *bitmap = _segMan->lookupBitmap(_bitmapId);
        bitmap->setPalette(palette);
        // SSCI calls updateScreenItem and frameOut here, but this is not
        // necessary in ScummVM since the new palette gets submitted before the
        // next frame is rendered, and the frame rendering call will perform the
        // same operations.
    } 
    // Cas 2 : Palette globale
    else {
        g_sci->_gfxPalette32->submit(palette);
        g_sci->_gfxPalette32->updateForFrame();
        g_sci->_gfxPalette32->updateHardware();
    }
}
```

---

## Références du Code Source

### Fichiers Principaux

1. **engines/sci/graphics/palette32.h**
   - Lignes 28-672 : Classe `GfxPalette32` complète
   - Lignes 37-176 : Structure `HunkPalette`
   - Lignes 190-228 : Structure `PalCycler`
   - Lignes 239-672 : Déclarations de `GfxPalette32`

2. **engines/sci/graphics/palette32.cpp**
   - Lignes 293-347 : Tables de correction gamma
   - Lignes 358-391 : Constructeur `GfxPalette32()`
   - Lignes 397-408 : `loadPalette()`
   - Lignes 438-462 : `submit(const Palette &)`
   - Lignes 463-473 : `submit(const HunkPalette &)`
   - Lignes 475-481 : `updateForFrame()`
   - Lignes 485-556 : `updateHardware()`
   - Lignes 569-589 : `mergePalette()`
   - Lignes 583-589 : `applyAll()`
   - Lignes 593-670 : Fonctions de varying
   - Lignes 696-746 : `applyVary()`
   - Lignes 747-796 : Fonctions kernel de varying
   - Lignes 801-850 : `setCycle()`
   - Lignes 852-925 : Fonctions de contrôle des cyclers
   - Lignes 926-946 : `updateCycler()` et gestion de cycle map
   - Lignes 969-983 : `applyAllCycles()`
   - Lignes 985-1006 : `applyCycles()`
   - Lignes 1009-1029 : `setFade()` et `fadeOff()`
   - Lignes 1031-1044 : `applyFade()`

3. **engines/sci/engine/kgraphics32.cpp**
   - Lignes 941-953 : `kPaletteSetGamma()` et `kPaletteSetFade()`
   - Lignes 954-971 : `kPalVarySetVary()`
   - Lignes 972-987 : `kPalVarySetPercent()`, `kPalVaryGetPercent()`, `kPalVaryOff()`
   - Lignes 988-1005 : `kPalVaryMergeTarget()`, `kPalVarySetTime()`, `kPalVarySetTarget()`
   - Lignes 1006-1023 : `kPalVarySetStart()`, `kPalVaryMergeStart()`, `kPalCycle()`
   - Lignes 1024-1039 : `kPalCycleSetCycle()`, `kPalCycleDoCycle()`
   - Lignes 1040-1059 : `kPalCyclePause()`, `kPalCycleOn()`
   - Lignes 1060-1075 : `kPalCycleOff()`, `kRemapColors32()`

4. **engines/sci/engine/savegame.cpp**
   - Lignes 928-998 : `GfxPalette32::saveLoadWithSerializer()`

5. **engines/sci/graphics/video32.cpp**
   - Lignes 241-262 : `VideoPlayer::submitPalette()`
   - Lignes 945-998 : `VMDPlayer::submitPalette()`

---

## Résumé

Le système **GfxPalette32** est un gestionnaire sophistiqué de palettes pour SCI32 qui :

1. **Maintient trois palettes** : `_sourcePalette` (brute), `_nextPalette` (avec effets), `_currentPalette` (affichée)

2. **Supporte quatre types d'effets** :
   - **Varying** : Transition progressive entre deux palettes (0-100%)
   - **Cycling** : Rotation animée de plages de couleurs (max 10 cyclers)
   - **Fading** : Atténuation d'intensité (0-255%)
   - **Gamma** : Correction de luminosité (5 niveaux)

3. **Processus de mise à jour** :
   ```
   submit() → updateForFrame() → applyAll() → updateHardware()
                                    ├─ applyVary()
                                    ├─ applyCycles()
                                    └─ applyFade()
   ```

4. **Gère le versioning** pour détecter les changements de palette source

5. **Coordonne avec GfxRemap32** via la cycle map pour éviter les conflits

6. **Force l'entrée 255 à blanc** (255, 255, 255) pour compatibilité matérielle

Ce système permet de créer des effets visuels complexes comme des transitions jour/nuit, des animations d'eau, et des effets de lumière, tout en maintenant la compatibilité avec les jeux SCI32 originaux.
