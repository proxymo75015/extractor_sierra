Robot Extractor
===============

Outil d'extraction pour les fichiers Sierra Robot (`.rbt`) compatible avec la logique de décodage de ScummVM (versions 4, 5 et 6). Exporte les cels en PNG, l'audio en WAV stéréo 16-bit 22.05 kHz (échantillons pairs → canal gauche, impairs → canal droit) et un fichier `metadata.json`.

**Structure**
- **`src/`** : code source principal (`robot_extractor.cpp`, `robot_extractor.hpp`).
- **`include/`** : en-têtes tiers embarqués (ex. `stb_image_write.h`).
- **`tests/`** : tests unitaires (Catch2). (anciennement `tests_new`)
- **`ScummVM/`** : références et exemples `.rbt` (NE PAS modifier).
- **`CMakeLists.txt`** : configuration de build.
- **`readme.md`** : ce document.

**Prérequis**
- Compilateur C++ avec support C++20 (GCC 11+, Clang 14+, MSVC récent).
- `cmake` (>= 3.20 recommandé).
- La dépendance `nlohmann_json` est récupérée automatiquement par CMake si absente.
- `stb_image_write.h` est fourni dans `include/`.

**Build (développement / exécution des tests)**
1. Configurer et compiler (avec les tests) :
```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build -j$(nproc)
```
2. Lancer les tests :
```bash
./build/tests
# ou via ctest
ctest --test-dir build --output-on-failure
```

**Build de l'exécutable `robot_extractor`**
Par défaut la configuration ci‑dessus construit l'exécutable de tests qui inclut le code d'extraction. Pour générer l'exécutable autonome `robot_extractor`, configurez sans les tests :
```bash
cmake -S . -B build -DBUILD_TESTS=OFF
cmake --build build -j$(nproc)
# alors ./build/robot_extractor sera généré
```

**Utilisation minimale**
```bash
./build/robot_extractor [--audio] [--quiet] [--force-be|--force-le] <input.rbt> <output_dir>
```

- `--audio` : exporte les pistes audio en WAV.
- `--quiet` : réduit la sortie console.
- `--force-be` / `--force-le` : forcer l'endianness (mutuellement exclusifs).

Sorties produites dans `<output_dir>`
- fichiers PNG pour chaque cel : `frame_XXXXX_N.png`
- fichiers WAV (si `--audio`) : `frame_XXXXX.wav` (stéréo 16-bit, 22.05 kHz)
- `metadata.json` : métadonnées des frames/cels

**Remarques**
- Le répertoire `ScummVM/` contient des exemples `.rbt` et n'a pas été modifié par les opérations de nettoyage : il doit rester intact.
- Le fichier racine `robot_extractor.hpp` (doublon) a été supprimé ; l'en‑tête actif est `src/robot_extractor.hpp`.

**Licence**
BSD 3‑Clause License

*Fin*
