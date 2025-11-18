#!/usr/bin/env python3
"""
Script pour extraire et créer une vidéo depuis un fichier RBT
"""
import sys
import os
import subprocess

def main():
    if len(sys.argv) < 2:
        print("Usage: extract_and_make_video.py <file.rbt>")
        sys.exit(1)
    
    rbt_file = sys.argv[1]
    base_name = os.path.splitext(os.path.basename(rbt_file))[0]
    output_dir = f"output_{base_name}"
    
    print(f"Extracting {rbt_file}...")
    
    # Créer le répertoire de sortie
    os.makedirs(output_dir, exist_ok=True)
    os.makedirs(f"{output_dir}/frames", exist_ok=True)
    
    # Utiliser le parser C++ pour extraire frames et audio
    # Note: Adapter selon le vrai extracteur disponible
    print("Note: Utiliser main.cpp avec les bons arguments")
    print(f"Output directory: {output_dir}")
    
    # Pour l'instant, copier les frames depuis build/rbt_dumps si elles existent
    dump_dir = f"build/rbt_dumps/{base_name}"
    if os.path.exists(dump_dir):
        print(f"Frames found in {dump_dir}, processing...")
        # Traitement à faire
    
    # Générer la vidéo
    frames_dir = f"{output_dir}/frames"
    audio_file = f"{output_dir}/audio.wav"
    output_video = f"{output_dir}/{base_name}_video.mp4"
    
    if os.path.exists(frames_dir) and os.path.exists(audio_file):
        print(f"Generating video {output_video}...")
        subprocess.run([
            "./tools/make_scummvm_video.py",
            frames_dir,
            audio_file,
            "10",
            output_video
        ])

if __name__ == "__main__":
    main()
