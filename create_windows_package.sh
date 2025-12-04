#!/bin/bash
# Création d'un package Windows prêt à distribuer

set -e

PACKAGE_NAME="extractor_sierra_windows"
PACKAGE_DIR="$PACKAGE_NAME"

echo "=== Création du package Windows ==="

# Nettoyer et créer le répertoire
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR"

# Copier l'exécutable
if [ ! -f "export_robot_mkv_windows.exe" ]; then
    echo "Erreur : export_robot_mkv_windows.exe introuvable"
    echo "Exécutez d'abord : ./build_windows.sh"
    exit 1
fi

cp export_robot_mkv_windows.exe "$PACKAGE_DIR/export_robot_mkv.exe"

# Créer le répertoire RBT
mkdir -p "$PACKAGE_DIR/RBT"

# Copier les DLL MinGW nécessaires
echo "Copie des DLL MinGW..."

# Chemins pour les DLLs MinGW
MINGW_POSIX="/usr/lib/gcc/x86_64-w64-mingw32/13-posix"
MINGW_PTHREAD="/usr/x86_64-w64-mingw32/lib"

# Copier les DLLs nécessaires
echo "  - libstdc++-6.dll"
cp "$MINGW_POSIX/libstdc++-6.dll" "$PACKAGE_DIR/"

echo "  - libgcc_s_seh-1.dll"
cp "$MINGW_POSIX/libgcc_s_seh-1.dll" "$PACKAGE_DIR/"

echo "  - libwinpthread-1.dll"
cp "$MINGW_PTHREAD/libwinpthread-1.dll" "$PACKAGE_DIR/"

# Créer README Windows
cat > "$PACKAGE_DIR/README_WINDOWS.txt" << 'EOF'
================================================================================
  SIERRA ROBOT VIDEO EXTRACTOR - Windows
================================================================================

IMPORTANT: INSTALLATION DE FFMPEG (OBLIGATOIRE)
------------------------------------------------

Ce programme NECESSITE FFmpeg pour fonctionner. Sans FFmpeg, seuls les
fichiers WAV seront generes (pas de MKV ni MP4).

INSTALLATION FFMPEG SUR WINDOWS:

1. Telecharger FFmpeg:
   https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip

2. Extraire le fichier ZIP dans C:\ffmpeg\

3. Ajouter au PATH Windows:
   - Clic droit sur "Ce PC" > Proprietes
   - Parametres systeme avances > Variables d'environnement
   - Dans "Variables systeme", selectionner "Path" > Modifier
   - Cliquer "Nouveau" et ajouter: C:\ffmpeg\bin
   - Cliquer OK sur toutes les fenetres

4. Verifier l'installation:
   - Ouvrir un NOUVEAU terminal (cmd)
   - Taper: ffmpeg -version
   - Si vous voyez la version de FFmpeg, c'est OK!

UTILISATION
-----------

1. Placer vos fichiers .RBT dans le dossier RBT/

2. Double-cliquer sur "run.bat" OU ouvrir un terminal et executer :
   export_robot_mkv.exe [codec]

   Codecs disponibles :
   - h264  (defaut, recommande)
   - h265  (meilleure compression)
   - vp9   (open source)
   - ffv1  (lossless)

3. Les resultats seront dans le dossier output/

STRUCTURE DE SORTIE
-------------------

output/
├── 91/
│   ├── 91_video.mkv        # MKV 4 pistes + audio
│   ├── 91_audio.wav        # Audio PCM 22 kHz
│   ├── 91_composite.mp4    # Video composite H.264
│   └── 91_metadata.txt     # Metadonnees
└── ...

FORMAT MKV
----------

Le fichier MKV contient 4 pistes video :
- Track 0 : BASE (pixels RGB fixes 0-235)
- Track 1 : REMAP (pixels recoloriables 236-254)
- Track 2 : ALPHA (masque transparence 255)
- Track 3 : LUMINANCE (niveaux de gris)
- Audio : PCM 48 kHz mono

DEPANNAGE
---------

PROBLEME: "FFmpeg is not installed or not in PATH"
SOLUTION: Verifier que FFmpeg est installe et dans le PATH (voir ci-dessus)

PROBLEME: Seuls les fichiers .wav sont generes
SOLUTION: FFmpeg n'est pas correctement installe. Refaire l'installation.

PROBLEME: "No .RBT files found"
SOLUTION: Placer vos fichiers .RBT dans le dossier RBT/

SUPPORT
-------

GitHub : https://github.com/proxymo75015/extractor_sierra
Documentation : docs/

================================================================================
EOF

# Créer script batch Windows
cat > "$PACKAGE_DIR/run.bat" << 'EOF'
@echo off
chcp 65001 >nul
echo ========================================
echo Sierra Robot Video Extractor
echo ========================================
echo.

REM Verifier si FFmpeg est installe
ffmpeg -version >nul 2>&1
if %errorlevel% neq 0 (
    echo ERREUR: FFmpeg n'est pas installe ou pas dans le PATH
    echo.
    echo Telechargez FFmpeg depuis: https://ffmpeg.org/download.html
    echo.
    pause
    exit /b 1
)

REM Creer le dossier RBT si necessaire
if not exist "RBT\" mkdir RBT

REM Verifier s'il y a des fichiers RBT
dir /b RBT\*.RBT >nul 2>&1
if %errorlevel% neq 0 (
    echo ATTENTION: Aucun fichier .RBT trouve dans RBT\
    echo.
    echo Placez vos fichiers .RBT dans le dossier RBT\
    echo.
    pause
    exit /b 1
)

echo Traitement des fichiers RBT...
echo.

REM Lancer l'extraction (codec h264 par defaut)
export_robot_mkv.exe h264

echo.
echo ========================================
echo Traitement termine
echo ========================================
echo.
echo Les resultats sont dans le dossier output\
echo.
pause
EOF

# Créer script de diagnostic test_mov.bat
cat > "$PACKAGE_DIR/test_mov.bat" << 'EOF'
@echo off
REM Script de diagnostic pour tester le MOV ProRes genere
echo ===== Diagnostic MOV ProRes =====
echo.

REM Verifier presence FFmpeg
where ffmpeg >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERREUR: FFmpeg n'est pas dans le PATH
    echo Installez FFmpeg FULL depuis https://www.gyan.dev/ffmpeg/builds/
    pause
    exit /b 1
)

echo FFmpeg trouve!
echo.

REM Trouver le premier fichier MOV dans output\
for /r output\ %%f in (*.mov) do (
    set MOV_FILE=%%f
    goto :found
)

echo ERREUR: Aucun fichier MOV trouve dans output\
echo Executez d'abord run.bat pour generer les fichiers
pause
exit /b 1

:found
echo Fichier MOV trouve: %MOV_FILE%
echo.
echo ===== Proprietes video =====
ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,pix_fmt,width,height,duration,nb_frames -of default=noprint_wrappers=1 "%MOV_FILE%"
echo.
echo ===== Proprietes audio =====
ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,sample_rate,channels -of default=noprint_wrappers=1 "%MOV_FILE%"
echo.
echo ===== Extraction frame de test =====
set TEST_FRAME=test_frame_from_mov.png
ffmpeg -i "%MOV_FILE%" -vf "select=eq(n\,10)" -frames:v 1 "%TEST_FRAME%" -y 2>nul
if exist "%TEST_FRAME%" (
    echo Frame extraite avec succes: %TEST_FRAME%
    echo.
    echo OUVREZ test_frame_from_mov.png dans Paint ou un viewer d'images
    echo pour VERIFIER que l'image est visible!
    echo.
    start "" "%TEST_FRAME%"
) else (
    echo ERREUR: Impossible d'extraire une frame
)
echo.
echo ===== Diagnostic termine =====
echo.
echo RESULTAT:
echo   - Si la frame PNG affiche une image: MOV est valide!
echo   - Si la frame PNG est noire: Probleme de generation
echo.
echo LECTEURS VIDEO COMPATIBLES PRORES 4444:
echo   [OK] QuickTime Player (si installe sur Windows)
echo   [OK] Adobe After Effects, Premiere Pro
echo   [OK] DaVinci Resolve
echo   [LIMITE] VLC (support ProRes 4444 alpha incomplet)
echo   [NON] Windows Media Player (ne supporte PAS ProRes)
echo.
pause
EOF

# Créer fichier de configuration exemple
cat > "$PACKAGE_DIR/config_example.txt" << 'EOF'
# Configuration d'exemple pour export_robot_mkv

# Codecs disponibles :
# - h264  : Codec universel (recommandé)
# - h265  : Meilleure compression
# - vp9   : Open source, excellente qualité
# - ffv1  : Lossless (archivage)

# Pour utiliser un codec spécifique :
# export_robot_mkv.exe h265

# Fichiers de sortie générés pour chaque RBT :
# - <nom>_video.mkv      : MKV 4 pistes + audio
# - <nom>_audio.wav      : Audio PCM 22050 Hz
# - <nom>_composite.mp4  : Vidéo H.264 composite
# - <nom>_metadata.txt   : Métadonnées complètes
EOF

# Copier la documentation
mkdir -p "$PACKAGE_DIR/docs"
cp README.md "$PACKAGE_DIR/docs/" 2>/dev/null || true
cp LICENSE "$PACKAGE_DIR/" 2>/dev/null || true
cp docs/MKV_FORMAT.md "$PACKAGE_DIR/docs/" 2>/dev/null || true

# Créer l'archive ZIP
echo ""
echo "Création de l'archive ZIP..."
zip -r "${PACKAGE_NAME}.zip" "$PACKAGE_DIR"

# Afficher le résumé
echo ""
echo "✓ Package Windows créé avec succès !"
echo ""
echo "Contenu du package :"
ls -lh "$PACKAGE_DIR/"
echo ""
echo "Archive : ${PACKAGE_NAME}.zip ($(du -sh ${PACKAGE_NAME}.zip | cut -f1))"
echo ""
echo "Pour distribuer :"
echo "  1. Envoyez le fichier ${PACKAGE_NAME}.zip"
echo "  2. L'utilisateur doit installer FFmpeg"
echo "  3. Extraire le ZIP et lancer run.bat"
