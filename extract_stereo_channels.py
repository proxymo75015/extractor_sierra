#!/usr/bin/env python3
"""
Extraction des canaux LEFT (EVEN) et RIGHT (ODD) depuis le fichier .RBT.
Analyse les positions audio réelles pour déterminer le canal correct.
"""

import os
import sys
import struct

RBT_FILE = "ScummVM/rbt/91.RBT"
OUTPUT_DIR = "output_91_stereo"
SAMPLE_RATE = 22050

def read_rbt_audio_packets(filename):
    """Lit les packets audio avec leurs positions pour déterminer EVEN/ODD."""
    
    packets = []
    
    with open(filename, 'rb') as f:
        # Lire l'en-tête principal (après SOL)
        f.seek(6)  # Skip "SOL" signature
        version = struct.unpack('<H', f.read(2))[0]
        num_frames = struct.unpack('<H', f.read(2))[0]
        
        print(f"📁 Fichier: {filename}")
        print(f"   Version: {version}")
        print(f"   Frames: {num_frames}\n")
        
        # Parser chaque frame
        for frame_num in range(num_frames):
            # Lire frame header
            frame_start = f.tell()
            frame_header = f.read(8)
            
            if len(frame_header) < 8:
                break
            
            frame_size = struct.unpack('<I', frame_header[0:4])[0]
            
            # Chercher le chunk audio dans la frame
            bytes_read = 0
            max_bytes = frame_size - 8
            
            while bytes_read < max_bytes:
                chunk_pos = f.tell()
                chunk_header = f.read(8)
                
                if len(chunk_header) < 8:
                    break
                
                chunk_type = struct.unpack('<H', chunk_header[0:2])[0]
                chunk_size = struct.unpack('<I', chunk_header[2:6])[0]
                
                if chunk_type == 2:  # Audio chunk
                    # Lire position et size
                    audio_meta = f.read(6)
                    audio_position = struct.unpack('<I', audio_meta[0:4])[0]
                    audio_size = struct.unpack('<H', audio_meta[4:6])[0]
                    
                    # Lire les données compressées
                    audio_data = f.read(chunk_size - 6)
                    
                    # Déterminer le canal: position % 4 == 0 → EVEN, sinon ODD
                    is_even = (audio_position % 4 == 0)
                    
                    packets.append({
                        'frame': frame_num,
                        'position': audio_position,
                        'size': audio_size,
                        'data': audio_data,
                        'is_even': is_even
                    })
                    
                    bytes_read += 8 + chunk_size
                else:
                    # Skip ce chunk
                    f.seek(chunk_size, 1)
                    bytes_read += 8 + chunk_size
            
            # Aller à la prochaine frame
            f.seek(frame_start + frame_size, 0)
    
    return packets

def decompress_dpcm16(data):
    """Décompresse les données DPCM16."""
    
    # Table DPCM16 de ScummVM
    table = [
        0x0000, 0x0008, 0x0010, 0x0020, 0x0030, 0x0040, 0x0050, 0x0060,
        0x0070, 0x0080, 0x0090, 0x00A0, 0x00B0, 0x00C0, 0x00D0, 0x00E0,
        0x00F0, 0x0100, 0x0110, 0x0120, 0x0130, 0x0140, 0x0150, 0x0160,
        0x0170, 0x0180, 0x0190, 0x01A0, 0x01B0, 0x01C0, 0x01D0, 0x01E0,
        0x01F0, 0x0200, 0x0210, 0x0220, 0x0230, 0x0240, 0x0250, 0x0260,
        0x0270, 0x0280, 0x0290, 0x02A0, 0x02B0, 0x02C0, 0x02D0, 0x02E0,
        0x02F0, 0x0300, 0x0310, 0x0320, 0x0330, 0x0340, 0x0350, 0x0360,
        0x0370, 0x0380, 0x0390, 0x03A0, 0x03B0, 0x03C0, 0x03D0, 0x03E0,
        0x03F0, 0x0400, 0x0410, 0x0420, 0x0430, 0x0440, 0x0450, 0x0460,
        0x0470, 0x0480, 0x0490, 0x04A0, 0x04B0, 0x04C0, 0x04D0, 0x04E0,
        0x04F0, 0x0500, 0x0510, 0x0520, 0x0530, 0x0540, 0x0550, 0x0560,
        0x0570, 0x0580, 0x0590, 0x05A0, 0x05B0, 0x05C0, 0x05D0, 0x05E0,
        0x05F0, 0x0600, 0x0610, 0x0620, 0x0630, 0x0640, 0x0650, 0x0660,
        0x0670, 0x0680, 0x0690, 0x06A0, 0x06B0, 0x06C0, 0x06D0, 0x06E0,
        0x06F0, 0x0700, 0x0710, 0x0720, 0x0730, 0x0740, 0x0750, 0x0760,
        0x0770, 0x0780, 0x0790, 0x07A0, 0x07B0, 0x07C0, 0x07D0, 0x07E0
    ]
    
    output = []
    sample = 0  # Prédicteur initialisé à 0 pour chaque packet
    
    for byte in data:
        delta = byte
        
        if delta & 0x80:
            next_sample = sample - table[delta & 0x7F]
        else:
            next_sample = sample + table[delta]
        
        # Clamping
        if next_sample > 32767:
            next_sample = 32767
        elif next_sample < -32768:
            next_sample = -32768
        
        sample = next_sample
        output.append(sample)
    
    return output

def extract_channels():
    """Extrait les canaux LEFT et RIGHT."""
    
    print("╔" + "═" * 78 + "╗")
    print("║" + " " * 20 + "EXTRACTION DES CANAUX STÉRÉO" + " " * 29 + "║")
    print("╚" + "═" * 78 + "╝\n")
    
    if not os.path.exists(RBT_FILE):
        print(f"❌ Erreur: {RBT_FILE} non trouvé")
        return False
    
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    
    # Lire tous les packets
    packets = read_rbt_audio_packets(RBT_FILE)
    
    if not packets:
        print("❌ Aucun packet audio trouvé")
        return False
    
    print(f"📦 Packets audio trouvés: {len(packets)}\n")
    
    # Séparer EVEN et ODD
    even_packets = [p for p in packets if p['is_even']]
    odd_packets = [p for p in packets if not p['is_even']]
    
    print(f"   • Canal LEFT (EVEN): {len(even_packets)} packets")
    print(f"   • Canal RIGHT (ODD): {len(odd_packets)} packets\n")
    
    # Décompresser et écrire les canaux
    print("Décompression et extraction...\n")
    
    # Canal LEFT (EVEN)
    left_samples = []
    for p in even_packets:
        samples = decompress_dpcm16(p['data'])
        left_samples.extend(samples)
    
    # Canal RIGHT (ODD)
    right_samples = []
    for p in odd_packets:
        samples = decompress_dpcm16(p['data'])
        right_samples.extend(samples)
    
    # Écrire les fichiers PCM
    left_file = os.path.join(OUTPUT_DIR, "audio_left.pcm")
    right_file = os.path.join(OUTPUT_DIR, "audio_right.pcm")
    
    with open(left_file, 'wb') as f:
        for sample in left_samples:
            f.write(struct.pack('<h', sample))
    
    with open(right_file, 'wb') as f:
        for sample in right_samples:
            f.write(struct.pack('<h', sample))
    
    # Statistiques
    left_duration = len(left_samples) / SAMPLE_RATE
    right_duration = len(right_samples) / SAMPLE_RATE
    
    print(f"✅ Extraction terminée!\n")
    
    print(f"📊 Canal LEFT (EVEN):")
    print(f"   • Fichier: {left_file}")
    print(f"   • Packets: {len(even_packets)}")
    print(f"   • Samples: {len(left_samples):,}")
    print(f"   • Durée: {left_duration:.3f} sec")
    print(f"   • Taille: {len(left_samples) * 2 / 1024:.1f} KB\n")
    
    print(f"📊 Canal RIGHT (ODD):")
    print(f"   • Fichier: {right_file}")
    print(f"   • Packets: {len(odd_packets)}")
    print(f"   • Samples: {len(right_samples):,}")
    print(f"   • Durée: {right_duration:.3f} sec")
    print(f"   • Taille: {len(right_samples) * 2 / 1024:.1f} KB\n")
    
    print(f"💡 Pour écouter:")
    print(f"   ffplay -f s16le -ar {SAMPLE_RATE} -ac 1 {left_file}")
    print(f"   ffplay -f s16le -ar {SAMPLE_RATE} -ac 1 {right_file}\n")
    
    print(f"💡 Pour convertir en WAV:")
    print(f"   ffmpeg -f s16le -ar {SAMPLE_RATE} -ac 1 -i {left_file} {OUTPUT_DIR}/audio_left.wav")
    print(f"   ffmpeg -f s16le -ar {SAMPLE_RATE} -ac 1 -i {right_file} {OUTPUT_DIR}/audio_right.wav\n")
    
    return True

if __name__ == "__main__":
    success = extract_channels()
    sys.exit(0 if success else 1)
