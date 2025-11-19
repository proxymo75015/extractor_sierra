#!/usr/bin/env python3
"""
Reconstruction de l'audio stéréo complet, puis séparation en canaux L/R.
Utilise le robot_decoder pour générer l'audio stéréo entrelacé,
puis sépare en deux fichiers mono.
"""

import os
import sys
import struct
import subprocess

OUTPUT_DIR = "output_91_extraction"
OUTPUT_STEREO_DIR = "output_91_stereo"
SAMPLE_RATE = 22050

def extract_full_audio():
    """Extrait l'audio complet avec robot_decoder."""
    
    print("╔" + "═" * 78 + "╗")
    print("║" + " " * 15 + "EXTRACTION AUDIO STÉRÉO COMPLÈTE" + " " * 30 + "║")
    print("╚" + "═" * 78 + "╝\n")
    
    print("Étape 1: Extraction avec robot_decoder...")
    
    # Nettoyer l'ancien répertoire
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    # Exécuter robot_decoder
    result = subprocess.run(
        ["./build/robot_decoder", "ScummVM/rbt/91.RBT", OUTPUT_DIR + "/"],
        capture_output=True,
        text=True
    )
    
    if result.returncode != 0:
        print(f"❌ Erreur lors de l'extraction: {result.stderr}")
        return False
    
    print("✅ Extraction terminée\n")
    
    # Chercher le fichier audio
    audio_file = None
    for root, dirs, files in os.walk(OUTPUT_DIR):
        for f in files:
            if f.endswith('.pcm') or f == 'audio.raw':
                audio_file = os.path.join(root, f)
                break
        if audio_file:
            break
    
    if not audio_file:
        print("❌ Fichier audio non trouvé")
        return False
    
    print(f"📁 Fichier audio trouvé: {audio_file}\n")
    
    # Lire le fichier audio (stéréo entrelacé: L R L R L R...)
    with open(audio_file, 'rb') as f:
        audio_data = f.read()
    
    num_samples = len(audio_data) // 2  # int16
    
    if num_samples % 2 != 0:
        print("⚠️  Nombre de samples impair, ajustement...")
        audio_data = audio_data[:-2]
        num_samples -= 1
    
    print(f"📊 Audio stéréo:")
    print(f"   • Samples totaux: {num_samples:,}")
    print(f"   • Durée: {num_samples / 2 / SAMPLE_RATE:.3f} sec")
    print(f"   • Taille: {len(audio_data) / 1024:.1f} KB\n")
    
    print("Étape 2: Séparation des canaux L/R...\n")
    
    # Séparer en canaux L et R
    left_samples = []
    right_samples = []
    
    for i in range(0, num_samples, 2):
        sample_bytes = audio_data[i*2:(i+1)*2]
        left_sample = struct.unpack('<h', sample_bytes)[0]
        left_samples.append(left_sample)
        
        sample_bytes = audio_data[(i+1)*2:(i+2)*2]
        right_sample = struct.unpack('<h', sample_bytes)[0]
        right_samples.append(right_sample)
    
    # Créer le répertoire de sortie
    os.makedirs(OUTPUT_STEREO_DIR, exist_ok=True)
    
    # Écrire les fichiers
    left_file = os.path.join(OUTPUT_STEREO_DIR, "audio_left.pcm")
    right_file = os.path.join(OUTPUT_STEREO_DIR, "audio_right.pcm")
    
    with open(left_file, 'wb') as f:
        for sample in left_samples:
            f.write(struct.pack('<h', sample))
    
    with open(right_file, 'wb') as f:
        for sample in right_samples:
            f.write(struct.pack('<h', sample))
    
    # Statistiques
    left_duration = len(left_samples) / SAMPLE_RATE
    right_duration = len(right_samples) / SAMPLE_RATE
    
    print(f"✅ Séparation terminée!\n")
    
    print(f"📊 Canal LEFT:")
    print(f"   • Fichier: {left_file}")
    print(f"   • Samples: {len(left_samples):,}")
    print(f"   • Durée: {left_duration:.3f} sec")
    print(f"   • Taille: {len(left_samples) * 2 / 1024:.1f} KB\n")
    
    print(f"📊 Canal RIGHT:")
    print(f"   • Fichier: {right_file}")
    print(f"   • Samples: {len(right_samples):,}")
    print(f"   • Durée: {right_duration:.3f} sec")
    print(f"   • Taille: {len(right_samples) * 2 / 1024:.1f} KB\n")
    
    print(f"💡 Pour écouter:")
    print(f"   ffplay -f s16le -ar {SAMPLE_RATE} -ac 1 {left_file}")
    print(f"   ffplay -f s16le -ar {SAMPLE_RATE} -ac 1 {right_file}\n")
    
    print(f"💡 Pour convertir en WAV:")
    print(f"   ffmpeg -f s16le -ar {SAMPLE_RATE} -ac 1 -i {left_file} {OUTPUT_STEREO_DIR}/audio_left.wav")
    print(f"   ffmpeg -f s16le -ar {SAMPLE_RATE} -ac 1 -i {right_file} {OUTPUT_STEREO_DIR}/audio_right.wav\n")
    
    return True

if __name__ == "__main__":
    success = extract_full_audio()
    sys.exit(0 if success else 1)
