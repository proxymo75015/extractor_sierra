#!/usr/bin/env python3
import sys, os, wave, subprocess
from glob import glob
import csv

def usage():
    print("Usage: make_scummvm_video.py <frames_dir> <audio_wav> <frame_rate> <out_mp4>")
    sys.exit(2)

if len(sys.argv) < 5:
    usage()

frames_dir = sys.argv[1]
audio_wav = sys.argv[2]
frame_rate = float(sys.argv[3])
out_file = sys.argv[4]

# list frames
frames = sorted([f for f in glob(os.path.join(frames_dir, "*.pgm"))])
if not frames:
    print("No frames found in", frames_dir); sys.exit(1)

# audio duration
with wave.open(audio_wav, 'rb') as w:
    audio_duration = w.getnframes() / w.getframerate()

per_frame = 1.0 / frame_rate

timeline = None
timeline_file = os.path.abspath(os.path.join(frames_dir, '..', 'timeline.csv'))
if os.path.exists(timeline_file):
    timeline = {}
    with open(timeline_file, newline='') as csvfile:
        reader = csv.DictReader(csvfile)
        for row in reader:
            try:
                idx = int(row['frame'])
                t = float(row['audio_time_seconds'])
                timeline[idx] = t
            except Exception:
                pass
    print('Loaded timeline from', timeline_file)

# prepare concat list
concat = out_file + ".concat.txt"
with open(concat, 'w') as f:
    # ScummVM-style dynamic framerate with setRobotTime repositioning
    normal = frame_rate
    min_rate = max(1.0, normal - 1.0)
    max_rate = normal + 1.0
    
    current_video_frame = 0  # current video frame position (ScummVM _currentFrameNo)
    current_rate = normal
    check_interval = 0.333  # check sync every ~1/3 second
    last_check_time = 0.0
    video_time = 0.0
    
    concat_entries = []
    
    while current_video_frame < len(frames):
        frame_idx = int(current_video_frame)
        
        # Periodic sync check (ScummVM checks every ~20 frames at 60fps ticks)
        if timeline and frame_idx in timeline and video_time >= last_check_time + check_interval:
            last_check_time = video_time
            
            # Calculate audio frame position from timeline
            audio_time = timeline[frame_idx]
            audio_frame = int(round(audio_time * normal))
            
            # ScummVM sync logic
            should_reset = False
            new_rate = current_rate
            
            if audio_frame < frame_idx - 1 and current_rate != min_rate:
                # Video ahead of audio: slow down
                new_rate = min_rate
                should_reset = True
            elif audio_frame > frame_idx + 1 and current_rate != max_rate:
                # Video behind audio: speed up
                new_rate = max_rate
                should_reset = True
            elif abs(audio_frame - frame_idx) <= 1 and current_rate != normal:
                # Back in sync: return to normal
                new_rate = normal
                should_reset = True
            
            # setRobotTime repositioning
            if should_reset:
                if audio_frame < frame_idx:
                    # Audio behind: stay at current frame
                    current_video_frame = float(frame_idx)
                else:
                    # Audio ahead: jump to audio position
                    current_video_frame = float(min(audio_frame, len(frames) - 1))
                current_rate = new_rate
                frame_idx = int(current_video_frame)
        
        # Calculate frame duration
        dur = 1.0 / current_rate
        
        # Extend last frame to audio end
        if frame_idx >= len(frames) - 1:
            frame_idx = len(frames) - 1
            dur = max(dur, audio_duration - video_time)
            concat_entries.append((frames[frame_idx], dur))
            break
        
        concat_entries.append((frames[frame_idx], dur))
        
        # Advance video position
        video_time += dur
        current_video_frame += 1.0
    
    # Write concat entries
    for frm, dur in concat_entries:
        f.write(f"file '{os.path.abspath(frm)}'\n")
        f.write(f"duration {dur:.6f}\n")
    
    # Repeat last frame to ensure duration
    f.write(f"file '{os.path.abspath(frames[-1])}'\n")

# ffmpeg concat to video
cmd = [
    'ffmpeg', '-y', '-f', 'concat', '-safe', '0', '-i', concat,
    '-framerate', str(frame_rate), '-c:v', 'libx264', '-pix_fmt', 'yuv420p',
    '-an', out_file
]
print('Running:', ' '.join(cmd))
subprocess.check_call(cmd)

# attach audio (shortest: false -> allow audio longer than video); we'll allow audio to be longer by putting -shortest or not.
final = out_file.replace('.mp4', '_with_audio.mp4')
cmd2 = [
    'ffmpeg', '-y', '-i', out_file, '-i', audio_wav,
    '-c:v', 'copy', '-c:a', 'aac', '-b:a', '128k', '-shortest', final
]
print('Attaching audio:', ' '.join(cmd2))
subprocess.check_call(cmd2)

print('Done:', final)
