# Notes sur l'Extraction Audio RBT

## Résumé des Modifications

Le code d'extraction audio dans `src/core/rbt_parser.cpp` a été simplifié et ajusté selon la documentation de référence (`LZS_DECODER_DOCUMENTATION.md` et `FORMAT_RBT_DOCUMENTATION.md`).

---

## Architecture Audio Robot

### Format de Base

Les fichiers Robot (.RBT) contiennent de l'audio **DPCM16 compressé** organisé en:

1. **Primers** (optionnels) : Données d'amorçage audio
   - Primer EVEN : ~19922 bytes compressés
   - Primer ODD : ~21024 bytes compressés
   
2. **Paquets audio par frame** : Données synchronisées avec la vidéo
   - Format : `[header 8 bytes][runway 8 bytes][données compressées]`
   - Taille typique : ~2213 bytes compressés par paquet

### Canaux EVEN et ODD

L'audio Robot utilise **2 canaux alternés** à 11025 Hz chacun :

- **Canal EVEN** : Paquets où `audioAbsolutePosition % 4 == 0`
- **Canal ODD** : Autres paquets

Les samples sont **entrelacés** pour produire du 22050 Hz mono :
```
Canal EVEN : positions 0, 2, 4, 6, 8...
Canal ODD  : positions 1, 3, 5, 7, 9...
```

---

## Décodage DPCM16

### Principe

DPCM (Differential Pulse Code Modulation) encode les **différences** entre samples :

```
Entrée : [delta₀][delta₁][delta₂]...
Sortie : [s₀][s₁][s₂]...

Où : sₙ = sₙ₋₁ + delta_signé(deltaₙ)
```

### Format des Deltas

Chaque octet d'entrée :
- **Bit 7** : Signe (0 = positif, 1 = négatif)
- **Bits 0-6** : Index dans `tableDPCM16[0..127]`

### Table DPCM16

Table de 128 valeurs delta prédéfinies (voir `src/formats/dpcm.cpp`) :
- Petites valeurs (0-63) : deltas fins (0x0000 à 0x03F8)
- Grandes valeurs (64-127) : deltas grossiers (0x0400 à 0x4000)

### Overflow x86

Le décodeur émule le comportement x86 16-bit signé :
```cpp
if (nextSample > 32767)   nextSample -= 65536;
if (nextSample < -32768)  nextSample += 65536;
```

---

## DPCM Runway

### Qu'est-ce que le Runway ?

Le **runway** est une séquence de **8 bytes** au début de chaque paquet audio qui sert à :
1. Initialiser le décodeur DPCM
2. Amener le signal à la bonne amplitude au 9ème sample

### Traitement

- **Décompression** : Les 8 bytes de runway sont TOUJOURS décompressés
- **Écriture** : Les 8 premiers samples NE SONT JAMAIS écrits dans le flux final
- **Raison** : Ce sont des samples de "mise en place" du signal, pas du contenu audio utile

### Dans le Code

```cpp
const size_t kRunwaySamples = 8;

// Décompresser TOUT le paquet (runway inclus)
deDPCM16Mono(samples.data(), compressedData.data(), dataSize, sampleValue);

// Écrire seulement APRÈS le runway
for (size_t s = kRunwaySamples; s < samples.size(); ++s) {
    audioBuffer[writePos] = samples[s];
    writePos += 2;  // Entrelacement
}
```

---

## Processus d'Extraction

### Étape 1 : Primers

```cpp
// Primers : décompression complète, PAS de runway à ignorer
deDPCM16Mono(evenSamples.data(), primerEvenData, evenPrimerSize, carry);

// Écriture aux positions paires (0, 2, 4, 6...)
for (size_t s = 0; s < evenSamples.size(); ++s) {
    audioBuffer[evenWritePos] = evenSamples[s];
    evenWritePos += 2;
}
```

### Étape 2 : Paquets de Frames

Pour chaque frame :

1. **Lire l'en-tête audio** (8 bytes)
   ```cpp
   int32_t audioAbsolutePosition = readSint32();
   int32_t audioBlockSize = readSint32();
   ```

2. **Déterminer le canal**
   ```cpp
   bool isEvenChannel = (audioAbsolutePosition % 4 == 0);
   ```

3. **Lire les données compressées**
   ```cpp
   fread(compressedData.data(), audioBlockSize, 1, file);
   ```

4. **Décompresser DPCM** (avec sample initial = 0)
   ```cpp
   int16_t sampleValue = 0;  // Réinitialisé pour chaque paquet
   deDPCM16Mono(samples.data(), compressedData.data(), audioBlockSize, sampleValue);
   ```

5. **Écrire en ignorant le runway**
   ```cpp
   for (size_t s = 8; s < samples.size(); ++s) {
       audioBuffer[writePos] = samples[s];
       writePos += 2;
   }
   ```

### Étape 3 : Interpolation (Optionnel)

Interpoler les gaps entre samples pour améliorer la qualité :

```cpp
// Pour chaque canal, interpoler les zéros entre voisins non-nuls
if (audioBuffer[i] == 0 && prev != 0 && next != 0) {
    audioBuffer[i] = (prev + next) / 2;
}
```

### Étape 4 : Écriture WAV

```cpp
writeWavHeader(file, 22050, 1, totalSamples);  // Mono 22050 Hz
fwrite(audioBuffer.data(), sizeof(int16_t), totalSamples, file);
```

---

## Simplifications Effectuées

### Suppression du Code Inutile

✅ **Supprimé** : Logique de padding 2048 bytes  
➡️ Non nécessaire pour un décodage basique

✅ **Supprimé** : Détection complexe de canaux avec multiples modulos  
➡️ Utilisation simple de `audioAbsolutePosition % 4 == 0`

✅ **Supprimé** : Compteurs de debug verbeux  
➡️ Logs minimaux et informatifs

✅ **Supprimé** : Interpolation complexe avec boucles imbriquées  
➡️ Interpolation simple par canal

### Code Conservé et Amélioré

✅ **Conservé** : Décompression DPCM16 avec table  
✅ **Conservé** : Gestion du runway (8 bytes)  
✅ **Conservé** : Entrelacement EVEN/ODD  
✅ **Amélioré** : Commentaires explicatifs basés sur la doc  
✅ **Amélioré** : Structure claire en 4 étapes

---

## Références

### Documentation Créée

1. **LZS_DECODER_DOCUMENTATION.md** : Algorithme de décompression LZS
2. **DPCM16_DECODER_DOCUMENTATION.md** : Algorithme DPCM16 détaillé
3. **FORMAT_RBT_DOCUMENTATION.md** : Structure complète du format RBT
4. **SOL_FILE_FORMAT_DOCUMENTATION.md** : Format de fichier SOL

### Code Source

- `src/core/rbt_parser.cpp` : Parseur et extracteur RBT
- `src/formats/dpcm.cpp` : Décodeur DPCM16
- `src/formats/decompressor_lzs.cpp` : Décodeur LZS

### Référence Externe

- ScummVM : `engines/sci/video/robot_decoder.{h,cpp}`
- ScummVM : `engines/sci/sound/decoders/sol.{h,cpp}`

---

## Exemple d'Utilisation

```cpp
// Ouvrir un fichier RBT
FILE *f = fopen("video.rbt", "rb");
RbtParser parser(f);

// Parser l'en-tête
if (!parser.parseHeader()) {
    fprintf(stderr, "Erreur de parsing\n");
    return 1;
}

// Extraire l'audio complet
if (parser.hasAudio()) {
    parser.extractAudio("./output");  // Crée output/audio.wav
}

// Ou extraire seulement les 100 premières frames
parser.extractAudio("./output", 100);

fclose(f);
```

---

## Notes Techniques

### Taux d'Échantillonnage

- **Canaux individuels** : 11025 Hz (EVEN et ODD)
- **Signal final** : 22050 Hz (entrelacé)
- **Samples par frame** : ~2205 @ 10 fps (0.1 seconde)

### Taille des Données

- **Primer EVEN** : ~19922 bytes → 19922 samples
- **Primer ODD** : ~21024 bytes → 21024 samples
- **Paquet régulier** : ~2213 bytes → 2213 samples (8 runway + 2205 utiles)

### Qualité Audio

- **Format** : PCM 16-bit signé
- **Fréquence** : 22050 Hz
- **Canaux** : Mono (entrelacement EVEN/ODD)
- **Compression** : ~2:1 (DPCM16 utilise 1 byte par sample au lieu de 2)

---

**Date de création** : Novembre 2024  
**Basé sur** : Documentation de référence ScummVM  
**Version** : 1.0
