# Extraction des positions Robot pour Phantasmagoria
# Guide étape par étape

## Méthode 1 : Via ScummVM Patché (Automatique)

### Prérequis
```bash
# Installer les dépendances de compilation ScummVM
sudo apt-get update
sudo apt-get install build-essential git libsdl2-dev
```

### Étapes

1. **Cloner ScummVM**
```bash
cd ~
git clone https://github.com/scummvm/scummvm.git
cd scummvm
```

2. **Appliquer le patch de débogage**
```bash
# Éditer engines/sci/engine/kvideo.cpp
nano engines/sci/engine/kvideo.cpp

# Trouver la fonction kRobotOpen (ligne ~266) et ajouter après "const int16 scale...":
warning("ROBOT %d: X=%d Y=%d priority=%d scale=%d", robotId, x, y, priority, scale);
```

3. **Compiler ScummVM**
```bash
./configure --enable-debug --disable-all-engines --enable-engine=sci
make -j$(nproc)
```

4. **Lancer Phantasmagoria et capturer les logs**
```bash
cd /workspaces/extractor_sierra
~/scummvm/scummvm --debugflags=all --debuglevel=1 phantasmagoria_game 2>&1 | tee robot_logs.txt
```

5. **Extraire les coordonnées**
```bash
grep "ROBOT" robot_logs.txt | grep -oP "ROBOT \K\d+ : X=\K-?\d+ Y=-?\d+"
```

---

## Méthode 2 : Analyse HEAP (Avancée)

Les coordonnées sont stockées dans les propriétés des objets Robot.

### Parser HEAP pour trouver les propriétés

```cpp
// À implémenter dans un nouveau fichier: src/sci_heap_parser.cpp
// Basé sur ScummVM engines/sci/engine/script.cpp

void parseHEAPForRobotProperties(const std::vector<uint8_t>& scriptData) {
    // Structure HEAP:
    // - Offset 0x00: signature "HEAP"
    // - Objects avec propriétés x, y
    
    // Rechercher les objets de classe Robot
    // Lire leurs propriétés initiales
}
```

---

## Méthode 3 : Extraction Manuelle (Simple)

### Pour chaque Robot ID (1000, 230, etc.)

1. **Lancer ScummVM** avec Phantasmagoria
2. **Jouer jusqu'à voir le Robot** apparaître
3. **Prendre screenshot** de la position
4. **Mesurer coordonnées** depuis le coin supérieur gauche
5. **Noter dans robot_positions.txt**

### Template robot_positions.txt
```
# Format: robot_id X Y
# Game resolution: 630x450
# Origin: top-left (0,0)

1000 150 143
230 200 180
91 175 150
```

---

## Méthode 4 : Valeurs par Défaut (Rapide)

Basé sur l'observation que la plupart des Robots sont centrés :

```python
# calc_default_positions.py
GAME_WIDTH = 630
GAME_HEIGHT = 450

# Pour un Robot RBT donné
def calculate_default_position(rbt_id):
    # Hypothèse: centrés horizontalement, positionnés au tiers supérieur
    x = (GAME_WIDTH - ROBOT_WIDTH) // 2  # ~150-200
    y = GAME_HEIGHT // 3  # ~150
    
    return x, y

# Robots connus de Phantasmagoria
robots = {
    1000: (150, 143),  # Vidéo d'introduction
    230: (180, 160),   # Scène de dialogue
    91: (175, 150),    # Cutscene
}
```

---

## Résumé des Découvertes

### Architecture SCI2.1
- **Coordonnées passées dynamiquement** via `kRobotOpen(robotId, plane, priority, x, y, scale)`
- **PAS dans les fichiers RBT** (header contient numFrames, pas X/Y)
- **PAS hardcodées dans scripts** (via propriétés d'objets)
- **Stockées dans HEAP** (section Local Variables des scripts)

### Headers RBT Décodés
```
Offset 0x00: version = 22
Offset 0x08: audioBlockSize = 2221
Offset 0x0E: numFramesTotal (143, 207, etc.)
Offset 0x14: xResolution = 0 (signifie "use game coordinates")
Offset 0x16: yResolution = 0
```

### Code ScummVM (engines/sci/engine/kvideo.cpp:266)
```cpp
reg_t kRobotOpen(EngineState *s, int argc, reg_t *argv) {
    const GuiResourceId robotId = argv[0].toUint16();
    const reg_t plane = argv[1];
    const int16 priority = argv[2].toSint16();
    const int16 x = argv[3].toSint16();  // ← X coordinate
    const int16 y = argv[4].toSint16();  // ← Y coordinate
    const int16 scale = argc > 5 ? argv[5].toSint16() : 128;
    g_sci->_video32->getRobotPlayer().open(robotId, plane, priority, x, y, scale);
    return make_reg(0, 0);
}
```

---

## Recommandation

**Pour une extraction rapide et fiable:**

1. Utilisez **Méthode 1** (ScummVM patché) si vous pouvez compiler
2. Sinon **Méthode 3** (extraction manuelle) pour 3-5 Robots principaux
3. Utilisez **Méthode 4** (valeurs par défaut) pour tester, puis affinez

**Fichier cible:** `robot_positions.txt` au format:
```
1000 150 143
230 180 160
91 175 150
```

Ensuite, modifiez `src/main.cpp` pour lire ce fichier et ajuster les positions lors de l'extraction.

