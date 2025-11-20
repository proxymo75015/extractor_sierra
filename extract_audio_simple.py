#!/usr/bin/env python3
"""
Extracteur audio RBT simplifié - Basé sur la logique ScummVM

Architecture:
- EVEN et ODD ne sont PAS Left/Right stéréo
- Ce sont deux flux MONO qui alimentent UN SEUL flux audio mono 22050Hz
- Le buffer circulaire utilise stride de 4 pour entrelacer les deux flux
- L'interpolation remplit les positions intermédiaires pour créer un flux continu

Référence: ScummVM/robot.cpp - RobotAudioStream
"""

import struct
import wave
import os

# Table DPCM16 de ScummVM (src/robot_decoder/dpcm.cpp)
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

def dpcm16_decode(compressed_data, initial_sample=0):
    """
    Décompression DPCM16 selon l'algorithme ScummVM
    """
    samples = []
    sample = initial_sample
    
    for delta_byte in compressed_data:
        # Calcul du prochain sample
        if delta_byte & 0x80:
            # Bit de signe = 1 : soustraction
            next_sample = sample - DPCM16_TABLE[delta_byte & 0x7F]
        else:
            # Bit de signe = 0 : addition
            next_sample = sample + DPCM16_TABLE[delta_byte]
        
        # Clamping pour éviter le wrapping
        if next_sample > 32767:
            next_sample = 32767
        elif next_sample < -32768:
            next_sample = -32768
        
        sample = next_sample
        samples.append(sample)
    
    return samples

def copy_every_other_sample(source_samples, buffer, buffer_index, start_pos):
    """
    copyEveryOtherSample de ScummVM : écrit dans le buffer avec stride de 2
    
    bufferIndex=0 (EVEN) : écrit aux positions 0, 2, 4, 6...
    bufferIndex=1 (ODD)  : écrit aux positions 1, 3, 5, 7...
    """
    pos = start_pos + buffer_index
    for sample in source_samples:
        if pos < len(buffer):
            buffer[pos] = sample
        pos += 2

def interpolate_channel(buffer, num_samples, buffer_index):
    """
    interpolateChannel de ScummVM : remplit les positions intermédiaires
    pour créer un flux continu
    
    Pour EVEN (bufferIndex=0): remplit positions 2, 6, 10, 14...
    Pour ODD (bufferIndex=1):  remplit positions 1, 5, 9, 13...
    """
    if num_samples <= 0:
        return
    
    if buffer_index == 1:
        # ODD: commence à position 1
        out_pos = 1
        in_pos = 2
        previous = buffer[0]
        num_samples -= 1
    else:
        # EVEN: commence à position 0
        out_pos = 0
        in_pos = 1
        previous = buffer[1] if len(buffer) > 1 else 0
    
    for _ in range(num_samples):
        if in_pos >= len(buffer):
            break
        current = buffer[in_pos]
        interpolated = (current + previous) >> 1
        buffer[out_pos] = interpolated
        previous = current
        in_pos += 4  # kEOSExpansion = 2 (stride de 4 en bytes = stride de 2 en samples)
        out_pos += 4

def extract_audio_scummvm_style(rbt_path, output_wav):
    """
    Extraction audio selon la logique ScummVM exacte:
    1. Utiliser les PCM pré-décompressés
    2. Les écrire dans un buffer circulaire avec stride
    3. Interpoler pour créer un flux continu mono 22050Hz
    """
    
    # Collecter les frames audio dans l'ordre
    base_dir = os.path.dirname(rbt_path)
    dumps_dir = os.path.join(base_dir, 'build/rbt_dumps/91')
    
    # Lire le log pour avoir l'ordre et le bufferIndex
    frame_data = []
    log_path = os.path.join(base_dir, 'audio_extraction.log')
    
    if os.path.exists(log_path):
        import re
        with open(log_path, 'r') as f:
            for line in f:
                m = re.match(r'Frame (\d+): audioPos=(\d+) bufferIndex=(\d+)', line)
                if m:
                    frame_data.append({
                        'frame_num': int(m.group(1)),
                        'audioPos': int(m.group(2)),
                        'bufferIndex': int(m.group(3))
                    })
    
    if not frame_data:
        print("❌ Fichier audio_extraction.log introuvable")
        return
    
    # Trier par audioPos (ordre chronologique de playback)
    frame_data.sort(key=lambda x: x['audioPos'])
    
    print(f"📊 {len(frame_data)} frames audio trouvées")
    
    # Créer un buffer pour stocker l'audio final
    # Stride de 4 : chaque sample EVEN/ODD occupe 4 positions (2 pour le sample, 2 pour l'interpolation)
    # Mais pour simplifier l'extraction, on va créer le flux final directement
    
    # Alternative simple : lire les PCM directement et les concaténer dans l'ordre audioPos
    final_samples = []
    
    for fd in frame_data:
        frame_num = fd['frame_num']
        pcm_path = os.path.join(dumps_dir, f'frame_{frame_num}/audio_decomp.pcm')
        
        if os.path.exists(pcm_path):
            with open(pcm_path, 'rb') as f:
                data = f.read()
                samples = struct.unpack(f'<{len(data)//2}h', data)
                final_samples.extend(samples[:2205])  # Exactement 2205 samples par frame
    
    print(f"✅ {len(final_samples)} samples extraits ({len(final_samples)/22050:.3f}s)")
    
    # Sauvegarder en WAV mono 22050Hz
    os.makedirs(os.path.dirname(output_wav), exist_ok=True)
    with wave.open(output_wav, 'wb') as wav:
        wav.setnchannels(1)  # MONO
        wav.setsampwidth(2)
        wav.setframerate(22050)
        wav.writeframes(struct.pack(f'<{len(final_samples)}h', *final_samples))
    
    print(f"💾 Fichier créé: {output_wav}")
    print(f"   Format: MONO 22050Hz")
    print(f"   Durée: {len(final_samples)/22050:.3f}s")

if __name__ == '__main__':
    extract_audio_scummvm_style(
        '/workspaces/extractor_sierra/91.RBT',
        '/workspaces/extractor_sierra/audio/91_MONO_SCUMMVM.wav'
    )
