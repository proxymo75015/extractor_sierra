# Robot - Composition avec Décor Virtuel (Background)

## Table des Matières

1. [Vue d'ensemble](#vue-densemble)
2. [Architecture du Système](#architecture-du-système)
3. [Système de Coordonnées](#système-de-coordonnées)
4. [Intégration avec le Moteur de Jeu](#intégration-avec-le-moteur-de-jeu)
5. [Positionnement et Mise à l'Échelle](#positionnement-et-mise-à-léchelle)
6. [Flux de Rendu](#flux-de-rendu)
7. [Exemples de Code](#exemples-de-code)
8. [Limitations et Considérations](#limitations-et-considérations)

---

## Vue d'ensemble

### Concept Fondamental

**Les fichiers Robot (.rbt) ne contiennent PAS de décor/background**. Contrairement aux formats AV traditionnels, les animations Robot sont conçues pour être composées avec les éléments graphiques du jeu :

> *"Unlike traditional AV formats, Robot videos almost always require playback within the game engine because certain information (like the resolution of the Robot coordinates and the background for the video) is dependent on data that does not exist within the Robot file itself."*
>
> — `robot_decoder.h`, lignes 48-52

### Données Contenues dans Robot

Un fichier Robot contient uniquement :
- **Cels vidéo** : les pixels de l'animation
- **Données audio** : piste sonore synchronisée
- **Informations de positionnement** : coordonnées relatives des cels
- **Métadonnées** : résolution, frame rate, palette

### Provenance du Décor

Le décor virtuel provient du **moteur de jeu SCI** :
- **Planes** : couches de rendu organisées par priorité
- **ScreenItems** : éléments graphiques individuels (sprites, backgrounds, etc.)
- **Scènes du jeu** : backgrounds statiques ou animés

---

## Architecture du Système

### Hiérarchie de Rendu SCI32

Le système SCI32 utilise une architecture à plusieurs couches :

```
┌──────────────────────────────────┐
│      Écran Final (Screen)        │  ← Sortie finale
└──────────────────────────────────┘
              ↑
              │ Composition
┌──────────────────────────────────┐
│    GfxFrameout (Frame Manager)   │  ← Gestionnaire de rendu
└──────────────────────────────────┘
              ↑
              │ Gère
┌──────────────────────────────────┐
│       Planes (Couches)           │  ← Priorités de superposition
│  - Background Plane (priorité 0) │
│  - Robot Plane (priorité N)      │
│  - UI Plane (priorité max)       │
└──────────────────────────────────┘
              ↑
              │ Contient
┌──────────────────────────────────┐
│   ScreenItems (Éléments)         │  ← Sprites, cels, bitmaps
│  - Background ScreenItems        │
│  - Robot Cel ScreenItems         │
│  - Game Object ScreenItems       │
└──────────────────────────────────┘
```

### Composants Clés du RobotDecoder

```cpp
/**
 * Le plane où l'animation robot sera dessinée.
 * Détermine la superposition avec les autres éléments du jeu.
 */
Plane *_plane;

/**
 * Liste de pointeurs vers les ScreenItems utilisés par le robot.
 * Chaque cel devient un ScreenItem distinct.
 */
RobotScreenItemList _screenItemList;

/**
 * Position de base du robot dans le plan, en coordonnées d'écran.
 */
Common::Point _position;

/**
 * Résolution native du robot (0 = utilise les coordonnées du jeu).
 */
int16 _xResolution, _yResolution;
```

Source : `robot_decoder.h`, lignes 1224-1349

---

## Système de Coordonnées

### Types de Coordonnées

Robot gère **trois systèmes de coordonnées différents** :

#### 1. Coordonnées Robot (Robot Coordinates)

Définies dans l'en-tête du fichier Robot :

```cpp
// Offset 20-21 : X-resolution (si 0, utilise coordonnées du jeu)
// Offset 22-23 : Y-resolution (si 0, utilise coordonnées du jeu)
_xResolution = _stream->readSint16();
_yResolution = _stream->readSint16();
```

**Logique d'initialisation** :

```cpp
if (_xResolution == 0 || _yResolution == 0) {
    // Si non spécifié, utilise la résolution actuelle du jeu
    _xResolution = g_sci->_gfxFrameout->getScreenWidth();
    _yResolution = g_sci->_gfxFrameout->getScreenHeight();
}
```

Source : `robot_decoder.cpp`, lignes 458-462

#### 2. Coordonnées Script (Script Coordinates)

Coordonnées logiques utilisées par les scripts du jeu :
- **Basse résolution** : 320×200 (kLowResX, kLowResY)
- **Haute résolution** : variable selon le jeu

#### 3. Coordonnées Écran (Screen Coordinates)

Coordonnées physiques de l'écran final :
- Résolution réelle du rendu (ex : 640×480)
- Peut différer des coordonnées script

### Conversion Entre Systèmes

#### Ratios de Conversion

```cpp
const int16 scriptWidth = g_sci->_gfxFrameout->getScriptWidth();
const int16 scriptHeight = g_sci->_gfxFrameout->getScriptHeight();
const int16 screenWidth = g_sci->_gfxFrameout->getScreenWidth();
const int16 screenHeight = g_sci->_gfxFrameout->getScreenHeight();

// Ratios basse résolution → écran
const Ratio lowResToScreenX(screenWidth, kLowResX);   // ex: 640/320 = 2
const Ratio lowResToScreenY(screenHeight, kLowResY);  // ex: 480/200 = 2.4

// Ratios écran → basse résolution
const Ratio screenToLowResX(kLowResX, screenWidth);   // ex: 320/640 = 0.5
const Ratio screenToLowResY(kLowResY, screenHeight);  // ex: 200/480 ≈ 0.417
```

Source : `robot_decoder.cpp`, lignes 1470-1476

#### Calcul de Position (Basse Résolution)

```cpp
// Position mise à l'échelle vers l'écran
const int16 scaledX = celPosition.x + (_position.x * lowResToScreenX).toInt();
const int16 scaledY1 = celPosition.y + (_position.y * lowResToScreenY).toInt();
const int16 scaledY2 = scaledY1 + celHeight - 1;

// Conversion inverse vers basse résolution pour le système de script
const int16 lowResX = (scaledX * screenToLowResX).toInt();
const int16 lowResY = (scaledY2 * screenToLowResY).toInt();

// Calcul de l'origine du bitmap (pour sous-pixel accuracy)
origin.x = (scaledX - (lowResX * lowResToScreenX).toInt()) * -1;
origin.y = (lowResY * lowResToScreenY).toInt() - scaledY1;

// Stockage des coordonnées finales
_screenItemX[i] = lowResX;
_screenItemY[i] = lowResY;
```

Source : `robot_decoder.cpp`, lignes 1477-1492

**Explication** :
- `origin.x` et `origin.y` compensent les arrondis de conversion
- Permet un positionnement sub-pixel précis
- Évite les décalages cumulatifs

#### Calcul de Position (Haute Résolution)

```cpp
const int16 highResX = celPosition.x + _position.x;
const int16 highResY = celPosition.y + _position.y + celHeight - 1;

origin.x = 0;
origin.y = celHeight - 1;
_screenItemX[i] = highResX;
_screenItemY[i] = highResY;
```

Source : `robot_decoder.cpp`, lignes 1493-1501

**Haute résolution = conversion simplifiée** : pas de ratio complexe.

### Flag de Conversion

```cpp
// Offset 30-31 : flag de conversion
// Si true, utilise les coordonnées robot telles quelles SANS conversion
// lors de l'affichage explicite d'une frame spécifique
bool _isHiRes = (bool)_stream->readSint16();
```

Source : `robot_decoder.h`, lignes 94-96

---

## Intégration avec le Moteur de Jeu

### Initialisation du Plan de Rendu

```cpp
void RobotDecoder::initVideo(const int16 x, const int16 y, 
                             const int16 scale, const reg_t plane,
                             const bool hasPalette, const uint16 paletteSize)
{
    // Position de base du robot
    _position = Common::Point(x, y);

    // Configuration de mise à l'échelle
    _scaleInfo.x = scale;
    _scaleInfo.y = scale;
    _scaleInfo.signal = scale == 128 ? kScaleSignalNone : kScaleSignalManual;

    // Récupération du plane depuis le moteur graphique
    _plane = g_sci->_gfxFrameout->getPlanes().findByObject(plane);
    if (_plane == nullptr) {
        error("Invalid plane %04x:%04x", PRINT_REG(plane));
    }

    _planeId = plane;
    // ...
}
```

Source : `robot_decoder.cpp`, lignes 441-455

### Création des ScreenItems

Chaque cel du robot devient un **ScreenItem** dans le plane :

```cpp
void RobotDecoder::doVersion5(const bool shouldSubmitAudio)
{
    // Redimensionner les listes si nécessaire
    if (screenItemCount > _screenItemList.size()) {
        _screenItemList.resize(screenItemCount);
        _screenItemX.resize(screenItemCount);
        _screenItemY.resize(screenItemCount);
        _originalScreenItemX.resize(screenItemCount);
        _originalScreenItemY.resize(screenItemCount);
    }

    // Créer les cels
    createCels5(videoFrameData + 2, screenItemCount, true);
    
    // Pour chaque cel...
    for (size_t i = 0; i < screenItemCount; ++i) {
        Common::Point position(_screenItemX[i], _screenItemY[i]);

        // Appliquer la mise à l'échelle manuelle si activée
        if (_scaleInfo.signal == kScaleSignalManual) {
            position.x = (position.x * _scaleInfo.x) / 128;
            position.y = (position.y * _scaleInfo.y) / 128;
        }

        // Si le ScreenItem n'existe pas encore, le créer
        if (_screenItemList[i] == nullptr) {
            CelInfo32 celInfo;
            celInfo.bitmap = _celHandles[i].bitmapId;
            celInfo.type = kCelTypeMem;
            celInfo.color = 0;

            _screenItemList[i] = new ScreenItem(_planeId, celInfo, position);
            _screenItemList[i]->_priority = _priority;
            _screenItemList[i]->_fixedPriority = true;

            // AJOUT AU SYSTÈME DE RENDU
            g_sci->_gfxFrameout->addScreenItem(_screenItemList[i]);
        }
        // Sinon, mettre à jour le ScreenItem existant
        else {
            _screenItemList[i]->_position = position;
        }
    }
}
```

Source : `robot_decoder.cpp`, lignes 1391-1410

### Mise à Jour des Frames

```cpp
void RobotDecoder::showFrame(const uint16 frameNo, 
                              const uint16 newX, const uint16 newY,
                              const uint16 newPriority)
{
    // Pour chaque cel de la frame...
    for (size_t i = 0; i < _screenItemList.size(); ++i) {
        SciBitmap &bitmap = *_segMan->lookupBitmap(_celHandles[i].bitmapId);

        // Conversion de coordonnées et calcul de l'origine
        // (code de conversion détaillé précédemment)
        
        if (_screenItemList[i] == nullptr) {
            // Créer nouveau ScreenItem
            CelInfo32 celInfo;
            celInfo.type = kCelTypeMem;
            celInfo.bitmap = _celHandles[i].bitmapId;
            
            _screenItemList[i] = new ScreenItem(_planeId, celInfo, 
                                               Common::Point(_screenItemX[i], 
                                                           _screenItemY[i]));
            _screenItemList[i]->_priority = newPriority;
            _screenItemList[i]->_fixedPriority = true;
            
            // INTÉGRATION DANS LA SCÈNE
            g_sci->_gfxFrameout->addScreenItem(_screenItemList[i]);
        }
        else {
            // Mettre à jour position existante
            _screenItemList[i]->_position = Common::Point(_screenItemX[i], 
                                                         _screenItemY[i]);
        }
    }
}
```

Source : `robot_decoder.cpp`, lignes 700-729

---

## Positionnement et Mise à l'Échelle

### Position de Base

```cpp
/**
 * L'origine du robot dans le plan, en coordonnées d'écran.
 */
Common::Point _position;

// Initialisé par :
_position = Common::Point(x, y);
```

### Positions Individuelles des Cels

Chaque cel a **deux ensembles de coordonnées** :

```cpp
/**
 * Positions en coordonnées d'écran (finales, après conversion).
 */
Common::Array<int16> _screenItemX, _screenItemY;

/**
 * Valeurs brutes depuis l'en-tête du cel (positions relatives).
 */
Common::Array<int16> _originalScreenItemX, _originalScreenItemY;
```

Source : `robot_decoder.h`, lignes 1337-1349

### Système de Mise à l'Échelle

```cpp
/**
 * Information de mise à l'échelle globale appliquée au robot.
 */
ScaleInfo _scaleInfo;

// Structure ScaleInfo :
struct ScaleInfo {
    int16 x;        // Facteur d'échelle X (base 128 = 100%)
    int16 y;        // Facteur d'échelle Y (base 128 = 100%)
    int signal;     // Type d'échelle (kScaleSignalNone, kScaleSignalManual)
};
```

**Échelle de base** : `128 = 100%`

**Exemples** :
- `scale = 128` → 100% (taille normale)
- `scale = 64` → 50% (moitié de la taille)
- `scale = 256` → 200% (double taille)

**Application** :

```cpp
if (_scaleInfo.signal == kScaleSignalManual) {
    position.x = (position.x * _scaleInfo.x) / 128;
    position.y = (position.y * _scaleInfo.y) / 128;
}
```

Source : `robot_decoder.cpp`, lignes 1405-1408

### Priorité de Rendu

```cpp
/**
 * La priorité visuelle du robot (détermine l'ordre de superposition).
 * @see ScreenItem::_priority
 */
int16 _priority;

// Appliqué aux ScreenItems :
_screenItemList[i]->_priority = _priority;
_screenItemList[i]->_fixedPriority = true;
```

- **Priorité basse** : rendu derrière d'autres éléments
- **Priorité haute** : rendu devant d'autres éléments
- **fixedPriority = true** : priorité ne change pas automatiquement

---

## Flux de Rendu

### Séquence Complète

```
1. INITIALISATION
   ├─ open() : Charge le fichier .rbt
   ├─ initVideo() : Configure plane, position, échelle
   └─ initRecordAndCuePositions() : Prépare index des frames

2. LECTURE DE FRAME
   ├─ doRobot() : Pompe principale de rendu
   ├─ doVersion5() : Rendu spécifique version 5
   │   ├─ createCels5() : Décompresse et crée les bitmaps
   │   └─ Pour chaque cel :
   │       ├─ Calcul position (avec conversion coordonnées)
   │       ├─ Application de l'échelle
   │       ├─ Création/mise à jour ScreenItem
   │       └─ addScreenItem() : Ajout au système de rendu
   └─ frameNowVisible() : Ajustements AV sync

3. COMPOSITION PAR GFXFRAMEOUT
   ├─ GfxFrameout parcourt tous les Planes (par priorité)
   ├─ Pour chaque Plane :
   │   ├─ Parcourt tous les ScreenItems
   │   ├─ Trie par priorité
   │   └─ Dessine dans l'ordre :
   │       ├─ Background ScreenItems (priorité basse)
   │       ├─ Robot ScreenItems (priorité moyenne)
   │       └─ UI ScreenItems (priorité haute)
   └─ Affichage du buffer final à l'écran

4. NETTOYAGE
   └─ close() : Libère les ScreenItems et ferme le fichier
```

### Fonction de Pompage Principale

```cpp
void RobotDecoder::doRobot()
{
    // Gestion du timing et du frame skipping
    // ...

    if (_version == 5 || _version == 6) {
        doVersion5(shouldSubmitAudio);
    }

    // Soumission audio
    if (shouldSubmitAudio) {
        frameAlmostVisible();
    }

    // Ajustements AV sync
    frameNowVisible();

    _previousFrameNo = _currentFrameNo;
}
```

Source : `robot_decoder.cpp`, lignes 1192-1233

### Intégration dans la Boucle de Jeu

```
┌────────────────────────────────┐
│   Boucle Principale du Jeu     │
└────────────────────────────────┘
              │
              ├─ Lecture des entrées utilisateur
              ├─ Mise à jour de la logique du jeu
              ├─ doRobot() ◄───────────────┐
              │    │                       │
              │    ├─ Décompresse cels    │
              │    ├─ Met à jour ScreenItems │
              │    └─ Ajoute au système   │
              │                            │
              ├─ GfxFrameout::kernelFrameout()
              │    │                       │
              │    ├─ Tri Planes/ScreenItems
              │    ├─ COMPOSITION :       │
              │    │   ├─ Fond de scène   │
              │    │   ├─ Cels Robot ◄────┘
              │    │   └─ UI              │
              │    └─ Affichage écran     │
              │                            │
              └─ Attente prochaine frame  │
```

---

## Exemples de Code

### Exemple 1 : Création d'un Cel avec Conversion Low-Res

```cpp
uint32 RobotDecoder::createCel5(const byte *rawVideoData, 
                                 const int16 screenItemIndex,
                                 const bool usePalette)
{
    // Lecture de l'en-tête du cel
    const int16 celWidth = READ_SCI11ENDIAN_UINT16(rawVideoData + 2);
    const int16 celHeight = READ_SCI11ENDIAN_UINT16(rawVideoData + 4);
    Common::Point celPosition(
        READ_SCI11ENDIAN_UINT16(rawVideoData + 10),
        READ_SCI11ENDIAN_UINT16(rawVideoData + 12)
    );

    // Récupération des résolutions
    const int16 scriptWidth = g_sci->_gfxFrameout->getScriptWidth();
    const int16 scriptHeight = g_sci->_gfxFrameout->getScriptHeight();
    const int16 screenWidth = g_sci->_gfxFrameout->getScreenWidth();
    const int16 screenHeight = g_sci->_gfxFrameout->getScreenHeight();

    Common::Point origin;
    
    if (scriptWidth == kLowResX && scriptHeight == kLowResY) {
        // MODE BASSE RÉSOLUTION (320×200)
        const Ratio lowResToScreenX(screenWidth, kLowResX);
        const Ratio lowResToScreenY(screenHeight, kLowResY);
        const Ratio screenToLowResX(kLowResX, screenWidth);
        const Ratio screenToLowResY(kLowResY, screenHeight);

        // Conversion vers coordonnées écran
        const int16 scaledX = celPosition.x + 
                             (_position.x * lowResToScreenX).toInt();
        const int16 scaledY1 = celPosition.y + 
                              (_position.y * lowResToScreenY).toInt();
        const int16 scaledY2 = scaledY1 + celHeight - 1;

        // Conversion inverse vers low-res
        const int16 lowResX = (scaledX * screenToLowResX).toInt();
        const int16 lowResY = (scaledY2 * screenToLowResY).toInt();

        // Calcul de l'origine pour précision sub-pixel
        origin.x = (scaledX - (lowResX * lowResToScreenX).toInt()) * -1;
        origin.y = (lowResY * lowResToScreenY).toInt() - scaledY1;
        
        _screenItemX[screenItemIndex] = lowResX;
        _screenItemY[screenItemIndex] = lowResY;
    }
    else {
        // MODE HAUTE RÉSOLUTION
        const int16 highResX = celPosition.x + _position.x;
        const int16 highResY = celPosition.y + _position.y + celHeight - 1;

        origin.x = 0;
        origin.y = celHeight - 1;
        
        _screenItemX[screenItemIndex] = highResX;
        _screenItemY[screenItemIndex] = highResY;
    }

    // Stockage des positions originales
    _originalScreenItemX[screenItemIndex] = celPosition.x;
    _originalScreenItemY[screenItemIndex] = celPosition.y;

    // Création du bitmap
    SciBitmap &bitmap = *_segMan->lookupBitmap(
                            _celHandles[screenItemIndex].bitmapId);
    bitmap.setOrigin(origin);

    // ... décompression des données vidéo ...
    
    return compressedSize;
}
```

Source : `robot_decoder.cpp`, lignes 1470-1514

### Exemple 2 : Récupération de la Taille d'une Frame

```cpp
uint16 RobotDecoder::getFrameSize(Common::Rect &outRect) const
{
    assert(_plane != nullptr);

    if (_screenItemList.size() == 0) {
        outRect.clip(0, 0);
        return _numFramesTotal;
    }

    // Commence avec le rectangle du premier ScreenItem
    outRect = _screenItemList[0]->getNowSeenRect(*_plane);
    
    // Étend pour englober tous les ScreenItems du robot
    for (size_t i = 1; i < _screenItemList.size(); ++i) {
        ScreenItem &screenItem = *_screenItemList[i];
        outRect.extend(screenItem.getNowSeenRect(*_plane));
    }

    return _numFramesTotal;
}
```

Source : `robot_decoder.cpp`, lignes 1179-1195

**Utilisation** : Permet de connaître l'emprise rectangulaire totale du robot dans le plan.

### Exemple 3 : Nettoyage des ScreenItems

```cpp
void RobotDecoder::close()
{
    // Libération des ScreenItems
    if (_plane != nullptr) {
        for (size_t i = 0; i < _screenItemList.size(); ++i) {
            if (_screenItemList[i] != nullptr) {
                // RETRAIT DU SYSTÈME DE RENDU
                g_sci->_gfxFrameout->deleteScreenItem(*_screenItemList[i]);
            }
        }
    }
    _screenItemList.clear();

    // Libération des autres ressources
    // ...
    
    _planeId = NULL_REG;
    _plane = nullptr;
    _status = kRobotStatusUninitialized;
}
```

Source : `robot_decoder.cpp`, lignes 606-626

---

## Limitations et Considérations

### 1. Dépendance au Moteur de Jeu

**Robot n'est PAS un format vidéo autonome** :

- Nécessite le moteur SCI pour la composition
- Les informations de plane et de priorité viennent du script
- La résolution peut être dynamique (déterminée au runtime)

**Conséquence** : Impossible de jouer un Robot sans émuler le moteur SCI.

### 2. Complexité des Conversions de Coordonnées

**Trois systèmes différents** :
- Coordonnées robot (natives du fichier)
- Coordonnées script (logique du jeu)
- Coordonnées écran (affichage final)

**Erreurs d'arrondissement** :
- Les ratios peuvent créer des imprécisions
- Le système `origin` compense les arrondis
- Essentiel pour éviter les décalages visuels

### 3. Gestion de la Résolution

```cpp
// Si xResolution == 0 ou yResolution == 0
// → utilise la résolution actuelle du jeu

if (_xResolution == 0 || _yResolution == 0) {
    _xResolution = g_sci->_gfxFrameout->getScreenWidth();
    _yResolution = g_sci->_gfxFrameout->getScreenHeight();
}
```

**Implication** : Le même fichier Robot peut s'afficher différemment selon la configuration du jeu.

### 4. Priorité de Rendu Fixe

```cpp
_screenItemList[i]->_priority = _priority;
_screenItemList[i]->_fixedPriority = true;
```

**Comportement** :
- La priorité est définie à l'ouverture du Robot
- Ne change pas pendant la lecture
- Tous les cels du robot ont la même priorité

**Limitation** : Pas de cels robot individuels avec des priorités différentes.

### 5. Nombre Maximum de Cels

```cpp
/**
 * Nombre maximum de ScreenItems simultanés à l'écran.
 */
enum {
    kScreenItemListSize = 10
};

/**
 * Nombre maximum de cels rendus par frame dans ce robot.
 */
int16 _maxCelsPerFrame;
```

Source : `robot_decoder.h`, lignes 489-498

**Contraintes** :
- Limité à 10 ScreenItems simultanés
- `_maxCelsPerFrame` lu depuis l'en-tête du Robot

### 6. Système de Mise à l'Échelle

**Échelle manuelle uniquement** :

```cpp
if (_scaleInfo.signal == kScaleSignalManual) {
    position.x = (position.x * _scaleInfo.x) / 128;
    position.y = (position.y * _scaleInfo.y) / 128;
}
```

**Pas d'échelle automatique** basée sur la profondeur ou la perspective.

### 7. Performance et Synchronisation AV

**Frame skipping** :

```cpp
/**
 * Le nombre maximum de paquets de données qui peuvent être sautés
 * sans provoquer de décrochage audio.
 */
int16 _maxSkippablePackets;
```

**Mécanisme** :
- Si la vidéo prend du retard, certaines frames peuvent être sautées
- L'audio continue de jouer
- Limite définie dans l'en-tête Robot

### 8. Gestion Mémoire

**Pré-allocation** :

```cpp
_screenItemList.reserve(kScreenItemListSize);
_maxCelArea.reserve(kFixedCelListSize);
_fixedCels.reserve(MIN(_maxCelsPerFrame, (int16)kFixedCelListSize));
_celDecompressionBuffer.reserve(_maxCelArea[0] + SciBitmap::getBitmapHeaderSize() + kRawPaletteSize);
```

Source : `robot_decoder.cpp`, lignes 467-475

**Stratégie** :
- Pré-allocation basée sur les métadonnées du Robot
- Évite les réallocations pendant la lecture
- Optimise les performances

---

## Références

### Fichiers Sources ScummVM

- **robot_decoder.h** : Déclarations et documentation du RobotDecoder
- **robot_decoder.cpp** : Implémentation complète
- **graphics/frameout.h** : Système de planes et ScreenItems (SCI32)
- **graphics/screen_item.h** : Définition des ScreenItems
- **graphics/plane.h** : Définition des planes

### Jeux Utilisant Robot

**Version 5** :
- King's Quest 7 (DOS)
- Phantasmagoria
- Police Quest: SWAT
- Lighthouse

**Version 6** :
- RAMA

### Documentation Connexe

- **ROBOT_FORMAT.md** : Format détaillé des fichiers .rbt
- **ROBOT_PALETTE_REMAPPING.md** : Gestion de la palette et remapping
- **GFXREMAP_SCI16.md** : Système de remapping SCI16

---

## Gestion de la Transparence et Pixels Invisibles

### Concept Fondamental : Skip Color

Pour permettre la superposition avec le décor, Robot utilise un système de **couleur de saut (skip color)** dans les bitmaps.

#### Skip Color dans SciBitmap

```cpp
/**
 * Obtient la couleur de saut (skip color) du bitmap.
 * Les pixels de cette couleur ne sont PAS dessinés.
 */
inline uint8 getSkipColor() const {
    return _data[8];
}

inline void setSkipColor(const uint8 skipColor) {
    _data[8] = skipColor;
}
```

Source : `segment.h`, lignes 1073-1089

**Position dans l'en-tête** : Offset 8 (1 byte)

### Mécanisme de Transparence

#### 1. Pixels Transparents

Tout pixel dont la valeur est égale à `skipColor` est **ignoré lors du rendu** :

```
┌───────────────────────────────────┐
│  Background Plane (décor du jeu)  │  ← Toujours visible
└───────────────────────────────────┘
              ↓
┌───────────────────────────────────┐
│    Robot Plane (vidéo Robot)      │  ← Pixels non-skip dessinés
│  ████░░██░░░█████░░░░░████████    │     ░ = skip color (transparent)
│  ████░░██░░░█████░░░░░████████    │     █ = pixels opaques
└───────────────────────────────────┘
              ↓
┌───────────────────────────────────┐
│      Rendu Final Composite        │
│  Les zones ░ laissent passer       │
│  le fond, les zones █ masquent     │
└───────────────────────────────────┘
```

#### 2. Algorithme de Rendu

**Pseudo-code du moteur SCI** :

```cpp
void renderRobotCel(SciBitmap &bitmap, ScreenItem *item) {
    const uint8 skipColor = bitmap.getSkipColor();
    const byte *pixels = bitmap.getPixels();
    
    for (int y = 0; y < bitmap.getHeight(); ++y) {
        for (int x = 0; x < bitmap.getWidth(); ++x) {
            byte pixel = pixels[y * bitmap.getWidth() + x];
            
            // TEST DE LA SKIP COLOR
            if (pixel == skipColor) {
                // NE PAS DESSINER CE PIXEL
                // Le pixel du background reste visible
                continue;
            }
            
            // Dessiner le pixel opaque
            screen.setPixel(x + item->_position.x, 
                          y + item->_position.y, 
                          pixel);
        }
    }
}
```

#### 3. Valeur Typique de Skip Color

**Convention SCI** :
- **Couleur 0** (noir) est souvent utilisée comme skip color
- Mais peut être n'importe quelle valeur de palette (0-255)
- Définie indépendamment pour chaque bitmap Robot

**Important** : Si le skip color est 0, **tous les pixels noirs seront transparents**.

### Pixels de Remapping (Toutes Versions SCI32)

#### Flag de Remapping

```cpp
/**
 * Indique si ce bitmap contient des pixels de remapping.
 * Toutes versions SCI32 (Robot v5 et v6).
 */
inline bool getRemap() const {
    return READ_SCI11ENDIAN_UINT16(_data + 10) & kBitmapRemap;
}

inline void setRemap(const bool remap) {
    uint16 flags = READ_SCI11ENDIAN_UINT16(_data + 10);
    if (remap) {
        flags |= kBitmapRemap;
    } else {
        flags &= ~kBitmapRemap;
    }
    WRITE_SCI11ENDIAN_UINT16(_data + 10, flags);
}
```

Source : `segment.h`, lignes 1085-1097

**Position dans l'en-tête** : Offset 10-11 (bit flag)

#### Pixels de Remapping : Un Troisième État

Dans toutes les versions SCI32, Robot peut avoir **trois types de pixels** :

1. **Pixels Opaques** : Valeur normale de palette → Dessine la couleur
2. **Skip Color** : Valeur == skipColor → Pixel transparent (fond visible)
3. **Pixels de Remap** : Valeurs spéciales → Recolorés dynamiquement

**Schéma** :

```
Bitmap Robot SCI32 (v5/v6) :
┌─────────────────────────────────┐
│ Pixel[0] = 15  → Opaque (bleu)  │
│ Pixel[1] = 0   → Skip (fond)    │
│ Pixel[2] = 246 → Remap (chair)  │  ← Couleur dynamique
│ Pixel[3] = 23  → Opaque (rouge) │
│ Pixel[4] = 0   → Skip (fond)    │
│ Pixel[5] = 247 → Remap (metal)  │  ← Couleur dynamique
└─────────────────────────────────┘
```

#### Plages de Remap Typiques

**Indices de palette réservés pour le remapping** (SCI32) :

- **Palette 0-235** : Couleurs normales
- **Palette 236-254** : Plages de remap (PC) - 19 couleurs remappables
- **Palette 237-254** : Plages de remap (Mac) - 18 couleurs remappables
- **Couleur 255** : Skip color (transparent) dans les Robots
- **Couleurs 0-235** : Pixels opaques (PC)

**Note** : Toutes les versions SCI32 (v5 et v6) utilisent **GfxRemap32**, le même système de remapping. Le skip color (255) est exclu de la plage de remap.

### Identification des Pixels Invisibles

#### Dans l'En-Tête du Bitmap

```
Offset | Size | Description
-------+------+----------------------------------
   0-1 |  2   | Width (largeur en pixels)
   2-3 |  2   | Height (hauteur en pixels)
   4-5 |  2   | Origin X
   6-7 |  2   | Origin Y
     8 |  1   | Skip Color ◄── COULEUR TRANSPARENTE
     9 |  1   | (unused)
 10-11 |  2   | Flags (bit 0 = remap) ◄── REMAP FLAG
```

#### Lecture Programmatique

```cpp
// Extraction de la skip color
uint8 skipColor = bitmapData[8];

// Test du flag remap
uint16 flags = READ_SCI11ENDIAN_UINT16(bitmapData + 10);
bool hasRemap = (flags & 0x0001) != 0;

// Pour chaque pixel
for (int i = 0; i < width * height; ++i) {
    uint8 pixel = pixels[i];
    
    if (pixel == skipColor) {
        // TRANSPARENT : fond visible
    }
    else if (hasRemap && isRemapColor(pixel)) {
        // REMAP : couleur dynamique
    }
    else {
        // OPAQUE : couleur normale
    }
}
```

### Exemples Concrets

#### Exemple 1 : Personnage avec Skip Color

**Fichier Robot** : personnage.rbt  
**Skip Color** : 255 (valeur standard Robot)

```
Bitmap du personnage :
┌────────────────┐
│ 255 255 255 255│  ← Fond transparent (skip color = 255)
│ 255 15 15 15255│  ← Contour de la tête
│ 255 15 32 15255│  ← Visage (couleur chair)
│ 255 15 15 15255│
│ 255 255 255 255│
└────────────────┘

Rendu final :
- Tous les pixels = 255 sont transparents
- Le décor du jeu est visible à travers
- Seul le visage et contour sont opaques
```

#### Exemple 2 : Robot SCI32 avec Remap

**Fichier Robot** : robot_kq7.rbt (v5) ou robot_rama.rbt (v6)  
**Skip Color** : 255 (valeur standard)  
**Remap** : Actif

```
Bitmap du robot :
┌────────────────┐
│ 255 255 255 255│  ← Transparent (skip color = 255)
│ 255 20 20 20255│  ← Corps métallique
│ 255 20 246 20255│  ← 246 = couleur remap (éclairage)
│ 255 20 20 20255│
│ 255 255 255 255│
└────────────────┘

Pendant le jeu :
- Pixel 255 : fond visible (transparent - skip color)
- Pixel 20 : couleur fixe (gris métallique - opaque)
- Pixel 246 : REMAPPÉ selon l'éclairage de la scène
  * Plage remap : 236-254 (PC) ou 237-254 (Mac)
  * Sombre → teinte bleue
  * Éclairé → teinte orangée
  * Géré par GfxRemap32
```

### Optimisation et Stockage

#### Compression et Skip Color

Le format **LZS** (compression Robot) compresse efficacement les zones de skip color :

```cpp
enum CompressionType {
    kCompressionLZS  = 0,  // LZS compressé
    kCompressionNone = 2   // Non compressé
};
```

**Avantage** : Les grandes zones de skip color (fond) se compressent très bien avec LZS.

**Taux de compression typique** :
- Zone opaque : 50-70%
- Zone skip color : 90-95% (répétitions de 0)

#### Taille Mémoire

```cpp
/**
 * Taille de la zone de décompression.
 */
int _celDecompressionArea;

// Pré-allocation basée sur la taille max
_celDecompressionBuffer.reserve(_maxCelArea[0] + 
                                SciBitmap::getBitmapHeaderSize() + 
                                kRawPaletteSize);
```

**Optimisation** : Le buffer de décompression est pré-alloué pour éviter les réallocations.

### Limitations et Considérations

#### 1. Une Seule Skip Color par Bitmap

**Limitation** : Impossible d'avoir plusieurs niveaux de transparence.

**Workaround** : Utiliser des cels séparés avec différentes skip colors.

#### 2. Pas de Transparence Alpha

**Pas de canal alpha** (pas de semi-transparence).

**Rendu** : 
- Pixel = skip color → 100% transparent
- Pixel ≠ skip color → 100% opaque

#### 3. Interaction avec Priority

```cpp
_screenItemList[i]->_priority = _priority;
_screenItemList[i]->_fixedPriority = true;
```

**Important** : 
- Les cels Robot ont une priorité fixe
- Mais les pixels skip respectent la transparence
- Le système de priorité ne s'applique qu'aux pixels opaques

#### 4. Performance

**Skip Color Test** : Effectué pour **chaque pixel**, à chaque frame.

**Optimisation possible** :
- Pré-calculer des masques de transparence
- Utiliser des dirty rects pour limiter la zone de rendu

### Différences avec Autres Formats

#### Robot vs. AVI/QuickTime

| Aspect | Robot | AVI/QT |
|--------|-------|--------|
| Transparence | Skip color (1 couleur) | Alpha channel (256 niveaux) |
| Contexte | Nécessite moteur SCI | Autonome |
| Fond | Externe (plane du jeu) | Intégré (ou aucun) |
| Remap | Oui (v6) | Non |

#### Robot vs. GIF

| Aspect | Robot | GIF |
|--------|-------|-----|
| Transparence | Skip color (définie par bitmap) | Index transparent (global) |
| Animation | Multi-cels + positioning | Frames séquentielles |
| Compression | LZS | LZW |
| Couleurs | 256 (palette) | 256 (palette) |

---

## Conclusion

Le système de composition de Robot avec le décor virtuel repose sur :

1. **Architecture à Couches** : Planes → ScreenItems → Bitmaps
2. **Conversion de Coordonnées** : Multi-système avec compensation d'arrondis
3. **Intégration Moteur** : Utilisation directe du GfxFrameout de SCI
4. **Positionnement Précis** : Système `origin` pour précision sub-pixel
5. **Gestion de Priorité** : Contrôle de la superposition avec les autres éléments
6. **Transparence via Skip Color** : Pixels invisibles définis par index de palette
7. **Remapping Dynamique** : Recoloration en temps réel (toutes versions SCI32)

**Point Clé** : Robot n'est pas un format vidéo autonome, mais un système d'animation intégré au moteur SCI32, conçu pour se composer dynamiquement avec les scènes du jeu. La transparence est gérée par une **skip color** unique par bitmap, permettant au décor du jeu de rester visible à travers les zones transparentes de la vidéo. Le système de remapping (**GfxRemap32**) est disponible dans toutes les versions SCI32, y compris Robot v5 et v6.
