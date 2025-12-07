================================================================================
  EXTRACTOR SIERRA - Version Windows 3.0.0
================================================================================

Extracteur video professionnel pour fichiers Robot (.RBT) de Sierra SCI32
Genere MKV multicouche + MOV ProRes 4444 RGBA avec transparence

================================================================================
  CONTENU DU PACKAGE
================================================================================

export_robot_mkv.exe       - Programme principal d'extraction batch
robot_extractor.exe        - Programme extraction individuelle (legacy)
extract_coordinates.exe    - Extraction coordonnees depuis RESSCI
README_WINDOWS.txt         - Ce fichier
LICENSE                    - Licence MIT

================================================================================
  PREREQUIS
================================================================================

IMPORTANT: FFmpeg doit etre installe et accessible dans le PATH Windows

Telechargement FFmpeg:
  https://ffmpeg.org/download.html#build-windows
  
Installation:
  1. Telecharger ffmpeg-release-essentials.zip
  2. Extraire dans C:\ffmpeg\
  3. Ajouter C:\ffmpeg\bin au PATH Windows
  
Verification:
  cmd> ffmpeg -version
  cmd> ffprobe -version

================================================================================
  UTILISATION
================================================================================

Mode Batch (recommande):
  export_robot_mkv.exe RBT\

  Extrait tous les fichiers .RBT du repertoire vers output\

Mode fichier unique:
  robot_extractor.exe fichier.RBT output_dir\

================================================================================
  FICHIERS GENERES
================================================================================

Pour chaque Robot {ID}.RBT:

  output\{ID}\{ID}_video.mkv       - MKV 4 pistes (BASE, REMAP, ALPHA, LUMA)
  output\{ID}\{ID}_video.mov       - MOV ProRes 4444 RGBA + audio
  output\{ID}\{ID}_audio.wav       - Audio WAV 22050 Hz mono
  output\{ID}\{ID}_frames\         - Frames PNG RGBA individuelles
  output\{ID}\{ID}_metadata.txt    - Metadonnees du Robot

Recapitulatif:
  output\robot_coordinates_summary.txt - Liste 2190 coordonnees RESSCI

================================================================================
  MODES AUTOMATIQUES
================================================================================

CANVAS MODE (coordonnees RESSCI trouvees):
  - Resolution: 630x450 pixels (canvas Phantasmagoria)
  - Position: ScummVM formula (robotX + celX, robotY + celY - celHeight)
  - Exemple: Robot 260 -> position (257, 257) sur canvas 630x450

CROP MODE (pas de coordonnees RESSCI):
  - Resolution: Tight crop automatique (bounding box minimale)
  - Position: celX, celY - celHeight
  - Exemple: Robot 161 -> 112x155 pixels

================================================================================
  STRUCTURE REPERTOIRES
================================================================================

Projet\
+-- export_robot_mkv.exe
+-- robot_extractor.exe
+-- extract_coordinates.exe
+-- RBT\                   <- Placer vos fichiers .RBT ici
|   +-- 161.RBT
|   +-- 260.RBT
|   +-- ...
+-- Resource\              <- Optionnel: fichiers RESSCI pour coordonnees
|   +-- RESSCI.001
|   +-- RESSCI.002
+-- output\                <- Fichiers extraits generes ici
    +-- 161\
    +-- 260\
    +-- robot_coordinates_summary.txt

================================================================================
  DEPANNAGE
================================================================================

Erreur "ffmpeg not found":
  -> Installer FFmpeg et ajouter au PATH Windows

Erreur "VCRUNTIME140.dll manquant":
  -> Les executables sont compiles en statique, cette erreur ne devrait
     plus apparaitre. Si elle persiste, reinstallez le package.

Video MKV vide ou corrompue:
  -> Verifier version FFmpeg (minimum 4.0)
  -> Reessayer avec ffmpeg.exe dans le meme repertoire

================================================================================
  SUPPORT
================================================================================

Documentation: https://github.com/proxymo75015/robot_extract/tree/main/docs
Issues:        https://github.com/proxymo75015/robot_extract/issues

================================================================================
  LICENCE
================================================================================

MIT License - Copyright (c) 2025
Voir LICENSE pour details complets

================================================================================

Version 3.0.0 - Decembre 2025
