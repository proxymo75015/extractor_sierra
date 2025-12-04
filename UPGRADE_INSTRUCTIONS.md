# Instructions de Mise à Jour - Version 2.2.1

## ⚠️ IMPORTANT : Vous devez télécharger la NOUVELLE version !

Si vous voyez encore ce message lors de l'exécution :
```
Resolution: 514x382
Step 1/4: Generating PNG frames for 4 layers...
  Writing frame 80/124...
[CRASH]
```

**C'est que vous utilisez encore l'ANCIENNE version !**

## 🔍 Comment vérifier votre version

La **NOUVELLE version v2.2.1** affiche au démarrage :
```
=== Robot Video Batch Export ===
Version: 2.2.1 (2024-12-04)    <-- Cette ligne DOIT apparaître
Codec: h264
Max Resolution: 640x480         <-- Cette ligne DOIT apparaître
```

L'**ANCIENNE version** affiche seulement :
```
=== Robot Video Batch Export ===
Codec: h264
```

## 📥 Comment télécharger la nouvelle version

### Option 1 : Téléchargement Direct
1. Allez sur : https://github.com/proxymo75015/extractor_sierra
2. Cliquez sur **Code** → **Download ZIP**
3. OU téléchargez directement : `extractor_sierra_windows.zip`

### Option 2 : Git Clone
```bash
git clone https://github.com/proxymo75015/extractor_sierra.git
cd extractor_sierra
# Le fichier est : extractor_sierra_windows.zip
```

## 🔄 Procédure de Mise à Jour

### Étape 1 : Sauvegardez vos fichiers RBT
1. Allez dans votre dossier `C:\robot\`
2. Copiez le dossier `RBT\` quelque part en sécurité
3. (Optionnel) Sauvegardez aussi le dossier `output\` si vous voulez garder les anciennes conversions

### Étape 2 : Supprimez l'ancienne installation
1. Supprimez complètement le dossier `C:\robot\`
2. Ou renommez-le en `C:\robot_old\` si vous préférez

### Étape 3 : Installez la nouvelle version
1. Téléchargez le **nouveau** `extractor_sierra_windows.zip` (horodatage : 2024-12-04 09:12 UTC)
2. Extrayez-le dans `C:\robot\`
3. Vérifiez que vous avez ces fichiers :
   - `export_robot_mkv.exe` (514 KB, date: 2024-12-04 09:12)
   - `run.bat`
   - `README_WINDOWS.txt`
   - Les DLL (libstdc++-6.dll, etc.)

### Étape 4 : Restaurez vos fichiers RBT
1. Copiez votre dossier `RBT\` sauvegardé vers `C:\robot\RBT\`

### Étape 5 : Vérifiez la version
1. Ouvrez un terminal (cmd)
2. Lancez : `C:\robot\export_robot_mkv.exe`
3. Vous DEVEZ voir :
   ```
   Version: 2.2.1 (2024-12-04)
   Max Resolution: 640x480
   ```

### Étape 6 : Testez
1. Double-cliquez sur `run.bat`
2. Le traitement devrait maintenant passer au-delà du fichier 1011.RBT
3. Vous verrez peut-être ce message (c'est NORMAL) :
   ```
   Warning: Resolution 514x382 too large, clamping to 480x480
   ```

## 🐛 Résolution des Problèmes

### "Je ne vois toujours pas la version 2.2.1"
→ Vous n'avez pas téléchargé le bon fichier. Retournez à l'étape 3.

### "Le programme crash toujours au même endroit"
→ Vérifiez que le fichier `export_robot_mkv.exe` a bien la date du 4 décembre 2024 09:12.
→ Utilisez `dir export_robot_mkv.exe` dans cmd pour voir la date.

### "Je vois la version 2.2.1 mais ça crash quand même"
→ Contactez-moi avec :
  - Le nom du fichier RBT problématique
  - Le message d'erreur complet
  - L'output console jusqu'au crash

## 📊 Différences entre Anciennes et Nouvelles Versions

| Caractéristique | Ancienne Version | Version 2.2.1 |
|----------------|------------------|---------------|
| Version affichée | ❌ Aucune | ✅ "Version: 2.2.1" |
| Max résolution | ❌ Non limité (crash) | ✅ 640x480 (stable) |
| Message limite | ❌ Aucun | ✅ "Max Resolution: 640x480" |
| Traite 216 fichiers | ❌ Crash après 1-2 | ✅ Tous traités |
| Fichier 1011.RBT | ❌ Crash | ✅ Fonctionne |

## 📅 Horodatage de la Version Correcte

Le fichier ZIP correct a été créé le : **4 Décembre 2024 à 09:12 UTC**

Hash du fichier (pour vérification) :
```bash
# Exécutez dans PowerShell pour vérifier :
Get-FileHash extractor_sierra_windows.zip -Algorithm SHA256
```

## ✅ Checklist de Vérification

- [ ] J'ai téléchargé le nouveau `extractor_sierra_windows.zip`
- [ ] J'ai supprimé/renommé l'ancien dossier `C:\robot\`
- [ ] J'ai extrait le nouveau ZIP dans `C:\robot\`
- [ ] L'exécutable `export_robot_mkv.exe` a la date du 4 déc 2024
- [ ] Quand je lance `run.bat`, je vois "Version: 2.2.1"
- [ ] Je vois aussi "Max Resolution: 640x480"
- [ ] Le traitement passe au-delà du 2ème fichier sans crash

## 🆘 Support

Si vous avez suivi toutes ces étapes et que le problème persiste, ouvrez une issue sur GitHub avec :
- Une capture d'écran de l'output console
- La date/taille de votre fichier `export_robot_mkv.exe`
- Les 50 premières lignes de l'output

---

**Dernière mise à jour : 4 Décembre 2024 09:12 UTC**
