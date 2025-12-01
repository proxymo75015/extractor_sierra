# Format MKV Multi-couches Robot

Documentation technique du format d'export MKV à 4 pistes pour les vidéos Robot SCI.

## Vue d'ensemble

Le format MKV (Matroska) permet de stocker plusieurs pistes vidéo et audio dans un seul fichier conteneur. Notre implémentation décompose les pixels Robot en 4 couches visuelles distinctes pour une flexibilité maximale en post-production.

## Structure du fichier

```
fichier.mkv
├── Track 0 (Video) : BASE RGB       [320x240, codec H264/H265/VP9/FFV1]
├── Track 1 (Video) : REMAP RGB      [320x240, codec H264/H265/VP9/FFV1]
├── Track 2 (Video) : ALPHA Grayscale [320x240, codec H264/H265/VP9/FFV1]
├── Track 3 (Video) : LUMINANCE Y     [320x240, codec H264/H265/VP9/FFV1]
└── Track 4 (Audio) : PCM 16-bit      [48000 Hz, mono]
```

## Pistes Vidéo

### Track 0 : BASE RGB (Couleurs fixes)

**Description** : Pixels opaques avec indices de palette 0-235.

**Contenu** :
- Pixels RGB (3 canaux)
- Opacité complète (alpha = 255)
- Représente ~15-20% de l'image

**Usage typique** :
- Décors fixes
- Arrière-plans
- Éléments statiques

**Exemple de pixel** :
```
Index palette : 42
RGB : (123, 78, 200)
Output : RGB(123, 78, 200) opaque
```

### Track 1 : REMAP RGB (Recoloration)

**Description** : Zones recoloriables avec indices 236-254.

**Contenu** :
- Pixels RGB (3 canaux)
- Couleurs modifiables dynamiquement
- Représente ~0-5% de l'image (souvent vide)

**Usage typique** :
- Personnages changeant de couleur
- Effets de lumière variables
- Indicateurs d'état

**Note** : Dans la plupart des vidéos testées, cette piste est **vide** (noir complet) car la fonctionnalité REMAP n'est pas utilisée.

### Track 2 : ALPHA Grayscale (Transparence)

**Description** : Masque de transparence binaire.

**Contenu** :
- Canal unique (grayscale)
- 255 = transparent (index 255)
- 0 = opaque (indices 0-254)
- Représente ~80-85% de l'image

**Usage typique** :
- Découpage du sujet
- Suppression du fond
- Compositing dans After Effects / Premiere

**Visualisation** :
```
Blanc (255) = Zones à masquer
Noir (0)    = Zones visibles
```

### Track 3 : LUMINANCE Y (Aperçu grayscale)

**Description** : Version en niveaux de gris de l'image composite.

**Contenu** :
- Canal unique (grayscale)
- Conversion ITU-R BT.601 : `Y = 0.299R + 0.587G + 0.114B`
- Représente la luminosité perçue

**Usage typique** :
- Prévisualisation rapide
- Effets noir et blanc
- Masques de luminosité

**Formule** :
```cpp
uint8_t Y = 0.299 * R + 0.587 * G + 0.114 * B;
```

## Piste Audio

### Track 4 : PCM 16-bit mono

**Description** : Audio décompressé depuis DPCM16.

**Caractéristiques** :
- Format : PCM signed 16-bit little-endian
- Fréquence : 48000 Hz (resamplé depuis 22050 Hz)
- Canaux : Mono (2 canaux EVEN/ODD mixés)
- Codec : PCM non compressé

**Pipeline** :
```
DPCM16 22050Hz → PCM 22050Hz → Resample 48kHz → MKV track
```

## Codecs Supportés

### H.264 (libx264) - **Recommandé**

**Paramètres** :
```bash
-c:v libx264 -preset medium -crf 18
```

**Avantages** :
- Compatible partout (navigateurs, mobiles, NLE)
- Bon ratio qualité/taille
- Hardware decoding

**Inconvénients** :
- Compression avec pertes
- Artefacts sur aplats

**Usage** : Distribution, web, lecture générale

### H.265 (libx265)

**Paramètres** :
```bash
-c:v libx265 -preset medium -crf 22
```

**Avantages** :
- 30-50% plus compact que H.264
- Meilleure qualité à bitrate égal
- Standard moderne

**Inconvénients** :
- Encodage lent
- Support limité (navigateurs)

**Usage** : Archivage, stockage optimisé

### VP9 (libvpx-vp9)

**Paramètres** :
```bash
-c:v libvpx-vp9 -crf 30 -b:v 0
```

**Avantages** :
- Open source (pas de royalties)
- Excellente qualité
- Supporte transparence native

**Inconvénients** :
- Encodage très lent
- Bitrate variable

**Usage** : Web (YouTube), archivage open source

### FFV1 (lossless)

**Paramètres** :
```bash
-c:v ffv1 -level 3
```

**Avantages** :
- **100% sans perte**
- Compression rapide
- Archivage certifié (LoC)

**Inconvénients** :
- Fichiers volumineux (5-10x H.264)
- Support limité (VLC, FFmpeg)

**Usage** : Archivage professionnel, master de conservation

## Métadonnées

Chaque piste contient des métadonnées pour identification :

```
Track 0:
  title: "BASE Layer (Fixed Colors)"
  
Track 1:
  title: "REMAP Layer (Recolorable)"
  
Track 2:
  title: "ALPHA Mask (Transparency)"
  
Track 3:
  title: "LUMINANCE Preview (Grayscale)"
  
Track 4:
  title: "Audio PCM 48kHz Mono"
```

## Utilisation en Post-Production

### Adobe After Effects

```javascript
// Importer video.mkv
// After Effects sépare automatiquement les pistes

comp = new CompItem("Robot Composite");
comp.layers.add(base_track);    // Track 0
comp.layers.add(remap_track);   // Track 1 (si utilisé)

// Appliquer alpha matte
alpha_layer = comp.layers.add(alpha_track);  // Track 2
alpha_layer.trackMatteType = TrackMatteType.ALPHA;
```

### DaVinci Resolve

1. Importer `video.mkv`
2. Aller dans "Edit" → "Select Tracks"
3. Choisir les pistes à utiliser (0, 2, 3...)
4. Utiliser Track 2 comme matte pour découpage

### FFmpeg (recomposition)

```bash
# Extraire les pistes
ffmpeg -i video.mkv -map 0:0 base.mkv
ffmpeg -i video.mkv -map 0:2 alpha.mkv

# Appliquer alpha
ffmpeg -i base.mkv -i alpha.mkv \
       -filter_complex "[0:v][1:v]alphamerge" \
       -c:v prores_ks output.mov
```

## Statistiques Typiques

Basé sur les fichiers de test (320×240, 10 fps) :

| Fichier | Frames | Durée | BASE % | REMAP % | SKIP % | Taille MKV |
|---------|--------|-------|--------|---------|--------|------------|
| 91.RBT  | 90     | 9.0s  | 16.2%  | 0.0%    | 83.8%  | 1.2 MB     |
| 170.RBT | 113    | 11.3s | 17.6%  | 0.0%    | 82.4%  | 1.4 MB     |
| 212.RBT | 33     | 3.3s  | 15.8%  | 0.0%    | 84.2%  | 439 KB     |

**Bitrate moyen** : ~950 kb/s (H.264 CRF 18)

## Compatibilité

### Lecture

- ✅ **VLC Media Player** (toutes plateformes)
- ✅ **FFmpeg** / **FFplay**
- ✅ **MPC-HC** (Windows)
- ✅ **IINA** (macOS)
- ⚠️ **Windows Media Player** (piste unique visible)
- ⚠️ **QuickTime** (piste unique visible)

### Édition

- ✅ **DaVinci Resolve** (multi-track support)
- ✅ **Adobe Premiere Pro** (import partiel)
- ✅ **Adobe After Effects** (import pistes séparées)
- ⚠️ **Final Cut Pro** (conversion recommandée)

## Références

- [Matroska Specifications](https://www.matroska.org/technical/specs/index.html)
- [ITU-R BT.601](https://www.itu.int/rec/R-REC-BT.601/)
- [FFmpeg Encoding Guide](https://trac.ffmpeg.org/wiki/Encode/H.264)
- [FFV1 Specifications](https://github.com/FFmpeg/FFV1)
