#!/usr/bin/env python3
"""
Extraction SIMPLIFIÉE des canaux EVEN et ODD de 91.RBT

Principe basé sur ScummVM robot.h:
- L'audio original mono 22050Hz est divisé en 2 canaux à 11025Hz chacun
- Canal EVEN (positions divisibles par 4) et canal ODD (autres positions)
- Chaque canal contient un échantillon sur deux du signal original
- L'entrelacement des deux canaux reconstitue le signal mono 22050Hz

Process:
1. Chaque frame contient UN packet audio compressé (2213 bytes)
2. Décompression DPCM16 → 2205 samples @ 11025Hz
3. Classification: audioPos % 4 == 0 → EVEN, sinon → ODD
4. Les 8 premiers samples sont du 'runway' DPCM et ne sont pas utilisés
5. Ajout séquentiel au fichier EVEN ou ODD correspondant

Pour reconstruire l'audio original: entrelacer EVEN et ODD → mono 22050Hz
"""
import struct
import os
import sys

# Table DPCM16 de ScummVM
DPCM16_TABLE = [
    0x0000,0x0008,0x0010,0x0020,0x0030,0x0040,0x0050,0x0060,0x0070,0x0080,
    0x0090,0x00A0,0x00B0,0x00C0,0x00D0,0x00E0,0x00F0,0x0100,0x0110,0x0120,
    0x0130,0x0140,0x0150,0x0160,0x0170,0x0180,0x0190,0x01A0,0x01B0,0x01C0,
    0x01D0,0x01E0,0x01F0,0x0200,0x0208,0x0210,0x0218,0x0220,0x0228,0x0230,
    0x0238,0x0240,0x0248,0x0250,0x0258,0x0260,0x0268,0x0270,0x0278,0x0280,
    0x0288,0x0290,0x0298,0x02A0,0x02A8,0x02B0,0x02B8,0x02C0,0x02C8,0x02D0,
    0x02D8,0x02E0,0x02E8,0x02F0,0x02F8,0x0300,0x0308,0x0310,0x0318,0x0320,
    0x0328,0x0330,0x0338,0x0340,0x0348,0x0350,0x0358,0x0360,0x0368,0x0370,
    0x0378,0x0380,0x0388,0x0390,0x0398,0x03A0,0x03A8,0x03B0,0x03B8,0x03C0,
    0x03C8,0x03D0,0x03D8,0x03E0,0x03E8,0x03F0,0x03F8,0x0400,0x0440,0x0480,
    0x04C0,0x0500,0x0540,0x0580,0x05C0,0x0600,0x0640,0x0680,0x06C0,0x0700,
    0x0740,0x0780,0x07C0,0x0800,0x0900,0x0A00,0x0B00,0x0C00,0x0D00,0x0E00,
    0x0F00,0x1000,0x1400,0x1800,0x1C00,0x2000,0x3000,0x4000
]

def dpcm16_decompress(compressed_data):
    """
    Décompression DPCM16 (Delta PCM 16-bit)
    Basé sur ScummVM deDPCM16Mono() avec table de lookup
    
    Chaque byte indexe dans la table DPCM16:
    - Bit 7 = 0: ajouter tableDPCM16[delta]
    - Bit 7 = 1: soustraire tableDPCM16[delta & 0x7F]
    """
    samples = []
    sample = 0  # Sample précédent (carry)
    
    for delta_byte in compressed_data:
        # Appliquer la table DPCM16
        if delta_byte & 0x80:  # Bit 7 = 1 → soustraction
            sample -= DPCM16_TABLE[delta_byte & 0x7F]
        else:  # Bit 7 = 0 → addition
            sample += DPCM16_TABLE[delta_byte]
        
        # Saturation à 16-bit signé (-32768 à 32767)
        if sample > 32767:
            sample = 32767
        elif sample < -32768:
            sample = -32768
        
        samples.append(sample)
    
    return samples

def extract_lr_channels(rbt_file, output_dir):
    """
    Extrait les canaux EVEN et ODD en lisant directement le fichier RBT
    
    Note: Chaque canal est à 11025Hz. Pour obtenir l'audio original mono 22050Hz,
    il faut entrelacer les samples: EVEN[0], ODD[0], EVEN[1], ODD[1], ...
    """
    os.makedirs(output_dir, exist_ok=True)
    
    # Lire les positions de frames depuis le log d'extraction
    frame_info = parse_extraction_log("audio_extraction.log")
    
    if not frame_info:
        print("ERROR: Impossible de lire audio_extraction.log")
        return
    
    # Ouvrir les fichiers de sortie
    even_samples = []
    odd_samples = []
    
    # Ouvrir le fichier RBT
    with open(rbt_file, 'rb') as f:
        for frame_idx, info in enumerate(frame_info):
            audioPos = info['audioPos']
            file_offset = info['fileOffset']
            comp_size = info['compSize']
            bufferIndex = info['bufferIndex']
            
            # Se positionner et lire les données compressées
            f.seek(file_offset)
            compressed_data = f.read(comp_size)
            
            if len(compressed_data) != comp_size:
                print(f"WARNING: Frame {frame_idx} lecture incomplète ({len(compressed_data)}/{comp_size})")
                continue
            
            # Décompression DPCM16
            # Le header indique 2213 bytes compressés → 2213 samples décompressés
            # Les 8 premiers samples sont du 'runway' DPCM (pour initialiser le décodeur)
            # On garde les 2205 samples suivants (positions 8 à 2212)
            # 2205 samples @ 11025Hz = 199.5ms, mais entrelacé avec l'autre canal
            # → 2205 samples de chaque canal = 4410 samples total @ 22050Hz = 200ms
            all_samples = dpcm16_decompress(compressed_data)
            
            # Ignorer les 8 premiers samples (runway DPCM) et garder 2205 samples
            samples = all_samples[8:8+2205]
            
            # Classification et ajout
            if bufferIndex == 0:  # EVEN
                even_samples.extend(samples)
                print(f"Frame {frame_idx:2d}: EVEN - audioPos={audioPos:6d} - {len(samples)} samples @ 11025Hz")
            else:  # ODD
                odd_samples.extend(samples)
                print(f"Frame {frame_idx:2d}: ODD  - audioPos={audioPos:6d} - {len(samples)} samples @ 11025Hz")
    
    # Écrire les fichiers PCM séparés (chaque canal @ 11025Hz)
    even_file = os.path.join(output_dir, "91_EVEN.pcm")
    odd_file = os.path.join(output_dir, "91_ODD.pcm")
    
    with open(even_file, 'wb') as f:
        f.write(struct.pack(f'<{len(even_samples)}h', *even_samples))
    
    with open(odd_file, 'wb') as f:
        f.write(struct.pack(f'<{len(odd_samples)}h', *odd_samples))
    
    # Créer l'audio entrelacé (mono 22050Hz)
    interleaved_samples = []
    max_len = max(len(even_samples), len(odd_samples))
    for i in range(max_len):
        if i < len(even_samples):
            interleaved_samples.append(even_samples[i])
        else:
            interleaved_samples.append(0)  # Padding si nécessaire
        if i < len(odd_samples):
            interleaved_samples.append(odd_samples[i])
        else:
            interleaved_samples.append(0)  # Padding si nécessaire
    
    interleaved_file = os.path.join(output_dir, "91_MONO_22050Hz.pcm")
    with open(interleaved_file, 'wb') as f:
        f.write(struct.pack(f'<{len(interleaved_samples)}h', *interleaved_samples))
    
    print(f"\n{'='*60}")
    print(f"Extraction terminée:")
    print(f"  EVEN: {len(even_samples):6d} samples @ 11025Hz ({len(even_samples)/11025:.2f}s)")
    print(f"  ODD:  {len(odd_samples):6d} samples @ 11025Hz ({len(odd_samples)/11025:.2f}s)")
    print(f"  MONO entrelacé: {len(interleaved_samples):6d} samples @ 22050Hz ({len(interleaved_samples)/22050:.2f}s)")
    print(f"{'='*60}")
    
    # Conversion WAV
    convert_to_wav(even_file, os.path.join(output_dir, "91_EVEN.wav"), 11025)
    convert_to_wav(odd_file, os.path.join(output_dir, "91_ODD.wav"), 11025)
    convert_to_wav(interleaved_file, os.path.join(output_dir, "91_MONO_22050Hz.wav"), 22050)

def parse_extraction_log(log_file):
    """
    Parse audio_extraction.log pour extraire:
    - Frame index
    - audioPos
    - bufferIndex (EVEN/ODD)
    - compSize
    - file offset (calculé à partir de record positions)
    """
    if not os.path.exists(log_file):
        return None
    
    frame_info = []
    record_positions = {}
    video_sizes = {}
    
    with open(log_file, 'r') as f:
        for line in f:
            # Parse record positions: "record[0]=45056 videoSize=2612 packetSize=4833"
            if line.startswith("record["):
                parts = line.split()
                idx = int(parts[0].split('[')[1].split(']')[0])
                pos = int(parts[0].split('=')[1])
                video_size = int(parts[1].split('=')[1])
                record_positions[idx] = pos
                video_sizes[idx] = video_size
            
            # Parse frame info: "Frame 0: audioPos=39844 bufferIndex=0 (EVEN) compSize=2213"
            elif line.startswith("Frame "):
                parts = line.split()
                frame_idx = int(parts[1].rstrip(':'))
                audioPos = int(parts[2].split('=')[1])
                bufferIndex = int(parts[3].split('=')[1])
                compSize = int(parts[5].split('=')[1])
                
                # Calculer l'offset dans le fichier
                # Position = record[frame_idx] + videoSize + 8 (header audio: 4 bytes pos + 4 bytes size)
                if frame_idx in record_positions:
                    file_offset = record_positions[frame_idx] + video_sizes[frame_idx] + 8
                    
                    frame_info.append({
                        'frame': frame_idx,
                        'audioPos': audioPos,
                        'bufferIndex': bufferIndex,
                        'compSize': compSize,
                        'fileOffset': file_offset
                    })
    
    return frame_info

def convert_to_wav(pcm_file, wav_file, sample_rate=22050):
    """Convertir PCM brut en WAV avec ffmpeg"""
    cmd = f'ffmpeg -y -f s16le -ar {sample_rate} -ac 1 -i "{pcm_file}" "{wav_file}" 2>/dev/null'
    result = os.system(cmd)
    if result == 0:
        print(f"  ✓ Converti: {os.path.basename(wav_file)} ({sample_rate}Hz)")

if __name__ == "__main__":
    rbt_file = "ScummVM/rbt/91.RBT"
    output_dir = "audio/lr_simple"
    
    if len(sys.argv) >= 2:
        rbt_file = sys.argv[1]
    if len(sys.argv) >= 3:
        output_dir = sys.argv[2]
    
    if not os.path.exists(rbt_file):
        print(f"ERROR: Fichier RBT introuvable: {rbt_file}")
        sys.exit(1)
    
    extract_lr_channels(rbt_file, output_dir)
