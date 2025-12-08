# Coordonnées Robot - Guide d'Utilisation

## 📄 Fichiers de Positions

### `robot_positions_default.txt` (RECOMMANDÉ)
Positions par défaut basées sur conventions Phantasmagoria:
- **Plein écran**: (0, 0) - pour FMV full screen
- **Centré**: (160, 100) - pour vidéos ~320×280 centrées

**Format:**
```
<RobotID> <X> <Y>  # commentaire optionnel
```

**Exemple:**
```
90   0   0  # plein écran
161 160 100  # centré
```

## 🎬 Utilisation avec export_robot_mkv

```bash
# Export avec positions par défaut
./build/export_robot_mkv --positions robot_positions_default.txt RBT/*.RBT output/

# Export Robot spécifique
./build/export_robot_mkv --positions robot_positions_default.txt RBT/90.RBT output/

# Export avec fichier personnalisé
./build/export_robot_mkv --positions mes_positions.txt RBT/*.RBT output/
```

## ✏️ Personnalisation des Positions

### Méthode 1: Édition Manuelle

1. Copier le fichier par défaut:
```bash
cp robot_positions_default.txt robot_positions_custom.txt
```

2. Éditer avec votre éditeur:
```bash
nano robot_positions_custom.txt
```

3. Modifier les coordonnées selon vos besoins:
```
90   0   0    # Garde plein écran
91  50  50    # Décale de 50px en X et Y  
161 200 150   # Recentre différemment
```

### Méthode 2: Inspection Visuelle

1. Extraire première frame de chaque Robot:
```bash
./build/robot_extractor RBT/161.RBT output/161_test/
```

2. Ouvrir `output/161_test/161_frames/frame_0000.png`

3. Déterminer visuellement:
   - **Plein écran**: Image remplit 640×480 → (0, 0)
   - **Centré petit**: ~320×280 au milieu → (160, 100)
   - **Autre**: Calculer offset manuellement

4. Mettre à jour fichier positions

## 📐 Calcul Manuel des Positions

### Formules
```
X_centré = (640 - largeur_robot) / 2
Y_centré = (480 - hauteur_robot) / 2
```

### Exemples

**Robot 320×280 centré:**
```
X = (640 - 320) / 2 = 160
Y = (480 - 280) / 2 = 100
```

**Robot 400×400 centré:**
```
X = (640 - 400) / 2 = 120
Y = (480 - 400) / 2 = 40
```

**Robot plein écran (640×480):**
```
X = 0
Y = 0
```

## 🔍 Vérification des Positions

### Après Export MKV

1. Ouvrir vidéo MKV avec VLC/mpv
2. Vérifier positionnement:
   - **Correct**: Vidéo alignée comme attendu
   - **Décalé**: Ajuster X/Y dans fichier positions
   - **Coupé**: Robot trop grand pour offset choisi

3. Ré-exporter si nécessaire:
```bash
# Corriger positions
nano robot_positions_custom.txt

# Ré-exporter
./build/export_robot_mkv --positions robot_positions_custom.txt RBT/161.RBT output/
```

## 📊 Robots Connus (Phantasmagoria)

| Robot ID | Description Probable | Position Par Défaut | Notes |
|----------|---------------------|---------------------|-------|
| 90 | Intro/Logo | 0, 0 | Plein écran |
| 91 | FMV cinématique | 0, 0 | Plein écran |
| 161 | Dialogue? | 160, 100 | Centré (à vérifier) |
| 162 | FMV cinématique | 0, 0 | Plein écran |
| 170 | FMV cinématique | 0, 0 | Plein écran |
| 260 | FMV cinématique | 0, 0 | Plein écran |

**Total estimé**: ~100-200 Robots dans le jeu complet

## 🛠️ Création Fichier Positions Complet

### Si Vous Avez TOUS les Robots

```bash
# Lister tous les RBT
ls RBT/*.RBT > robots_list.txt

# Créer fichier positions (tous plein écran par défaut)
cat robots_list.txt | while read rbt; do
    id=$(basename "$rbt" .RBT)
    echo "$id   0   0  # TODO: vérifier" >> robot_positions_full.txt
done

# Éditer manuellement les cas spéciaux
nano robot_positions_full.txt
```

### Template pour Affinage

```
# Format: RobotID X Y  # Commentaire
#
# Catégories:
# - FMV plein écran: 0 0
# - Dialogue centré: 160 100
# - Portrait: 320 140 (à gauche)
# - Autre: calculer manuellement

90   0   0    # Logo Sierra - plein écran ✓
91   0   0    # Intro mansion - plein écran ✓
161 160 100   # Adrienne dialogue - centré (à vérifier)
162   0   0   # Cinématique - plein écran ✓
170   0   0   # Flashback - plein écran ✓
260   0   0   # Fin - plein écran ✓
```

## ⚠️ Limitations

### Format RBT Non Documenté
- Coordonnées **NON extraites automatiquement** des fichiers RBT
- Format propriétaire Sierra (1995)
- Seul ScummVM décode en runtime

### Positions Estimées
- Fichier par défaut = **conventions basées sur analyse**
- **Pas de garantie 100%** sans inspection visuelle
- Majorité des FMV Phantasmagoria = plein écran (0, 0)

### Validation Recommandée
Pour projet critique:
1. Exporter TOUS les Robots
2. Inspecter visellement première frame
3. Ajuster positions manuellement
4. **Effort**: ~4-6 heures pour ~150 Robots

## 📞 Support

### Si Positions Incorrectes

1. **Identifier Robot problématique**:
   - Noter ID (ex: 161)
   - Noter décalage observé (ex: "trop à gauche")

2. **Calculer nouvelle position**:
   - Extraire frame: `./robot_extractor RBT/161.RBT output/test/`
   - Mesurer dimensions avec GIMP/Photoshop
   - Calculer centrage: `X = (640 - W) / 2`

3. **Mettre à jour fichier**:
   ```
   161 <X_calculé> <Y_calculé>  # ajusté manuellement
   ```

4. **Ré-exporter et valider**

### Contribution

Si vous créez un fichier `robot_positions_full.txt` complet:
- **Partagez-le** avec la communauté!
- Format: Markdown table avec ID, X, Y, Description
- Hébergement: GitHub, wiki ScummVM, etc.

## 📚 Voir Aussi

- **ROBOT_COORDINATES_INVESTIGATION.md**: Rapport technique détaillé
- **FORMAT_RBT_DOCUMENTATION.md**: Structure fichier RBT (partiel)
- **README.md**: Guide général du projet

---

**Dernière mise à jour**: 8 décembre 2025  
**Version fichier positions**: 1.0 (6 Robots connus)  

