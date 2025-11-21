# Tests et Programmes de Validation

Ce répertoire contient des programmes de test indépendants utilisés pour valider le décodage audio du format Robot RBT.

## Programmes

### `count_audio_frames.cpp`
Programme de validation qui lit un fichier Robot RBT et affiche :
- Le nombre total de frames audio
- Les positions absolues (`audioAbsolutePosition`) de chaque frame
- La classification EVEN/ODD de chaque position
- Les statistiques de distribution des canaux

**Compilation :**
```bash
g++ -std=c++20 -Iinclude -Isrc -o build/count_audio_frames tests/count_audio_frames.cpp src/utils/sci_util.cpp
```

**Utilisation :**
```bash
./build/count_audio_frames ScummVM/rbt/91.RBT
```

**Résultat attendu (91.RBT) :**
- 90 frames audio
- Positions de 39844 à 236089 (incrément de 2205)
- Distribution : 45 EVEN (50%), 45 ODD (50%)

---

### `decode_audio_headers.cpp`
Programme de validation qui décode tous les en-têtes audio d'un fichier Robot RBT :
- En-tête du primer audio (14 bytes) : taille totale, compression, tailles EVEN/ODD
- En-têtes des frames audio (8 bytes chacun) : position absolue, taille du bloc

**Compilation :**
```bash
g++ -std=c++20 -Iinclude -Isrc -o build/decode_audio_headers tests/decode_audio_headers.cpp src/utils/sci_util.cpp
```

**Utilisation :**
```bash
./build/decode_audio_headers ScummVM/rbt/91.RBT tests/audio_headers_decoded.txt
```

**Sortie :** Fichier texte avec le décodage de tous les en-têtes

---

## Fichiers de sortie

### `audio_headers_decoded.txt`
Fichier de sortie généré par `decode_audio_headers.cpp` contenant :
- Décodage du primer header (offsets 0-13)
- Décodage des 90 frame headers (8 bytes chacun)
- Uniquement les données brutes du fichier (pas de champs calculés)

---

## Notes

Ces programmes sont **100% indépendants** du code principal du projet. Ils servent uniquement à :
1. Valider la compréhension du format Robot RBT
2. Vérifier les implémentations du décodeur principal
3. Déboguer les problèmes de parsing audio
4. Documenter les structures de données du format

Ils ont été utilisés pour découvrir et corriger la différence entre :
- Méthode ScummVM : `audioAbsolutePosition % 4 == 0` pour EVEN (distribution déséquilibrée)
- Spécification Robot : `audioAbsolutePosition % 2 == 0` pour EVEN (distribution équilibrée 50/50)
