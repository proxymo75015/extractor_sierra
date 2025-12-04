@echo off
REM Script de verification detaillee du MOV ProRes genere
echo ===== Verification MOV ProRes =====
echo.

REM Verifier FFmpeg
where ffmpeg >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERREUR: FFmpeg introuvable
    pause
    exit /b 1
)

REM Trouver le premier MOV
for /r output\ %%f in (*.mov) do (
    set MOV_FILE=%%f
    goto :found
)

echo ERREUR: Aucun MOV trouve
pause
exit /b 1

:found
echo Fichier: %MOV_FILE%
echo.

REM Proprietes codec
echo === Codec Video ===
ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,pix_fmt,width,height,nb_frames -of default=noprint_wrappers=1 "%MOV_FILE%"
echo.

REM Extraire 3 frames a differents moments
echo === Extraction frames test ===
set OUT_DIR=%~dp0test_frames
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

echo Frame 5...
ffmpeg -i "%MOV_FILE%" -vf "select=eq(n\,5)" -frames:v 1 "%OUT_DIR%\frame_005.png" -y 2>nul
echo Frame 10...
ffmpeg -i "%MOV_FILE%" -vf "select=eq(n\,10)" -frames:v 1 "%OUT_DIR%\frame_010.png" -y 2>nul
echo Frame 20...
ffmpeg -i "%MOV_FILE%" -vf "select=eq(n\,20)" -frames:v 1 "%OUT_DIR%\frame_020.png" -y 2>nul

echo.
echo Frames extraites dans: %OUT_DIR%
echo.

REM Lister les frames extraites
dir /b "%OUT_DIR%\*.png" 2>nul
if %ERRORLEVEL% EQU 0 (
    echo.
    echo SUCCES: Frames extraites du MOV
    echo Ouvrez les fichiers PNG ci-dessus pour voir le contenu
    echo.
    echo Si les PNG montrent une image:
    echo   - Le MOV contient bien l'image
    echo   - Votre lecteur video ne supporte pas ProRes 4444 correctement
    echo   - Utilisez DaVinci Resolve, After Effects ou QuickTime
    echo.
    echo Si les PNG sont noirs:
    echo   - Probleme d'encodage ProRes
    echo   - Contactez le support avec cette sortie
) else (
    echo ERREUR: Impossible d'extraire les frames
)

echo.
pause
