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
