#!/bin/bash
# Script de compilation croisée pour Windows (MinGW)

set -e

echo "=== Compilation Windows (MinGW-w64) ==="

# Vérifier si MinGW est installé
if ! command -v x86_64-w64-mingw32-g++ &> /dev/null; then
    echo "Installation de MinGW-w64..."
    sudo apt-get update
    sudo apt-get install -y mingw-w64 g++-mingw-w64-x86-64
fi

# Créer le répertoire de build Windows
BUILD_DIR="build_windows"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo ""
echo "Configuration CMake pour Windows..."

# Configurer CMake avec le toolchain MinGW
cmake .. \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
    -DCMAKE_FIND_ROOT_PATH=/usr/x86_64-w64-mingw32 \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_BUILD_TYPE=Release

echo ""
echo "Compilation..."

# Compiler
cmake --build . --target export_robot_mkv --config Release -j$(nproc)

# Vérifier le résultat
if [ -f "export_robot_mkv.exe" ]; then
    echo ""
    echo "✓ Compilation réussie !"
    echo ""
    echo "Exécutable Windows : $PWD/export_robot_mkv.exe"
    ls -lh export_robot_mkv.exe
    
    # Copier à la racine
    cp export_robot_mkv.exe ../export_robot_mkv_windows.exe
    echo ""
    echo "✓ Copié vers : export_robot_mkv_windows.exe"
    
    # Informations sur les DLL nécessaires
    echo ""
    echo "=== DLL requises (à distribuer avec l'exécutable) ==="
    x86_64-w64-mingw32-objdump -p export_robot_mkv.exe | grep "DLL Name:" | sort -u
    
else
    echo "✗ Erreur : export_robot_mkv.exe n'a pas été généré"
    exit 1
fi

echo ""
echo "=== Package Windows ==="
echo "Pour créer un package complet, exécutez :"
echo "  ./create_windows_package.sh"
