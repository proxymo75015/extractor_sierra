#!/usr/bin/env python3
"""
Robot Position Parser for ScummVM Debug Logs
Extracts Robot coordinates from Phantasmagoria debug output
"""

import re
import sys
from collections import defaultdict

def parse_scummvm_log(log_file):
    """
    Parse ScummVM debug log to extract Robot positions
    
    Expected log format:
    WARNING: ROBOT_DEBUG: Robot 1000 at position X=150 Y=143 (priority=200 scale=128)
    """
    
    robot_positions = defaultdict(list)
    pattern = r'Robot\s+(\d+)\s+at\s+position\s+X=(-?\d+)\s+Y=(-?\d+)'
    
    with open(log_file, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            match = re.search(pattern, line)
            if match:
                robot_id = int(match.group(1))
                x = int(match.group(2))
                y = int(match.group(3))
                robot_positions[robot_id].append((x, y))
    
    return robot_positions

def consolidate_positions(robot_positions):
    """
    Pour chaque Robot, retourne la position la plus fréquente
    (certains Robots peuvent apparaître plusieurs fois)
    """
    
    consolidated = {}
    
    for robot_id, positions in robot_positions.items():
        # Compter les occurrences
        from collections import Counter
        counter = Counter(positions)
        most_common = counter.most_common(1)[0][0]
        consolidated[robot_id] = most_common
    
    return consolidated

def write_robot_positions(positions, output_file):
    """
    Écrit le fichier robot_positions.txt
    Format: robot_id X Y
    """
    
    with open(output_file, 'w') as f:
        f.write("# Robot Positions for Phantasmagoria\n")
        f.write("# Extracted from ScummVM debug logs\n")
        f.write("# Format: robot_id X Y\n")
        f.write("# Game resolution: 630x450\n")
        f.write("#\n")
        
        for robot_id in sorted(positions.keys()):
            x, y = positions[robot_id]
            f.write(f"{robot_id} {x} {y}\n")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 parse_robot_logs.py <scummvm_log_file> [output_file]")
        print()
        print("Example:")
        print("  python3 parse_robot_logs.py scummvm_robot_logs.txt robot_positions.txt")
        sys.exit(1)
    
    log_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else "robot_positions.txt"
    
    print(f"Parsing ScummVM log: {log_file}")
    robot_positions = parse_scummvm_log(log_file)
    
    if not robot_positions:
        print("❌ No Robot positions found in log file!")
        print()
        print("Expected log format:")
        print("  WARNING: ROBOT_DEBUG: Robot 1000 at position X=150 Y=143 ...")
        print()
        print("Make sure:")
        print("  1. ScummVM was patched with debug warnings")
        print("  2. At least one Robot video was played")
        print("  3. Debug level was set (--debuglevel=1)")
        sys.exit(1)
    
    print(f"Found {len(robot_positions)} unique Robot IDs")
    
    # Afficher les positions trouvées
    for robot_id, positions in sorted(robot_positions.items()):
        print(f"  Robot {robot_id}: {len(positions)} occurrence(s)")
        if len(positions) > 1:
            print(f"    Positions: {positions}")
    
    print()
    print("Consolidating positions...")
    consolidated = consolidate_positions(robot_positions)
    
    print(f"Writing to: {output_file}")
    write_robot_positions(consolidated, output_file)
    
    print("✅ Done!")
    print()
    print("Robot Positions:")
    for robot_id in sorted(consolidated.keys()):
        x, y = consolidated[robot_id]
        print(f"  {robot_id:4d}: X={x:4d} Y={y:4d}")

if __name__ == "__main__":
    main()
