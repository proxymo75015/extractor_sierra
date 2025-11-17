# Rapport de succès - Robot Decoder Extractor

## Résumé
Le parser RBT pour fichiers Robot de Sierra SCI a été complété avec succès et compilé sans erreur.

## État du projet

### ✅ Complété
- **Compilation** : Succès sans erreur
- **Parser d'en-tête RBT** : Implémentation complète
  - Détection automatique de l'endianness (Mac BE vs PC LE)
  - Lecture de tous les champs d'en-tête (version, résolution, framerate, palette, audio)
  - Support des versions 5 et 6
  - Gestion des données primer pour l'audio

- **Extraction de frames vidéo** :
  - Décompression LZS fonctionnelle
  - Export en format PGM (Netpbm grayscale)
  - Support multi-cels par frame
  - Gestion des chunks de décompression

- **Extraction audio** :
  - Décodage DPCM mono 22050Hz 16-bit
  - Export en format raw PCM
  - Gestion du padding kRobotZeroCompressSize

### 📊 Tests effectués
1. **Fichier 90.RBT** (67 frames)
   - Résolution: 640x390
   - Audio: 2,62 minutes (3 461 376 samples)
   - ✅ Toutes les frames extraites
   - ✅ Audio extrait avec succès

2. **Fichier 161.RBT** (29 frames)
   - Résolution: 100x147 à 101x155 (variable)
   - Audio: 22 secondes (485 440 samples)
   - ✅ Toutes les frames extraites
   - ✅ Audio extrait avec succès

### 📁 Fichiers générés
Pour chaque RBT analysé, le programme génère :
- `frames/frame_XXXX_cel_YY.pgm` : Images au format PGM
- `audio.raw.pcm` : Audio brut mono 22050Hz 16-bit (optionnel)
- `palette.bin` : Palette couleur 256 entrées RGB
- `metadata.txt` : Informations sur la vidéo
- `cues.txt` : Points de synchronisation audio/vidéo

### 🔧 Structure du code

#### Fichiers principaux
- `src/robot_decoder/rbt_parser.cpp/.h` : Parser principal
- `src/robot_decoder/main.cpp` : Programme d'extraction
- `src/robot_decoder/sci_util.cpp/.h` : Utilitaires endianness SCI11
- `src/robot_decoder/dpcm.cpp/.h` : Décodeur audio DPCM
- `src/robot_decoder/decompressor_lzs.cpp/.h` : Décompresseur LZS

#### Fonctions clés implémentées
1. `RbtParser::parseHeader()` - Analyse complète de l'en-tête
2. `RbtParser::extractFrame(idx, callback)` - Extraction d'une frame
3. `RbtParser::extractAllAudio(callback)` - Extraction audio complète
4. Helpers I/O: `readUint16LE/BE()`, `readSint32()`, `readUint32()`, `seekSet()`

### 🎯 Utilisation

```bash
# Extraction vidéo seulement
./robot_decoder <fichier.rbt> <dossier_sortie>

# Extraction vidéo + audio
./robot_decoder <fichier.rbt> <dossier_sortie> dummy audio
```

### 📝 Notes techniques

#### Détection d'endianness
Le système lit l'offset 6 en big-endian. Si la valeur est entre 1 et 255, 
c'est un fichier Mac (BE), sinon PC (LE).

#### Format RBT
- Signature: 0x16 (offset 0)
- Tag: "SOL\0" (offset 2-5)
- Version: 5 ou 6 (offset 6)
- Layout: header → palette → primer → records[video+audio]

#### Constantes
- `kRobotZeroCompressSize = 2048` : Padding pour blocs audio compressés
- Taux échantillonnage: 22050 Hz
- Format audio: DPCM mono 16-bit

### ⚠️ Avertissements de compilation (mineurs)
```
warning: ignoring return value of 'fread' [-Wunused-result]
```
Ces warnings concernent des lectures d'en-tête non critiques (hasPalette, hasAudio, palette).
Ils peuvent être ignorés ou corrigés en vérifiant les valeurs de retour.

### 🚀 Prochaines étapes possibles
- Ajouter la conversion automatique PGM → PNG avec application de la palette
- Créer un script d'assemblage des frames en vidéo (ffmpeg)
- Mixer l'audio RAW en WAV/MP3
- Interface graphique pour visualisation

## Conclusion
Le parser RBT est **fonctionnel et validé** sur plusieurs fichiers de test.
L'extraction complète (vidéo + audio) fonctionne correctement.

---
Date: 2024-11-17
Statut: ✅ SUCCÈS
