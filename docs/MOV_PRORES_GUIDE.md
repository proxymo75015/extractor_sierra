# Guide d'utilisation des fichiers MOV ProRes 4444

## 📋 Vue d'ensemble

L'extracteur génère des fichiers **MOV ProRes 4444 RGBA** avec canal alpha natif pour une utilisation professionnelle en post-production.

### Caractéristiques techniques

| Propriété | Valeur |
|-----------|--------|
| **Container** | QuickTime MOV |
| **Codec vidéo** | ProRes 4444 (`prores_ks`) |
| **Profile** | 4444 (ap4h) |
| **Pixel format** | `yuva444p10le` ou `yuva444p12le` |
| **Résolution** | Variable (normalisée par RBT) |
| **Framerate** | 15 fps (natif RBT) |
| **Canal alpha** | 10-12 bit natif |
| **Codec audio** | PCM S16LE (lossless) |
| **Fréquence audio** | 22050 Hz mono |
| **Qualité** | Quasi-lossless |

---

## 🎬 Utilisation dans les logiciels professionnels

### DaVinci Resolve (Recommandé - Gratuit)

**Import** :
1. Ouvrir DaVinci Resolve
2. Media Pool → Import Media
3. Sélectionner `*_composite.mov`
4. Glisser sur timeline

**Visualisation de l'alpha** :
- Timeline → Clic droit → Display Mode → Alpha Channel
- Background → Checkerboard pour voir transparence

**Export avec alpha** :
- Deliver → Format: QuickTime
- Codec: ProRes 4444 XQ (ou 4444)
- ✅ Include Alpha Channel

### Adobe Premiere Pro

**Import** :
1. File → Import
2. Sélectionner `*_composite.mov`
3. L'alpha est automatiquement reconnu

**Vérification alpha** :
- Effect Controls → Opacity → Toggle Track Output
- Program Monitor → Transparency Grid (Checkerboard)

**Réglages séquence** :
- Sequence Settings → Video Previews → ProRes 422 HQ
- Maximum Bit Depth : Activé
- Maximum Render Quality : Activé

### Adobe After Effects

**Import** :
1. File → Import → File
2. Sélectionner `*_composite.mov`
3. Interpret Footage → Alpha: Straight (Unmatted)

**Composition** :
- Composition → Background Color → Black
- Toggle Transparency Grid pour voir alpha
- Layer → Track Matte pour masquage avancé

### Final Cut Pro (macOS)

**Import** :
1. File → Import → Media
2. Sélectionner `*_composite.mov`
3. Glisser dans Browser puis Timeline

**Alpha** :
- Automatiquement reconnu
- Viewer → Show Transparency (Option+T)
- Inspector → Video → Blend Mode pour compositing

---

## 🔍 Dépannage

### Problème : "Pas d'image dans le MOV"

#### Diagnostic rapide

**Windows** : Exécuter `verify_mov.bat` (fourni dans le package)
```batch
verify_mov.bat
```
Ce script :
1. Trouve automatiquement le premier MOV
2. Affiche les propriétés codec
3. Extrait 3 frames PNG de test
4. Vous guide selon le résultat

**Linux/macOS** : Vérification manuelle
```bash
# Propriétés du MOV
ffprobe output/230/230_composite.mov

# Extraire frame de test
ffmpeg -i output/230/230_composite.mov \
  -vf "select=eq(n\,10)" \
  -vframes 1 test_frame.png
```

#### Causes possibles

**1. Lecteur vidéo incompatible**

❌ **Ne supportent PAS ProRes 4444 avec alpha** :
- VLC Media Player
- Windows Media Player
- MPV (configuration par défaut)

✅ **Lecteurs compatibles** :
- **DaVinci Resolve** (gratuit, recommandé)
- **Adobe Premiere Pro / After Effects**
- **Final Cut Pro** (macOS)
- **QuickTime Player** (macOS uniquement)
- **MPV** avec `--vo=gpu` :
  ```bash
  mpv --vo=gpu output/230/230_composite.mov
  ```

**Test** : Si les frames PNG extraites montrent l'image, le problème vient du lecteur.

**2. FFmpeg incomplet (Windows)**

Vérifier si FFmpeg supporte ProRes :
```batch
ffmpeg -codecs | findstr prores
```

**Attendu** :
```
DEV.L. prores          Apple ProRes (iCodec Pro)
```

**Si absent** :
1. Télécharger build FULL : https://www.gyan.dev/ffmpeg/builds/
2. Extraire dans `C:\ffmpeg\`
3. Ajouter `C:\ffmpeg\bin\` au PATH
4. Redémarrer le terminal
5. Relancer l'extraction

**3. Frames PNG corrompues**

Vérifier une frame source :
```bash
# Analyser frame PNG
python3 << EOF
from PIL import Image
img = Image.open('output/230/230_frames/frame_0010.png')
print(f"Mode: {img.mode}, Size: {img.size}")
pixels = list(img.getdata())
opaque = sum(1 for r,g,b,a in pixels if a == 255 and (r > 0 or g > 0 or b > 0))
print(f"Pixels colorés opaques: {opaque}/{len(pixels)}")
EOF
```

**Attendu** : Mode=RGBA, plusieurs milliers de pixels colorés

### Problème : "Alpha channel non reconnu"

**Vérification pixel format** :
```bash
ffprobe -v error -select_streams v:0 \
  -show_entries stream=pix_fmt \
  output/230/230_composite.mov
```

**Attendu** : `yuva444p10le` ou `yuva444p12le` (le "a" = alpha)

**Si `yuv444p` (sans alpha)** :
- Bug d'encodage FFmpeg
- Vérifier version FFmpeg : `ffmpeg -version`
- Utiliser build récent (2023+)

**Import dans logiciel** :
- After Effects : Interpret Footage → Alpha = **Straight (Unmatted)**
- Premiere : Sequence Settings → Transparency → **Straight Alpha**

### Problème : "Fichier très volumineux"

**Normal** :
- ProRes 4444 = ~70-100 MB pour 10 secondes
- H.264 MP4 = ~5-10 MB pour 10 secondes

**Compression sans perte d'alpha** :
```bash
# Réencoder en ProRes 422 HQ (sans alpha, plus léger)
ffmpeg -i input.mov \
  -c:v prores_ks -profile:v 3 \
  -pix_fmt yuv422p10le \
  -c:a copy \
  output_hq.mov
```

**Alternative avec alpha** (VP9 dans WebM) :
```bash
ffmpeg -i input.mov \
  -c:v libvpx-vp9 -pix_fmt yuva420p \
  -b:v 2M \
  -c:a libopus \
  output.webm
```

---

## 📊 Comparaison des formats

### Quand utiliser MOV ProRes 4444 ?

✅ **Oui** :
- Post-production professionnelle
- Compositing avec transparence
- Archivage qualité maximale
- Export vers autres logiciels pro
- Chromakey ou rotoscopie

❌ **Non** (utiliser MKV à la place) :
- Analyse technique des couches
- Besoin d'accès séparé BASE/REMAP/ALPHA
- Recoloration dynamique des zones REMAP
- Édition par couches

### MOV vs MKV : Tableau récapitulatif

| Critère | MOV ProRes 4444 | MKV Multi-track |
|---------|-----------------|-----------------|
| **Format** | Composite RGBA | 4 pistes séparées |
| **Transparence** | ✅ Canal alpha natif | ❌ Track ALPHA séparée |
| **Couches** | ❌ Fusionnées | ✅ BASE/REMAP/ALPHA/LUM |
| **Recoloration** | ❌ Impossible | ✅ Track REMAP éditable |
| **Compatibilité** | ✅ Tous logiciels pro | ⚠️ Lecteurs avancés |
| **Taille fichier** | ~100 MB/10s | ~20 MB/10s (H.264) |
| **Qualité** | Quasi-lossless | Variable (codec) |
| **Usage** | Post-prod, diffusion | Analyse, archivage tech |

**Recommandation** : **Conserver les deux formats** pour flexibilité maximale.

---

## 🛠 Commandes utiles

### Vérification rapide

```bash
# Propriétés complètes
ffprobe -v error -show_entries stream=codec_name,pix_fmt,width,height \
  output/230/230_composite.mov

# Extraire frame 10 en PNG
ffmpeg -i output/230/230_composite.mov \
  -vf "select=eq(n\,10)" \
  -vframes 1 frame_10.png

# Compter pixels transparents
python3 << 'EOF'
from PIL import Image
img = Image.open('frame_10.png').convert('RGBA')
pixels = list(img.getdata())
transparent = sum(1 for r,g,b,a in pixels if a == 0)
opaque = sum(1 for r,g,b,a in pixels if a == 255)
print(f"Transparents: {transparent}, Opaques: {opaque}")
EOF
```

### Conversion de format

```bash
# MOV → MP4 (perte de l'alpha)
ffmpeg -i input.mov -c:v libx264 -crf 18 -c:a aac output.mp4

# MOV → PNG séquence (avec alpha)
ffmpeg -i input.mov output_frames/frame_%04d.png

# MOV → GIF animé (avec transparence)
ffmpeg -i input.mov \
  -vf "fps=10,scale=320:-1:flags=lanczos,split[s0][s1];[s0]palettegen=reserve_transparent=1[p];[s1][p]paletteuse" \
  output.gif
```

### Édition rapide

```bash
# Découper segment (5s → 10s)
ffmpeg -i input.mov -ss 5 -to 10 -c copy output_trim.mov

# Ralenti 2x
ffmpeg -i input.mov \
  -filter:v "setpts=2.0*PTS" \
  -c:v prores_ks -profile:v 4444 -pix_fmt yuva444p10le \
  output_slow.mov

# Overlay sur fond noir
ffmpeg -i input.mov \
  -f lavfi -i color=black:s=1920x1080:r=15 \
  -filter_complex "[1:v][0:v]overlay=(W-w)/2:(H-h)/2" \
  -c:v prores_ks -profile:v 3 \
  output_centered.mov
```

---

## 🎯 Cas d'usage recommandés

### 1. Compositing avec fond personnalisé

```bash
# Overlay sur vidéo de fond
ffmpeg -i background.mp4 -i robot.mov \
  -filter_complex "[0:v][1:v]overlay=(W-w)/2:(H-h)/2" \
  -c:v prores_ks -profile:v 3 \
  final_composite.mov
```

### 2. Export pour réseaux sociaux

```bash
# Transparent → fond blanc (Instagram/Facebook)
ffmpeg -i input.mov \
  -f lavfi -i color=white:s=1080x1080 \
  -filter_complex "[1:v][0:v]overlay=(W-w)/2:(H-h)/2" \
  -c:v libx264 -crf 18 -pix_fmt yuv420p \
  instagram.mp4
```

### 3. Archivage longue durée

**Garder ProRes 4444** :
- Qualité maximale préservée
- Alpha channel intégré
- Compatible universellement

**Alternative lossless** (plus compact) :
```bash
# FFV1 dans MKV (lossless + alpha)
ffmpeg -i input.mov \
  -c:v ffv1 -level 3 -pix_fmt yuva444p \
  -c:a flac \
  archive.mkv
```

### 4. Animation avec transparence

**Import dans Blender** :
1. Video Sequence Editor → Add → Movie
2. Sélectionner `*_composite.mov`
3. Strip → Image Offset → Adjust si besoin
4. Output Properties → File Format → FFmpeg video
5. Encoding → Video Codec → ProRes 4444 (ou PNG sequence)

---

## 📞 Support

### Problème non résolu ?

1. **Exécuter `verify_mov.bat`** (Windows) ou commandes de diagnostic
2. **Vérifier** :
   - Version FFmpeg : `ffmpeg -version`
   - Support ProRes : `ffmpeg -codecs | grep prores`
   - Propriétés MOV : `ffprobe <fichier>.mov`
3. **Consulter** :
   - `docs/VERIFICATION_REPORT.md` - Tests de validation
   - `docs/MKV_FORMAT.md` - Détails techniques
   - `PAS_DIMAGE.txt` (Windows) - Guide dépannage complet

### Fichiers de référence

- `examples/sample_rbt/230.RBT` - Exemple 390×461 (89% transparent)
- `examples/sample_rbt/1014.RBT` - Exemple 551×277 (grand format)

**Tester avec exemple** :
```bash
mkdir -p RBT
cp examples/sample_rbt/230.RBT RBT/
./export_robot_mkv
# Vérifier output/230/230_composite.mov
```
