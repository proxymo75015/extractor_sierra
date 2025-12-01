# Exemples d'Utilisation - export_robot_mkv

Exemples pratiques pour l'extraction et la conversion de vidéos Robot (.RBT).

## Extraction Simple

### Export MKV basique (H.264)

```bash
./export_robot_mkv ScummVM/rbt/170.RBT output_170
```

**Résultat** :
```
output_170/
├── 170_video.mkv      # MKV 4 pistes + audio (1.4 MB)
├── 170_audio.wav      # Audio PCM 48 kHz (487 KB)
└── 170_metadata.txt   # Statistiques pixels
```

### Export avec codec spécifique

```bash
# H.265 (meilleure compression)
./export_robot_mkv ScummVM/rbt/212.RBT output_212 h265

# VP9 (open source)
./export_robot_mkv ScummVM/rbt/91.RBT output_91 vp9

# FFV1 (lossless, archivage)
./export_robot_mkv ScummVM/rbt/300.RBT output_300 ffv1
```

## Traitement en Batch

### Script bash pour tous les fichiers

```bash
#!/bin/bash
# batch_export.sh

for rbt in ScummVM/rbt/*.RBT; do
    filename=$(basename "$rbt" .RBT)
    echo "Traitement de $filename..."
    ./export_robot_mkv "$rbt" "output_batch/$filename" h264
done

echo "Traitement terminé : $(ls -1 output_batch/*.mkv | wc -l) fichiers"
```

**Usage** :
```bash
chmod +x batch_export.sh
mkdir -p output_batch
./batch_export.sh
```

### Export avec différents codecs

```bash
#!/bin/bash
# export_all_codecs.sh

INPUT="ScummVM/rbt/170.RBT"
BASE="output_codecs/170"

codecs=(h264 h265 vp9 ffv1)

for codec in "${codecs[@]}"; do
    echo "Export codec $codec..."
    ./export_robot_mkv "$INPUT" "${BASE}_${codec}" "$codec"
done

# Comparer les tailles
du -h output_codecs/*.mkv
```

## Post-Production

### Extraction piste BASE uniquement (FFmpeg)

```bash
# Extraire piste 0 (BASE RGB)
ffmpeg -i 170_video.mkv -map 0:0 -c copy 170_base_only.mkv

# Convertir en MP4
ffmpeg -i 170_base_only.mkv -c:v libx264 -crf 18 170_base.mp4
```

### Application du masque ALPHA

```bash
# Extraire BASE et ALPHA
ffmpeg -i 170_video.mkv -map 0:0 base.mkv
ffmpeg -i 170_video.mkv -map 0:2 alpha.mkv

# Appliquer alpha (transparence)
ffmpeg -i base.mkv -i alpha.mkv \
       -filter_complex "[0:v][1:v]alphamerge" \
       -c:v qtrle 170_transparent.mov
```

### Conversion ProRes (Apple)

```bash
# ProRes 4444 avec canal alpha
ffmpeg -i 170_video.mkv -map 0:0 -map 0:2 \
       -filter_complex "[0:v][1:v]alphamerge" \
       -c:v prores_ks -profile:v 4444 \
       -vendor ap10 -pix_fmt yuva444p10le \
       170_prores.mov
```

## Analyse

### Statistiques pixels détaillées

```bash
# Voir le fichier metadata.txt
cat output_170/170_metadata.txt

# Exemple de sortie :
# === Pixel Statistics ===
# Total pixels per frame: 76800 (320 x 240)
# 
# Frame 0:
#   BASE pixels: 13488 (17.56%)
#   REMAP pixels: 0 (0.00%)
#   SKIP pixels: 63312 (82.44%)
```

### Informations pistes MKV (mkvinfo)

```bash
# Installer mkvtoolnix si nécessaire
sudo apt-get install mkvtoolnix

# Afficher structure MKV
mkvinfo 170_video.mkv | grep -A 5 "Track number"

# Exemple de sortie :
# + Track number: 1 (track ID for mkvmerge & mkvextract: 0)
#  + Track type: video
#  + Name: BASE Layer (Fixed Colors)
#  + Codec ID: V_MPEG4/ISO/AVC
```

### Lecture pistes individuelles (VLC)

```bash
# Piste BASE (0)
vlc 170_video.mkv --video-track-id=0

# Piste ALPHA (2)
vlc 170_video.mkv --video-track-id=2

# Piste LUMINANCE (3)
vlc 170_video.mkv --video-track-id=3
```

## Debugging

### Vérifier contenu MKV (ffprobe)

```bash
ffprobe -v quiet -show_streams 170_video.mkv

# Compter les pistes
ffprobe -v quiet -show_streams 170_video.mkv | grep codec_type | wc -l
# Attendu : 5 (4 vidéo + 1 audio)
```

### Tester extraction audio seule

```bash
# Extraire audio WAV depuis MKV
ffmpeg -i 170_video.mkv -map 0:4 -c copy 170_audio_extracted.wav

# Vérifier propriétés
ffprobe 170_audio_extracted.wav
# Format : PCM 16-bit
# Fréquence : 48000 Hz
# Canaux : 1 (mono)
```

### Comparer qualité codecs

```bash
#!/bin/bash
# compare_quality.sh

INPUT="170_video.mkv"

# Extraire une frame de chaque codec
for i in {0..3}; do
    ffmpeg -i "$INPUT" -map 0:$i -vframes 1 \
           -pix_fmt rgb24 "frame_track${i}.png"
done

# Afficher tailles
ls -lh frame_track*.png
```

## Intégration After Effects

### Import multi-pistes

```javascript
// Script ExtendScript pour After Effects

var comp = app.project.importFile(new ImportOptions(File("170_video.mkv")));

// Créer composition
var newComp = app.project.items.addComp("Robot_170", 320, 240, 1, 11.3, 10);

// Ajouter pistes
var baseLayer = newComp.layers.add(comp.layer(1));  // Track 0 (BASE)
var alphaLayer = newComp.layers.add(comp.layer(3)); // Track 2 (ALPHA)

// Appliquer matte
baseLayer.trackMatteType = TrackMatteType.ALPHA;
baseLayer.trackMatteLayer = alphaLayer;
```

## Vérification Intégrité

### Checksum après export

```bash
#!/bin/bash
# verify_export.sh

RBT_FILE="ScummVM/rbt/170.RBT"
OUTPUT_DIR="output_170"

# Export
./export_robot_mkv "$RBT_FILE" "$OUTPUT_DIR" h264

# Vérifier fichiers générés
expected_files=("170_video.mkv" "170_audio.wav" "170_metadata.txt")

for file in "${expected_files[@]}"; do
    if [[ ! -f "$OUTPUT_DIR/$file" ]]; then
        echo "❌ Fichier manquant : $file"
        exit 1
    fi
done

echo "✅ Tous les fichiers présents"

# Vérifier pistes MKV
track_count=$(ffprobe -v quiet -show_streams "$OUTPUT_DIR/170_video.mkv" | grep codec_type | wc -l)

if [[ $track_count -eq 5 ]]; then
    echo "✅ 5 pistes détectées (4 vidéo + 1 audio)"
else
    echo "❌ Nombre de pistes incorrect : $track_count (attendu 5)"
    exit 1
fi

echo "✅ Export vérifié avec succès"
```

## Performance

### Comparaison temps d'encodage

```bash
#!/bin/bash
# benchmark_codecs.sh

INPUT="ScummVM/rbt/170.RBT"
OUTPUT_BASE="benchmark/170"

codecs=(h264 h265 vp9 ffv1)

for codec in "${codecs[@]}"; do
    echo "=== Codec: $codec ==="
    time ./export_robot_mkv "$INPUT" "${OUTPUT_BASE}_${codec}" "$codec"
    echo ""
done

# Résultats typiques (170.RBT, 113 frames) :
# h264 : ~8s  (rapide, universel)
# h265 : ~25s (lent, petite taille)
# vp9  : ~40s (très lent, excellente qualité)
# ffv1 : ~5s  (très rapide, gros fichier)
```

### Optimisation batch (parallèle)

```bash
#!/bin/bash
# parallel_export.sh

# Limiter à 4 processus simultanés
find ScummVM/rbt -name "*.RBT" | xargs -P 4 -I {} bash -c '
    filename=$(basename "{}" .RBT)
    ./export_robot_mkv "{}" "output_parallel/$filename" h264
'
```

## Ressources

### Commandes utiles

```bash
# Lister tous les RBT disponibles
ls -lh ScummVM/rbt/*.RBT

# Vérifier binaire compilé
file export_robot_mkv

# Aide intégrée
./export_robot_mkv --help

# Version FFmpeg
ffmpeg -version | head -1
```

### Liens

- [FFmpeg Documentation](https://ffmpeg.org/documentation.html)
- [Matroska Format](https://www.matroska.org/technical/specs/index.html)
- [VLC Playlist Guide](https://wiki.videolan.org/Documentation:Streaming_HowTo/)
- [After Effects Scripting](https://ae-scripting.docsforadobe.dev/)

---

**Note** : Ces exemples supposent que `export_robot_mkv` est compilé et exécutable dans le répertoire courant. Ajustez les chemins selon votre configuration.
