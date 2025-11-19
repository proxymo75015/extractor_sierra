#!/usr/bin/env python3
"""
Extract LEFT and RIGHT audio channels from 91.RBT using correct audioPos classification.

Pattern discovered from audioPos values:
- EVEN (LEFT): audioPos % 4 == 0 → bufferIndex=0
- ODD (RIGHT): audioPos % 4 != 0 → bufferIndex=1

Frames follow a pattern: 1 EVEN, 3 ODD, 1 EVEN, 3 ODD, etc.
- EVEN frames: 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88 (23 frames)
- ODD frames: All others (67 frames)
"""
import struct
import os
import sys

# Map frame index to channel based on audioPos pattern from extraction log
# Pattern: frame 0=EVEN, 1-3=ODD, 4=EVEN, 5-7=ODD, etc.
def is_even_frame(frame_idx):
    """Returns True if frame contains EVEN (LEFT) channel data"""
    return (frame_idx % 4) == 0

def extract_channels(dumps_dir, output_dir):
    """Extract L and R channels from frame dumps"""
    os.makedirs(output_dir, exist_ok=True)
    
    left_samples = []
    right_samples = []
    
    frame_count = 90
    for i in range(frame_count):
        decomp_file = os.path.join(dumps_dir, f"frame_{i}", "audio_decomp.pcm")
        
        if not os.path.exists(decomp_file):
            print(f"Warning: Missing {decomp_file}")
            continue
        
        # Read decompressed PCM (2205 samples = 4410 bytes)
        with open(decomp_file, 'rb') as f:
            pcm_data = f.read()
        
        # Parse as int16 samples
        sample_count = len(pcm_data) // 2
        samples = struct.unpack(f'<{sample_count}h', pcm_data)
        
        # Classify frame
        if is_even_frame(i):
            left_samples.extend(samples)
            print(f"Frame {i}: EVEN (LEFT) - {len(samples)} samples")
        else:
            right_samples.extend(samples)
            print(f"Frame {i}: ODD (RIGHT) - {len(samples)} samples")
    
    # Write separate channel files
    left_file = os.path.join(output_dir, "91_LEFT.pcm")
    right_file = os.path.join(output_dir, "91_RIGHT.pcm")
    
    with open(left_file, 'wb') as f:
        f.write(struct.pack(f'<{len(left_samples)}h', *left_samples))
    
    with open(right_file, 'wb') as f:
        f.write(struct.pack(f'<{len(right_samples)}h', *right_samples))
    
    print(f"\nExtraction complete:")
    print(f"  LEFT:  {len(left_samples)} samples ({len(left_samples)/22050:.2f}s) → {left_file}")
    print(f"  RIGHT: {len(right_samples)} samples ({len(right_samples)/22050:.2f}s) → {right_file}")
    
    # Convert to WAV for easy listening
    convert_to_wav(left_file, os.path.join(output_dir, "91_LEFT.wav"))
    convert_to_wav(right_file, os.path.join(output_dir, "91_RIGHT.wav"))

def convert_to_wav(pcm_file, wav_file):
    """Convert raw PCM to WAV using ffmpeg"""
    cmd = f'ffmpeg -y -f s16le -ar 22050 -ac 1 -i "{pcm_file}" "{wav_file}"'
    os.system(cmd)
    print(f"  Converted {os.path.basename(pcm_file)} → {os.path.basename(wav_file)}")

if __name__ == "__main__":
    dumps_dir = "build/rbt_dumps/91"
    output_dir = "audio/lr_channels"
    
    if len(sys.argv) >= 2:
        dumps_dir = sys.argv[1]
    if len(sys.argv) >= 3:
        output_dir = sys.argv[2]
    
    extract_channels(dumps_dir, output_dir)
