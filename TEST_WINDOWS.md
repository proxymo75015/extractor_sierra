# Test Windows - Instructions de débogage

## Problème actuel
L'exécutable génère uniquement les fichiers WAV, pas les MKV ni MP4.

## Corrections apportées (version du 1er décembre 2025)

### 1. Commandes système adaptées pour Windows
- `tail -5` → `2>nul` sous Windows, `2>&1 | tail -5` sous Linux
- `rm -rf` → `rd /s /q` sous Windows, `rm -rf` sous Linux
- Vérification FFmpeg : `>nul` sous Windows, `>/dev/null` sous Linux

### 2. Fichiers modifiés
- `src/export_robot_mkv.cpp` : Vérification FFmpeg compatible Windows
- `src/formats/robot_mkv_exporter.cpp` : Commandes FFmpeg et nettoyage compatibles Windows

## Test à effectuer sous Windows

1. **Vérifier FFmpeg** :
   ```cmd
   ffmpeg -version
   ```
   Doit afficher la version de FFmpeg (comme dans la capture : N-110589-g30c71ef98e)

2. **Placer un fichier RBT** dans `RBT/` (par exemple `91.RBT`)

3. **Lancer manuellement** :
   ```cmd
   export_robot_mkv.exe h264
   ```

4. **Vérifier la sortie console** :
   - Doit afficher : "Checking FFmpeg availability..."
   - Doit afficher : "FFmpeg found!"
   - Doit afficher : "Step 1/4: Generating PNG frames..."
   - Doit afficher : "Step 2/4: Encoding MKV with 4 video tracks..."
   - Doit afficher : "Step 3/4: Cleaning up temporary files..."
   - Doit afficher : "Step 4/4: Export complete!"

5. **Vérifier les fichiers générés** dans `output/91/` :
   - `91_video.mkv` (devrait exister maintenant)
   - `91_audio.wav` (existe déjà)
   - `91_composite.mp4` (devrait exister maintenant)
   - `91_metadata.txt` (devrait exister)

## Si ça ne fonctionne toujours pas

### Test manuel de la commande FFmpeg

Créer un dossier de test et essayer :
```cmd
mkdir test_ffmpeg
cd test_ffmpeg
echo test > test.txt
ffmpeg -version 2>nul
echo Code de retour : %errorlevel%
```

Le code de retour doit être 0.

### Vérifier les dossiers temporaires

Pendant l'exécution, vérifier si des dossiers temporaires sont créés :
- `.robot_mkv_temp_XXXXX_base/`
- `.robot_mkv_temp_XXXXX_remap/`
- `.robot_mkv_temp_XXXXX_alpha/`
- `.robot_mkv_temp_XXXXX_composite/`

Si ces dossiers ne sont pas créés → Problème de permissions
Si ces dossiers restent après exécution → La commande de nettoyage échoue
Si ces dossiers sont vides → Les PNG ne sont pas générés

### Activer le mode verbose FFmpeg

Modifier temporairement la ligne 276 de `robot_mkv_exporter.cpp` :
```cpp
// Au lieu de :
cmd << " -f matroska \"" << outputFile << "\" 2>nul";

// Utiliser :
cmd << " -f matroska \"" << outputFile << "\"";
```

Cela affichera les erreurs FFmpeg dans la console.

## Version du package
- **Version Linux** : `export_robot_mkv` (299 KB)
- **Version Windows** : `export_robot_mkv.exe` (512 KB)
- **Package Windows** : `extractor_sierra_windows.zip` (8.2 MB avec DLLs)
- **Date de compilation** : 1er décembre 2025, 15:28 UTC

## Prochaines étapes si le problème persiste

1. Capturer la sortie console complète lors de l'exécution
2. Vérifier si les dossiers temporaires sont créés
3. Tester FFmpeg manuellement avec une commande simple
4. Vérifier les permissions d'écriture dans le répertoire
