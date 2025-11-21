#!/bin/bash
# Script shell d'exemple pour conversion batch de fichiers Robot vers OpenEXR
#
# Usage: ./batch_convert_rbt_to_exr.sh input_directory/ output_directory/ [pc|mac]

set -e

INPUT_DIR="${1:?Usage: $0 input_dir output_dir [platform]}"
OUTPUT_DIR="${2:?Usage: $0 input_dir output_dir [platform]}"
PLATFORM="${3:-pc}"

# Vérifier que robot_decoder existe
if ! command -v robot_decoder &> /dev/null; then
    echo "Erreur: robot_decoder non trouvé dans PATH" >&2
    echo "Compilez le projet d'abord avec: cmake --build build/" >&2
    exit 1
fi

# Vérifier que le répertoire d'entrée existe
if [ ! -d "$INPUT_DIR" ]; then
    echo "Erreur: Répertoire introuvable: $INPUT_DIR" >&2
    exit 1
fi

# Créer le répertoire de sortie
mkdir -p "$OUTPUT_DIR"

# Compter les fichiers .RBT
RBT_COUNT=$(find "$INPUT_DIR" -iname "*.rbt" | wc -l)

if [ "$RBT_COUNT" -eq 0 ]; then
    echo "Aucun fichier .RBT trouvé dans $INPUT_DIR" >&2
    exit 1
fi

echo "================================================"
echo "Conversion batch Robot → OpenEXR"
echo "================================================"
echo "Répertoire source: $INPUT_DIR"
echo "Répertoire sortie: $OUTPUT_DIR"
echo "Plateforme: ${PLATFORM^^}"
echo "Fichiers .RBT trouvés: $RBT_COUNT"
echo "================================================"
echo

CURRENT=0
SUCCESS=0
FAILED=0

# Traiter chaque fichier .RBT
while IFS= read -r -d '' rbt_file; do
    CURRENT=$((CURRENT + 1))
    
    # Extraire le nom de base sans extension
    BASENAME=$(basename "$rbt_file" .rbt)
    BASENAME=$(basename "$BASENAME" .RBT)
    
    # Créer un sous-répertoire pour cette vidéo
    VIDEO_OUTPUT_DIR="$OUTPUT_DIR/$BASENAME"
    mkdir -p "$VIDEO_OUTPUT_DIR"
    
    echo "[$CURRENT/$RBT_COUNT] Conversion de: $BASENAME"
    echo "  Source: $rbt_file"
    echo "  Sortie: $VIDEO_OUTPUT_DIR/"
    
    # Exécuter la conversion
    # Note: Adapter selon l'interface réelle de robot_decoder
    if robot_decoder \
        "$rbt_file" \
        --export-exr \
        --output-dir "$VIDEO_OUTPUT_DIR" \
        --platform "$PLATFORM" \
        --compression zip \
        2>&1 | sed 's/^/    /'; then
        
        echo "  ✓ Succès"
        SUCCESS=$((SUCCESS + 1))
    else
        echo "  ✗ Échec" >&2
        FAILED=$((FAILED + 1))
    fi
    
    echo
    
done < <(find "$INPUT_DIR" -iname "*.rbt" -print0)

echo "================================================"
echo "Conversion terminée"
echo "================================================"
echo "Total: $RBT_COUNT fichiers"
echo "Succès: $SUCCESS"
echo "Échecs: $FAILED"
echo "================================================"

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
