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
