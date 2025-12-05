#!/usr/bin/env python3
"""
Liste tous les fichiers Robot (.RBT) référencés dans les scripts SCI
"""

import struct
import os
import sys
import glob

def parse_resmap(resmap_path):
    """Parse le fichier RESMAP pour trouver les offsets des scripts"""
    scripts = []
    
    with open(resmap_path, 'rb') as f:
        data = f.read()
    
    pos = 0
    while pos < len(data) - 10:
        if pos + 10 > len(data):
            break
            
        res_type = data[pos]
        
        if res_type == 0x04:  # Script
            number = struct.unpack('<H', data[pos+1:pos+3])[0]
            offset = struct.unpack('<I', data[pos+3:pos+7])[0] & 0xFFFFFFFF
            scripts.append((number, offset))
            
        pos += 1
    
    return scripts

def find_robot_numbers(ressci_path, offset, size=100000):
    """Cherche TOUS les numéros de Robot dans un script"""
    with open(ressci_path, 'rb') as f:
        f.seek(offset)
        data = f.read(size)
    
    robots = set()
    
    # Chercher 0x46 (CALLK en SCI32) suivi de 0x3A (Robot kernel)
    for i in range(len(data) - 20):
        if data[i] == 0x46 and data[i+1] == 0x3A:
            # Robot kernel call trouvé, chercher les numéros autour
            # Les numéros Robot sont typiquement en little-endian sur 2 octets
            context = data[max(0, i-30):min(len(data), i+40)]
            
            # Chercher les patterns PUSHI (0x38) suivi d'un nombre
            for j in range(len(context)-2):
                if context[j] == 0x38:  # PUSHI
                    robot_num = struct.unpack('<H', context[j+1:j+3])[0]
                    # Les numéros Robot sont généralement entre 0 et 10000
                    if 0 < robot_num < 10000:
                        robots.add(robot_num)
    
    return robots

def main():
    resource_dir = "/workspaces/extractor_sierra/Resource"
    
    print("🔍 Liste de tous les Robots référencés dans les scripts SCI")
    print("=" * 60)
    print()
    
    # Trouver tous les fichiers
    resmap_files = sorted(glob.glob(os.path.join(resource_dir, "RESMAP.*")))
    
    all_robots = set()
    
    for resmap in resmap_files:
        cd_num = os.path.splitext(resmap)[1][1:]
        ressci = os.path.join(resource_dir, f"RESSCI.{cd_num}")
        
        if not os.path.exists(ressci):
            continue
        
        print(f"📀 CD{cd_num}:")
        scripts = parse_resmap(resmap)
        
        cd_robots = set()
        for script_num, offset in scripts:
            robots = find_robot_numbers(ressci, offset)
            cd_robots.update(robots)
        
        if cd_robots:
            robot_list = sorted(cd_robots)
            print(f"   Robots trouvés: {len(robot_list)}")
            print(f"   Numéros: {', '.join(map(str, robot_list[:20]))}")
            if len(robot_list) > 20:
                print(f"   ... et {len(robot_list)-20} autres")
            all_robots.update(cd_robots)
        else:
            print(f"   ❌ Aucun Robot trouvé")
        print()
    
    print("=" * 60)
    print(f"📊 Total: {len(all_robots)} Robots différents trouvés")
    
    # Vérifier si 1000 est dans la liste
    if 1000 in all_robots:
        print(f"✅ Robot 1000.RBT est référencé dans les scripts !")
    else:
        print(f"❌ Robot 1000.RBT N'EST PAS dans le CD1")
        print(f"   Vous devez copier les fichiers d'un autre CD")
    
    # Chercher 1180 aussi
    if 1180 in all_robots:
        print(f"✅ Robot 1180.RBT est référencé dans les scripts !")
    else:
        print(f"❌ Robot 1180.RBT n'est pas dans le CD1")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
