================================================================================
  SIERRA ROBOT VIDEO EXTRACTOR - Windows v2.4.1
================================================================================

IMPORTANT: INSTALLATION DE FFMPEG COMPLET (OBLIGATOIRE)
--------------------------------------------------------

Ce programme NECESSITE FFmpeg COMPLET avec support ProRes pour fonctionner.

INSTALLATION FFMPEG SUR WINDOWS:

1. Telecharger FFmpeg COMPLET (pas Essentials):
   https://www.gyan.dev/ffmpeg/builds/
   > Choisir "ffmpeg-release-full.7z" (200-300 MB)

2. Extraire le fichier avec 7-Zip dans C:\ffmpeg\

3. Ajouter au PATH Windows:
   - Clic droit sur "Ce PC" > Proprietes
   - Parametres systeme avances > Variables d'environnement
   - Dans "Variables systeme", selectionner "Path" > Modifier
   - Cliquer "Nouveau" et ajouter: C:\ffmpeg\bin
   - Cliquer OK sur toutes les fenetres

4. Verifier l'installation:
   - Ouvrir un NOUVEAU terminal (cmd)
   - Taper: ffmpeg -version
   - Taper: ffmpeg -codecs | findstr prores
   - Vous devez voir "DEV.L. prores" pour que l'export MOV fonctionne

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
├── 230/
│   ├── 230_composite.mov    # NOUVEAU: ProRes 4444 RGBA avec transparence
│   ├── 230_video.mkv        # MKV multi-couches (BASE/REMAP/ALPHA/LUMINANCE)
│   ├── 230_audio.wav        # Audio PCM 22050 Hz mono
│   ├── 230_metadata.txt     # Metadonnees completes
│   └── 230_frames/          # Frames PNG RGBA individuelles
│       ├── frame_0000.png
│       └── ...
└── ...

FICHIERS GENERES
-----------------

1. *_composite.mov (ProRes 4444 RGBA) - RECOMMANDE
   - Canal alpha natif (transparence)
   - Qualite professionnelle quasi-lossless
   - Compatible: DaVinci Resolve, Premiere Pro, After Effects, Final Cut
   - Taille: ~10 MB pour 10 secondes

2. *_video.mkv (Multi-couches) - FORMAT TECHNIQUE
   - 4 pistes video separees (BASE, REMAP, ALPHA, LUMINANCE)
   - Acces aux couches separees pour analyse/edition avancee
   - Taille: ~2-5 MB pour 10 secondes (selon codec)

PROBLEME: "PAS D'IMAGE DANS LE MOV" ?
--------------------------------------

Executez verify_mov.bat pour diagnostic automatique.

Ce script va :
1. Trouver automatiquement le premier fichier MOV
2. Afficher les proprietes du codec
3. Extraire 3 frames PNG de test
4. Vous guider selon le resultat

LECTEURS VIDEO COMPATIBLES MOV PRORES 4444:

   COMPATIBLES (avec transparence alpha):
   - DaVinci Resolve (GRATUIT, recommande)
     https://www.blackmagicdesign.com/products/davinciresolve
   - Adobe Premiere Pro
   - Adobe After Effects
   - MPV avec --vo=gpu

   INCOMPATIBLES (pas de support alpha ProRes):
   - VLC Media Player
   - Windows Media Player
   - Lecteur Films et TV (Windows)

Pour plus de details, consultez PAS_DIMAGE.txt

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
