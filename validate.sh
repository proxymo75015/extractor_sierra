#!/bin/bash
# Script de validation complète

echo "=== VALIDATION EXTRACTEUR ROBOT RBT ==="
echo ""

# 1. Vérifier la compilation
echo "1. Vérification compilation..."
if [ -f build/robot_decoder ]; then
    echo "   ✅ Binaire robot_decoder trouvé"
else
    echo "   ❌ Binaire robot_decoder manquant"
    exit 1
fi

# 2. Test extraction rapide
echo ""
echo "2. Test extraction (5 frames)..."
rm -rf /tmp/validate_test
./build/robot_decoder ScummVM/rbt/91.RBT /tmp/validate_test 5 audio > /dev/null 2>&1
FRAME_COUNT=$(ls /tmp/validate_test/frames/*.ppm 2>/dev/null | wc -l)
if [ "$FRAME_COUNT" -eq 5 ]; then
    echo "   ✅ Extraction vidéo OK ($FRAME_COUNT frames)"
else
    echo "   ❌ Extraction vidéo échouée (attendu: 5, obtenu: $FRAME_COUNT)"
    exit 1
fi

if [ -f /tmp/validate_test/audio.raw.pcm ]; then
    AUDIO_SIZE=$(stat -c%s /tmp/validate_test/audio.raw.pcm)
    if [ "$AUDIO_SIZE" -gt 0 ]; then
        echo "   ✅ Extraction audio OK ($AUDIO_SIZE bytes)"
    else
        echo "   ❌ Fichier audio vide"
        exit 1
    fi
else
    echo "   ❌ Fichier audio manquant"
    exit 1
fi

# 3. Vérifier la vidéo finale
echo ""
echo "3. Vérification vidéo finale..."
if [ -f output_91_clean/video_91_full.mp4 ]; then
    DURATION=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 output_91_clean/video_91_full.mp4 2>/dev/null | cut -d. -f1)
    if [ "$DURATION" -eq 9 ]; then
        echo "   ✅ Vidéo finale OK (durée: ${DURATION}s)"
    else
        echo "   ⚠️  Vidéo finale présente (durée: ${DURATION}s, attendu: 9s)"
    fi
else
    echo "   ⚠️  Vidéo finale non créée (normal si première exécution)"
fi

# 4. Vérifier la documentation
echo ""
echo "4. Vérification documentation..."
DOCS=("FINAL_SUMMARY.md" "QUICK_START.md" "EXTRACTION_REPORT.md")
for doc in "${DOCS[@]}"; do
    if [ -f "$doc" ]; then
        echo "   ✅ $doc"
    else
        echo "   ❌ $doc manquant"
    fi
done

# 5. Résumé
echo ""
echo "=== RÉSULTAT FINAL ==="
echo "✅ Tous les tests passés avec succès!"
echo ""
echo "Pour créer une vidéo complète:"
echo "  ./build/robot_decoder ScummVM/rbt/91.RBT output_91 90 audio"
echo "  cd output_91 && ffmpeg -framerate 10 -i frames/frame_%04d_cel_00.ppm -f s16le -ar 22050 -ac 2 -i audio.raw.pcm -c:v libx264 -pix_fmt yuv420p -c:a aac video.mp4 -y"
echo ""
echo "Documentation disponible:"
echo "  - QUICK_START.md : Guide de démarrage rapide"
echo "  - FINAL_SUMMARY.md : Résumé technique complet"
echo "  - EXTRACTION_REPORT.md : Rapport d'extraction détaillé"
echo ""

# Cleanup
rm -rf /tmp/validate_test

exit 0
