================================================================================
  SIERRA ROBOT VIDEO EXTRACTOR - Windows v2.5.0
================================================================================

IMPORTANT: INSTALLATION DE FFMPEG (OBLIGATOIRE)
------------------------------------------------

Ce programme NECESSITE FFmpeg pour fonctionner. Sans FFmpeg, seuls les
fichiers WAV seront generes (pas de MOV ni MP4).

INSTALLATION FFMPEG SUR WINDOWS:

1. Telecharger FFmpeg FULL BUILD (support ProRes):
   https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-full.7z

2. Extraire le fichier dans C:\ffmpeg\

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
   export_robot_mkv.exe [codec] [--canvas WIDTHxHEIGHT]

   Codecs disponibles :
   - h264  (defaut, recommande)
   - h265  (meilleure compression)
   - vp9   (open source)
   - ffv1  (lossless)
   - prores (ProRes 4444 avec transparence, QuickTime MOV)

   Options :
   - --canvas WIDTHxHEIGHT : Force la resolution du canvas (ex: --canvas 640x480)
     Si omis, detecte automatiquement la resolution appropriee (VGA, etc.)

3. Les resultats seront dans le dossier output/

EXEMPLES D'UTILISATION
---------------------

# Detection automatique du canvas (recommande)
export_robot_mkv.exe h264

# Forcer le canvas a 640x480 (VGA Sierra SCI32)
export_robot_mkv.exe h264 --canvas 640x480

# Export ProRes 4444 avec transparence (QuickTime MOV)
export_robot_mkv.exe prores

# Export ProRes avec canvas force
export_robot_mkv.exe prores --canvas 800x600

STRUCTURE DE SORTIE
-------------------

output/
├── 91/
│   ├── 91_video.mov        # MOV ProRes 4444 RGBA + audio
│   ├── 91_audio.wav        # Audio PCM 22 kHz
│   ├── 91_composite.mp4    # Video composite H.264
│   └── 91_metadata.txt     # Metadonnees
└── ...

FORMAT MOV PRORES 4444
----------------------

Le fichier MOV contient :
- Video : ProRes 4444 (ap4h) RGBA 10-bit
- Audio : PCM S16LE 22050 Hz mono
- Transparence : Canal alpha preserve (pixels transparents = noir alpha 0)
- Positions : ScummVM compatibles (celX, celY preserves)

DEPANNAGE
---------

PROBLEME: "FFmpeg is not installed or not in PATH"
SOLUTION: Verifier que FFmpeg est installe et dans le PATH (voir ci-dessus)

PROBLEME: Seuls les fichiers .wav sont generes
SOLUTION: FFmpeg n'est pas correctement installe. Refaire l'installation.

PROBLEME: "No .RBT files found"
SOLUTION: Placer vos fichiers .RBT dans le dossier RBT/

PROBLEME: "Unknown encoder 'prores_ks'"
SOLUTION: Installer FFmpeg FULL BUILD (pas Essentials). Voir installation ci-dessus.

SUPPORT
-------

GitHub : https://github.com/proxymo75015/extractor_sierra
Documentation : docs/

================================================================================
