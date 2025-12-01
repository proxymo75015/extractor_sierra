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
