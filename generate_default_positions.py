#!/usr/bin/env python3
"""
Generate Default Robot Positions for Phantasmagoria
Uses centered positioning heuristic (most common in SCI games)
"""

import sys
from pathlib import Path

# Phantasmagoria game resolution
GAME_WIDTH = 630
GAME_HEIGHT = 450

# Common Robot video dimensions (from RBT analysis)
# Most Phantasmagoria Robots are around 320-340 pixels wide
COMMON_ROBOT_WIDTH = 330
COMMON_ROBOT_HEIGHT = 242

def calculate_centered_position():
    """
    Calculate centered position for a Robot video
    This is the most common positioning in SCI games
    """
    x = (GAME_WIDTH - COMMON_ROBOT_WIDTH) // 2
    y = (GAME_HEIGHT - COMMON_ROBOT_HEIGHT) // 3  # Upper third for drama
    
    return x, y

def get_robot_ids_from_directory(rbt_dir):
    """Extract all Robot IDs from RBT files"""
    robot_ids = []
    
    rbt_path = Path(rbt_dir)
    if not rbt_path.exists():
        return []
    
    for rbt_file in sorted(rbt_path.glob("*.RBT")):
        try:
            robot_id = int(rbt_file.stem)
            robot_ids.append(robot_id)
        except ValueError:
            continue
    
    return sorted(robot_ids)

def generate_positions_file(robot_ids, output_file):
    """Generate robot_positions.txt with centered positions"""
    
    x, y = calculate_centered_position()
    
    with open(output_file, 'w') as f:
        f.write("# Robot Positions for Phantasmagoria\n")
        f.write("# Auto-generated with centered positioning heuristic\n")
        f.write("# Format: robot_id X Y\n")
        f.write(f"# Game resolution: {GAME_WIDTH}x{GAME_HEIGHT}\n")
        f.write(f"# Assumed Robot size: {COMMON_ROBOT_WIDTH}x{COMMON_ROBOT_HEIGHT}\n")
        f.write("#\n")
        f.write(f"# Centered position: X={x} Y={y}\n")
        f.write("#\n")
        f.write("# NOTE: These are ESTIMATED values based on common SCI conventions.\n")
        f.write("# For exact positions, use extract_robot_positions.sh with ScummVM.\n")
        f.write("#\n\n")
        
        for robot_id in robot_ids:
            f.write(f"{robot_id} {x} {y}\n")
    
    print(f"✅ Generated {output_file} with {len(robot_ids)} Robot positions")
    print(f"   Using centered position: X={x} Y={y}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 generate_default_positions.py <RBT_directory> [output_file]")
        print()
        print("Examples:")
        print("  python3 generate_default_positions.py RBT/")
        print("  python3 generate_default_positions.py RBT/ robot_positions.txt")
        print()
        print("This generates default centered positions for all Robot videos.")
        sys.exit(1)
    
    rbt_dir = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else "robot_positions.txt"
    
    print("=" * 70)
    print("Default Robot Position Generator")
    print("=" * 70)
    print()
    print(f"Game resolution: {GAME_WIDTH}x{GAME_HEIGHT}")
    print(f"Assumed Robot size: {COMMON_ROBOT_WIDTH}x{COMMON_ROBOT_HEIGHT}")
    print()
    
    # Get all Robot IDs
    robot_ids = get_robot_ids_from_directory(rbt_dir)
    
    if not robot_ids:
        print(f"❌ No RBT files found in {rbt_dir}")
        sys.exit(1)
    
    print(f"Found {len(robot_ids)} Robot files")
    print()
    
    # Calculate centered position
    x, y = calculate_centered_position()
    print(f"Centered position: X={x} Y={y}")
    print()
    
    # Generate file
    generate_positions_file(robot_ids, output_file)
    print()
    print("=" * 70)
    print("IMPORTANT:")
    print("=" * 70)
    print("These are ESTIMATED positions based on common SCI conventions.")
    print("They may not be pixel-perfect compared to ScummVM.")
    print()
    print("For exact positions, use one of these methods:")
    print("  1. Run: ./extract_robot_positions.sh (ScummVM patched)")
    print("  2. Manual extraction from ScummVM screenshots")
    print()
    print("To test these positions:")
    print("  1. Extract a Robot video with your tool")
    print("  2. Compare visually with ScummVM")
    print("  3. Adjust values in robot_positions.txt if needed")
    print("=" * 70)

if __name__ == "__main__":
    main()
