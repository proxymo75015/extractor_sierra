#!/bin/bash
# Script pour extraire les positions Robot de Phantasmagoria
# via ScummVM patché

set -e

SCUMMVM_DIR="${SCUMMVM_DIR:-$HOME/scummvm}"
PHANTOM_DIR="${PHANTOM_DIR:-./phantasmagoria_game}"
OUTPUT_FILE="robot_positions.txt"

echo "========================================="
echo "Robot Position Extractor for Phantasmagoria"
echo "========================================="
echo ""

# Vérifier si ScummVM est disponible
if [ ! -d "$SCUMMVM_DIR" ]; then
    echo "ERREUR: ScummVM non trouvé dans $SCUMMVM_DIR"
    echo ""
    echo "Options:"
    echo "1. Cloner ScummVM:"
    echo "   git clone https://github.com/scummvm/scummvm.git ~/scummvm"
    echo ""
    echo "2. Spécifier un autre chemin:"
    echo "   export SCUMMVM_DIR=/chemin/vers/scummvm"
    echo ""
    exit 1
fi

echo "[1/4] Application du patch de débogage..."
cd "$SCUMMVM_DIR"

# Vérifier si déjà patché
if git diff engines/sci/engine/kvideo.cpp | grep -q "ROBOT_DEBUG"; then
    echo "  ✓ Patch déjà appliqué"
else
    # Appliquer le patch
    if [ -f "/workspaces/extractor_sierra/scummvm_robot_patch.diff" ]; then
        patch -p1 < /workspaces/extractor_sierra/scummvm_robot_patch.diff
        echo "  ✓ Patch appliqué"
    else
        echo "  ⚠ Patch non trouvé, modification manuelle..."
        # Modification directe
        sed -i '/const int16 scale = argc > 5/a\	// LOG ROBOT COORDINATES FOR EXTRACTION\n\twarning("ROBOT_DEBUG: Robot %d at position X=%d Y=%d (priority=%d scale=%d)", robotId, x, y, priority, scale);' \
            engines/sci/engine/kvideo.cpp
        echo "  ✓ Modification appliquée"
    fi
fi

echo ""
echo "[2/4] Compilation de ScummVM (mode debug)..."
if [ ! -f "scummvm" ] || [ engines/sci/engine/kvideo.cpp -nt scummvm ]; then
    ./configure --enable-debug --disable-all-engines --enable-engine=sci
    make -j$(nproc)
    echo "  ✓ Compilation terminée"
else
    echo "  ✓ Binaire à jour"
fi

echo ""
echo "[3/4] Préparation du jeu..."
cd /workspaces/extractor_sierra

# Créer un fichier de configuration ScummVM
cat > scummvm_temp.ini <<EOF
[scummvm]
gfx_mode=opengl
fullscreen=false
aspect_ratio=true
gui_language=fr_FR

[phantasmagoria]
gameid=phantasmagoria
description=Phantasmagoria (CD1+CD2)
language=en
platform=pc
path=$PHANTOM_DIR
EOF

echo "  ✓ Configuration créée"

echo ""
echo "[4/4] Extraction des positions Robot..."
echo ""
echo "INSTRUCTIONS:"
echo "============="
echo "1. ScummVM va démarrer Phantasmagoria"
echo "2. Les logs ROBOT_DEBUG apparaîtront dans la console"
echo "3. Pour chaque vidéo Robot qui se joue, noter les coordonnées"
echo "4. Appuyez sur Ctrl+C quand vous avez fini"
echo ""
echo "ASTUCE: Pour tester rapidement:"
echo "  - Démarrez une nouvelle partie"
echo "  - Les premières vidéos Robot apparaîtront automatiquement"
echo ""
read -p "Appuyez sur Entrée pour lancer ScummVM..."

# Lancer ScummVM et capturer les logs
LOG_FILE="scummvm_robot_logs.txt"
echo "" > "$LOG_FILE"

"$SCUMMVM_DIR/scummvm" -c scummvm_temp.ini --debugflags=all --debuglevel=1 phantasmagoria 2>&1 | tee -a "$LOG_FILE"

echo ""
echo "========================================="
echo "Extraction terminée!"
echo "========================================="
echo ""
echo "Les logs complets sont dans: $LOG_FILE"
echo ""
echo "Extraction des positions Robot..."

# Parser les logs pour extraire les positions
grep "ROBOT_DEBUG" "$LOG_FILE" | while read line; do
    if [[ $line =~ Robot\ ([0-9]+)\ at\ position\ X=(-?[0-9]+)\ Y=(-?[0-9]+) ]]; then
        ROBOT_ID="${BASH_REMATCH[1]}"
        X="${BASH_REMATCH[2]}"
        Y="${BASH_REMATCH[3]}"
        echo "$ROBOT_ID $X $Y"
    fi
done | sort -u -n > "$OUTPUT_FILE"

if [ -s "$OUTPUT_FILE" ]; then
    echo "✓ Positions extraites dans: $OUTPUT_FILE"
    echo ""
    echo "Contenu:"
    cat "$OUTPUT_FILE"
else
    echo "⚠ Aucune position Robot trouvée"
    echo ""
    echo "Possible raisons:"
    echo "  - Aucune vidéo Robot n'a été jouée"
    echo "  - Le patch n'a pas été appliqué correctement"
    echo "  - ScummVM a été compilé sans les warnings"
fi

echo ""
echo "NETTOYAGE:"
echo "  Pour retirer le patch: cd $SCUMMVM_DIR && git checkout engines/sci/engine/kvideo.cpp"
