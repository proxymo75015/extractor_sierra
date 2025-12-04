# Export ProRes 4444 avec Transparence

## 🎬 Vue d'ensemble

Depuis la version **2.4.0**, l'extracteur exporte les vidéos Robot en format **ProRes 4444 MOV** au lieu de H.264 MP4. Ce format professionnel préserve la **transparence native** des fichiers RBT.

## ✨ Caractéristiques Techniques

### Format de Sortie

| Paramètre | Valeur |
|-----------|--------|
| **Container** | QuickTime MOV |
| **Codec vidéo** | ProRes 4444 (`prores_ks`) |
| **Profile** | 4444 (avec alpha) |
| **Pixel format** | `yuva444p10le` (YUV 4:4:4 + Alpha 10-bit) |
| **Codec audio** | PCM S16LE (lossless) |
| **Échantillonnage** | 22050 Hz → Conservé natif |
| **Canaux** | Mono |

### Commande FFmpeg

```bash
ffmpeg -framerate 15 \
  -i frames/frame_%04d.png \
  -i audio.wav \
  -c:v prores_ks -profile:v 4444 -pix_fmt yuva444p10le \
  -c:a pcm_s16le \
  output.mov
```

## 🔍 Gestion de la Transparence

### Mapping Pixels RBT → Alpha

Le format Robot utilise un système de pixels indexés avec 3 types :

| Type | Indices | Traitement Alpha |
|------|---------|------------------|
| **Base** | 0-235 | Alpha = **255** (opaque) |
| **Remap** | 236-254 | Alpha = **255** (opaque) |
| **Skip** | 255 | Alpha = **0** (transparent) |

### Code de Conversion

```cpp
// Extraction RGBA depuis RBT
for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
        uint8_t paletteIndex = pixelIndices[y * width + x];
        
        if (paletteIndex == 255) {
            // Pixel transparent (skip)
            rgbaImage[idx + 0] = 0;    // R
            rgbaImage[idx + 1] = 0;    // G
            rgbaImage[idx + 2] = 0;    // B
            rgbaImage[idx + 3] = 0;    // Alpha = transparent
        } else {
            // Pixel opaque depuis palette
            rgbaImage[idx + 0] = palette[paletteIndex * 3 + 0];  // R
            rgbaImage[idx + 1] = palette[paletteIndex * 3 + 1];  // G
            rgbaImage[idx + 2] = palette[paletteIndex * 3 + 2];  // B
            rgbaImage[idx + 3] = 255;  // Alpha = opaque
        }
    }
}
```

## 📊 Comparaison MP4 vs MOV

| Critère | H.264 MP4 (ancien) | ProRes 4444 MOV (nouveau) |
|---------|-------------------|---------------------------|
| **Transparence** | ❌ Non supportée | ✅ Canal alpha natif |
| **Qualité** | Avec pertes (CRF 18) | Quasi-lossless |
| **Taille fichier** | ~5-10 MB | ~50-100 MB |
| **Compatibilité** | Lecture universelle | Post-production pro |
| **Édition** | Difficile (GOP) | ✅ I-frame only |
| **Compositing** | Impossible sans masque | ✅ Direct avec alpha |
| **Archivage** | ⚠️ Compression destructive | ✅ Préservation qualité |

## 🎯 Cas d'Usage

### 1. Compositing dans After Effects

```javascript
// Le canal alpha est automatiquement détecté
// Utiliser Interpret Footage > Alpha: Straight (Unmatted)

// Exemple de composition :
footage = app.project.importFile("91_composite.mov");
comp = app.project.items.addComp("Composite", 640, 480, 1, 10, 15);
layer = comp.layers.add(footage);
// La transparence fonctionne immédiatement !
```

### 2. Import dans DaVinci Resolve

1. Glisser-déposer le fichier `.mov` dans la Media Pool
2. Le canal alpha est automatiquement reconnu
3. Utiliser Fusion pour compositing avec d'autres éléments
4. Ou appliquer color grading sans perte sur format quasi-lossless

### 3. Overlay dans Premiere Pro

```
Timeline → Import MOV → Placer au-dessus d'un autre clip
Le masque alpha fonctionne automatiquement (mode "Normal")
```

## 🔧 Vérification du Fichier

### Avec FFmpeg

```bash
# Afficher les informations du fichier
ffmpeg -i 91_composite.mov

# Vérifier présence du canal alpha
ffprobe -v error -select_streams v:0 \
  -show_entries stream=pix_fmt \
  -of default=noprint_wrappers=1:nokey=1 \
  91_composite.mov
# Output attendu : yuva444p10le
```

### Avec FFplay

```bash
# Visualiser avec fond en damier (transparence visible)
ffplay -vf "format=rgba,split[a][b];[b]lutrgb=a=val*0.5[b];[a][b]alphamerge" 91_composite.mov
```

## 📦 Taille des Fichiers

### Estimation par Seconde

| Résolution | Durée | Taille ProRes 4444 |
|------------|-------|-------------------|
| 320×240 | 10s @ 15fps | ~15 MB |
| 514×382 | 10s @ 15fps | ~40 MB |
| 640×480 | 10s @ 15fps | ~60 MB |

**Facteur de compression vs PNG** : ~5-10× plus compact que séquence PNG

## ⚙️ Configuration FFmpeg

### Vérifier Support ProRes

```bash
ffmpeg -codecs | grep prores
```

**Output attendu :**
```
DEV.L. prores               Apple ProRes (iCodec Pro)
```

### Installation si Manquant

**Windows** :
```powershell
# Télécharger build "FULL" (pas "essentials")
# https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-full.zip
```

**Linux** :
```bash
sudo apt install ffmpeg
# OU compiler depuis source avec --enable-encoder=prores_ks
```

**macOS** :
```bash
brew install ffmpeg
```

## 🐛 Dépannage

### Erreur "Unknown encoder 'prores_ks'"

**Cause** : FFmpeg compilé sans support ProRes

**Solution** :
1. Windows : Installer build "full" au lieu de "essentials"
2. Linux : `sudo apt install ffmpeg` (version complète)
3. Vérifier : `ffmpeg -encoders | grep prores`

### Fichier MOV Trop Volumineux

**Normal** : ProRes 4444 est quasi-lossless, donc ~10× plus lourd que H.264

**Alternatives** :
- Utiliser le fichier MKV multi-pistes (Track 0 = composite RGB)
- Réencoder en H.264 : `ffmpeg -i input.mov -c:v libx264 -crf 18 output.mp4`
- Pour archivage : Garder ProRes (meilleure qualité)

### Canal Alpha Non Reconnu

**Vérifier** : `ffprobe -show_streams input.mov`

**Si `pix_fmt=yuv444p10le`** (sans alpha) :
- FFmpeg n'a pas détecté les PNG RGBA en entrée
- Vérifier que frames PNG sont bien RGBA (4 canaux)
- Forcer format : `-pix_fmt yuva444p10le`

## 📚 Ressources

- [Documentation ProRes Apple](https://support.apple.com/en-us/HT202410)
- [FFmpeg ProRes Encoder](https://trac.ffmpeg.org/wiki/Encode/VFX#ProRes)
- [Spec ProRes 4444](https://www.apple.com/final-cut-pro/docs/Apple_ProRes_White_Paper.pdf)

## 🔄 Historique

| Version | Format Composite | Notes |
|---------|-----------------|-------|
| < 2.4.0 | H.264 MP4 RGB | Pas de transparence |
| ≥ 2.4.0 | ProRes 4444 MOV RGBA | ✅ Canal alpha natif |
