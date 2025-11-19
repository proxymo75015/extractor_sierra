# Synchronisation Audio/Vidéo dans le Format Robot

## Découverte Importante

**Les packets audio décompressés contiennent exactement 2205 samples = 100ms**, soit la durée exacte d'une frame vidéo à 10 fps.

### Ce que cela signifie

❌ **FAUX** (hypothèse initiale) :
- Packet compressé → 1024 samples → 46.4ms
- Gap de 53.6ms à interpoler par élongation temporelle
- Facteur d'élongation 2.15x

✅ **VRAI** (confirmé par test) :
- Packet compressé DPCM → décompression → **2205 samples → 100ms**
- **Aucun gap temporel**
- **Aucune élongation nécessaire**
- Synchronisation parfaite intrinsèque au format

## Rôle de l'Interpolation Linéaire

L'interpolation linéaire (`interpolateChannel()`) ne sert **PAS** à l'élongation temporelle, mais à **reconstruire l'entrelacement stéréo** dans le buffer circulaire.

### Structure du Buffer Circulaire

Le buffer utilise un **stride de 4** pour stocker les deux canaux (EVEN/ODD) :

```
Position dans le buffer:
0     1     2     3     4     5     6     7     8     9    10    11
EVEN  ?     ODD   ?     EVEN  ?     ODD   ?     EVEN  ?     ODD   ?
│           │           │           │           │           │
Gauche    Droite      Gauche      Droite      Gauche      Droite

Les "?" sont les positions intermédiaires calculées par interpolation
```

### Processus d'Interpolation

```cpp
// Pour le canal EVEN (positions 0, 4, 8, 12...)
interpolateChannel(buffer, numSamples, 0);
// Remplit les positions 2, 6, 10, 14... avec (sample[0]+sample[4])/2

// Pour le canal ODD (positions 2, 6, 10, 14...)  
interpolateChannel(buffer, numSamples, 1);
// Remplit les positions 1, 5, 9, 13... avec (sample[2]+sample[6])/2
```

## Synchronisation Temporelle Parfaite

### Données Mesurées (fichier 91.RBT)

- **90 frames vidéo** à 10 fps = 9.000 secondes
- **90 packets audio** décompressés
- **2205 samples/packet** × 90 = 198,450 samples total
- **198,450 / 22050 Hz** = 9.000 secondes
- **Différence** : 0.000 ms

### Résultat

```
✅ Chaque frame vidéo:      100 ms
✅ Chaque packet audio:     100 ms  
✅ Gap à interpoler:        0 ms
✅ Facteur d'élongation:    1.000x (aucune élongation)
```

## Pourquoi 2205 Samples ?

Le format DPCM16 de Sierra est conçu pour produire exactement la bonne quantité de samples après décompression :

$$\text{Samples par frame} = \frac{\text{Sample Rate}}{\text{FPS}} = \frac{22050}{10} = 2205$$

Cette synchronisation est **intrinsèque au design du format** :
- Les données DPCM compressées (~2213 bytes/packet)
- Se décompressent en exactement 2205 samples
- Qui durent exactement 100ms à 22050 Hz
- Correspondant parfaitement à une frame à 10 fps

## Architecture Audio Complète

### 1. Compression/Décompression DPCM

```
Packet compressé (2213 bytes)
        ↓
   deDPCM16Mono()
        ↓
2205 samples décompressés (4410 bytes)
        ↓
Durée: 100.00 ms
```

### 2. Écriture dans le Buffer Circulaire

```
copyEveryOtherSample()
        ↓
Écriture avec stride de 2 (entrelacement basique)
        ↓
Canal EVEN → positions 0, 2, 4, 6, 8...
Canal ODD  → positions 1, 3, 5, 7, 9...
```

### 3. Reconstruction Stéréo

```
interpolateChannel(EVEN) → remplit positions intermédiaires du canal gauche
interpolateChannel(ODD)  → remplit positions intermédiaires du canal droit
        ↓
Buffer avec stride de 4 complètement rempli
        ↓
Flux audio continu sans gaps
```

### 4. Lecture

```
readBuffer()
        ↓
Lit les samples dans l'ordre du buffer
        ↓
Flux mono 22050 Hz continu
        ↓
Parfaitement synchronisé avec les frames vidéo
```

## Implications

### Pour la Compréhension du Format

1. **Le format Robot est plus simple qu'on ne le pensait** :
   - Pas de time-stretching complexe
   - Pas d'algorithme d'élongation temporelle
   - Synchronisation native par design

2. **L'interpolation a un rôle purement spatial** :
   - Reconstruction de l'entrelacement buffer
   - Lissage des transitions entre samples
   - Amélioration de la qualité audio

3. **La "magie" est dans le DPCM** :
   - Le codec DPCM16 de Sierra produit exactement le bon nombre de samples
   - Le ratio compression/décompression est calibré pour la synchronisation
   - Format optimisé pour le jeu vidéo temps réel

### Pour l'Implémentation

1. **Le code est correct tel quel** :
   - `interpolateChannel()` fait exactement ce qu'elle doit faire
   - Le buffer circulaire fonctionne comme prévu
   - La synchronisation est automatique

2. **Pas besoin de modification** :
   - Aucun algorithme d'élongation à ajouter
   - Aucune compensation temporelle nécessaire
   - Le format garantit la synchronisation

3. **Optimisation possible** :
   - L'interpolation pourrait être désactivée sans impact sur la sync
   - Mais elle améliore la qualité audio (lissage)
   - Coût CPU négligeable (simple moyenne)

## Conclusion

Le format audio Robot de Sierra est **élégamment simple** :
- Compression DPCM optimisée pour produire le bon timing
- Entrelacement des canaux dans un buffer circulaire
- Interpolation linéaire pour reconstruire le flux continu
- Synchronisation parfaite garantie par le format lui-même

Aucune élongation temporelle n'est nécessaire car **le format est synchronisé par design**. 🎯
