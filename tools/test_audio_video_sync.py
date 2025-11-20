#!/usr/bin/env python3
"""
Test de synchronisation audio/vidéo pour le format Robot.
Vérifie que chaque frame audio a la même durée que la frame vidéo associée
pour les canaux LEFT (EVEN) et RIGHT (ODD).
"""

import os
import sys

# Configuration
DUMPS_DIR = "build/rbt_dumps/91"
SAMPLE_RATE = 22050  # Hz
VIDEO_FPS = 10  # frames par seconde
FRAME_DURATION_MS = 1000 / VIDEO_FPS  # 100 ms
EXPECTED_SAMPLES_PER_FRAME = int(SAMPLE_RATE / VIDEO_FPS)  # 2205 samples

def count_samples_in_pcm(pcm_file):
    """Compte les samples dans un fichier PCM 16-bit mono."""
    if not os.path.exists(pcm_file):
        return None
    file_size = os.path.getsize(pcm_file)
    return file_size // 2  # 2 bytes par sample (int16)

def test_synchronization():
    """Test la synchronisation audio/vidéo."""
    
    print("╔" + "═" * 78 + "╗")
    print("║" + " " * 15 + "TEST DE SYNCHRONISATION AUDIO/VIDÉO" + " " * 28 + "║")
    print("╚" + "═" * 78 + "╝\n")
    
    if not os.path.exists(DUMPS_DIR):
        print(f"❌ Erreur: dossier {DUMPS_DIR} non trouvé")
        print("   Exécutez d'abord: cd build && ./rbt_dumper")
        return False
    
    # Trouver toutes les frames
    frame_dirs = [d for d in os.listdir(DUMPS_DIR) if d.startswith('frame_')]
    frame_numbers = sorted([int(d.split('_')[1]) for d in frame_dirs])
    
    if not frame_numbers:
        print("❌ Aucune frame trouvée")
        return False
    
    print(f"📊 Configuration:")
    print(f"   • Sample rate: {SAMPLE_RATE} Hz")
    print(f"   • FPS vidéo: {VIDEO_FPS}")
    print(f"   • Durée frame vidéo: {FRAME_DURATION_MS:.1f} ms")
    print(f"   • Samples attendus/frame: {EXPECTED_SAMPLES_PER_FRAME}")
    print(f"   • Frames à tester: {len(frame_numbers)}\n")
    
    # Tester chaque frame
    all_ok = True
    total_samples = 0
    
    for frame_num in frame_numbers:
        frame_dir = os.path.join(DUMPS_DIR, f"frame_{frame_num}")
        audio_file = os.path.join(frame_dir, "audio_decomp.pcm")
        
        num_samples = count_samples_in_pcm(audio_file)
        
        if num_samples is None:
            print(f"Frame {frame_num:3d}: ❌ Fichier audio manquant")
            all_ok = False
            continue
        
        total_samples += num_samples
        duration_ms = (num_samples / SAMPLE_RATE) * 1000
        
        if num_samples == EXPECTED_SAMPLES_PER_FRAME:
            status = "✅"
        else:
            status = "❌"
            all_ok = False
        
        if num_samples != EXPECTED_SAMPLES_PER_FRAME or frame_num < 5:
            print(f"Frame {frame_num:3d}: {status} {num_samples:4d} samples "
                  f"({duration_ms:6.2f} ms)")
    
    # Statistiques globales
    print("\n" + "─" * 80)
    print(f"\n📈 Résultats globaux:")
    
    total_video_duration = len(frame_numbers) * FRAME_DURATION_MS
    total_audio_duration = (total_samples / SAMPLE_RATE) * 1000
    
    print(f"   • Total frames: {len(frame_numbers)}")
    print(f"   • Total samples: {total_samples:,}")
    print(f"   • Durée vidéo: {total_video_duration:.2f} ms ({total_video_duration/1000:.3f} sec)")
    print(f"   • Durée audio: {total_audio_duration:.2f} ms ({total_audio_duration/1000:.3f} sec)")
    print(f"   • Différence: {abs(total_video_duration - total_audio_duration):.3f} ms")
    
    # Vérification de la constance
    if all_ok:
        print(f"\n✅ SUCCÈS: Synchronisation parfaite!")
        print(f"   Chaque frame audio = {EXPECTED_SAMPLES_PER_FRAME} samples = {FRAME_DURATION_MS:.1f} ms")
        print(f"   Correspondance exacte avec les frames vidéo")
        print(f"\n💡 Note: Les canaux LEFT (EVEN) et RIGHT (ODD) sont entrelacés")
        print(f"   dans le flux mono. Chaque packet décompressé contient les")
        print(f"   données des deux canaux, alternant entre EVEN et ODD.")
    else:
        print(f"\n❌ ÉCHEC: Des frames ne correspondent pas à la durée attendue")
    
    return all_ok

if __name__ == "__main__":
    success = test_synchronization()
    sys.exit(0 if success else 1)
