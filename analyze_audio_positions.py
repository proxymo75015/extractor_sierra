#!/usr/bin/env python3
"""
Analyse les positions audio de chaque frame pour déterminer EVEN/ODD.
"""

import os
import sys

DUMPS_DIR = "build/rbt_dumps/91"

def analyze_positions():
    """Analyse les positions audio."""
    
    print("╔" + "═" * 78 + "╗")
    print("║" + " " * 22 + "ANALYSE DES POSITIONS AUDIO" + " " * 29 + "║")
    print("╚" + "═" * 78 + "╝\n")
    
    # Lire le fichier de timing si disponible
    timing_file = os.path.join(DUMPS_DIR, "../timing.csv")
    
    if not os.path.exists(timing_file):
        print(f"❌ Fichier timing.csv non trouvé: {timing_file}")
        print("   Essayons d'extraire à nouveau...")
        return False
    
    print(f"📁 Lecture de: {timing_file}\n")
    
    with open(timing_file, 'r') as f:
        lines = f.readlines()
    
    # Header
    print(f"{'Frame':>5} | {'Position':>10} | {'Size':>6} | {'Time':>8} | {'Canal':^6} | {'% 4':>5}")
    print("─" * 80)
    
    even_count = 0
    odd_count = 0
    
    for line in lines:
        parts = line.strip().split(',')
        if len(parts) >= 3:
            try:
                frame = int(parts[0])
                position = int(parts[1])
                size = int(parts[2])
                time = float(parts[3]) if len(parts) > 3 else 0.0
                
                # Règle: position % 4 == 0 → EVEN (0), sinon ODD (1)
                is_even = (position % 4 == 0)
                mod4 = position % 4
                
                if is_even:
                    canal = "EVEN"
                    even_count += 1
                else:
                    canal = "ODD"
                    odd_count += 1
                
                if frame < 20 or frame % 10 == 0:
                    print(f"{frame:>5} | {position:>10} | {size:>6} | {time:>8.3f} | {canal:^6} | {mod4:>5}")
                    
            except (ValueError, IndexError):
                continue
    
    print("─" * 80)
    print(f"\n📊 Statistiques:")
    print(f"   • Packets EVEN (LEFT):  {even_count}")
    print(f"   • Packets ODD (RIGHT):  {odd_count}")
    print(f"   • Total:                {even_count + odd_count}\n")
    
    return True

if __name__ == "__main__":
    success = analyze_positions()
    sys.exit(0 if success else 1)
