@echo off
echo ================================================================================
echo   EXTRACTOR SIERRA - Extraction Batch
echo ================================================================================
echo.

if not exist RBT\ (
    echo ERREUR: Le repertoire RBT\ n'existe pas
    echo.
    echo Creez un repertoire RBT\ et placez-y vos fichiers .RBT
    echo Exemple: RBT\161.RBT, RBT\260.RBT, etc.
    echo.
    pause
    exit /b 1
)

echo Verification FFmpeg...
ffmpeg -version >nul 2>&1
if errorlevel 1 (
    echo ERREUR: FFmpeg n'est pas installe ou pas dans le PATH
    echo.
    echo Telechargez FFmpeg: https://ffmpeg.org/download.html#build-windows
    echo Installez-le et ajoutez-le au PATH Windows
    echo.
    pause
    exit /b 1
)
echo OK FFmpeg trouve
echo.

echo Lancement extraction...
echo.
export_robot_mkv.exe RBT\

echo.
echo ================================================================================
echo   Extraction terminee!
echo ================================================================================
echo.
echo Fichiers generes dans: output\
echo.
pause
