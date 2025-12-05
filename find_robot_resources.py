#!/usr/bin/env python3
"""
Cherche les ressources Robot (type 0x0C) dans les fichiers RESSCI
Les vidéos Robot sont stockées comme ressources, pas dans des scripts
"""

import struct
import os
import sys
import glob

def parse_resmap_all_types(resmap_path):
    """Parse le RESMAP pour TOUS les types de ressources"""
    resources = {}
    
    with open(resmap_path, 'rb') as f:
        data = f.read()
    
    # Structure SCI32 RESMAP : octets variables
    # Format général : type (1 octet), numéro (2 octets), offset (4-6 octets)
    
    i = 0
    while i < len(data) - 6:
        res_type = data[i]
        
        # Types SCI connus
        type_names = {
            0x00: "View",
            0x01: "Pic", 
            0x02: "Script",
            0x03: "Text",
            0x04: "Sound",
            0x05: "Memory",
            0x06: "Vocab",
            0x07: "Font",
            0x08: "Cursor",
            0x09: "Patch",
            0x0A: "Bitmap",
            0x0B: "Palette",
            0x0C: "Wave/Robot",  # Robot videos dans SCI32
            0x0D: "Audio",
            0x0E: "Sync",
            0x80: "Message"
        }
        
        # Vérifier si c'est un type valide
        if res_type in type_names or res_type == 0xFF:
            if res_type == 0xFF:
                # Marqueur de fin
                break
            
            # Lire le numéro de ressource (2 octets)
            if i + 2 < len(data):
                res_num = struct.unpack('<H', data[i+1:i+3])[0]
                
                if res_type not in resources:
                    resources[res_type] = []
                resources[res_type].append(res_num)
                
                i += 6  # Sauter cette entrée
            else:
                break
        else:
            i += 1
    
    return resources

def main():
    resource_dir = "/workspaces/extractor_sierra/Resource"
    
    print("🔍 Analyse des ressources dans les fichiers RESMAP")
    print("=" * 60)
    print()
    
    resmap_files = sorted(glob.glob(os.path.join(resource_dir, "RESMAP.*")))
    
    all_robots = set()
    
    for resmap in resmap_files:
        cd_num = os.path.splitext(resmap)[1][1:]
        
        print(f"📀 CD{cd_num}:")
        resources = parse_resmap_all_types(resmap)
        
        for res_type, numbers in sorted(resources.items()):
            type_names = {
                0x00: "View",
                0x01: "Pic", 
                0x02: "Script",
                0x03: "Text",
                0x04: "Sound",
                0x0C: "Robot/Wave",
                0x80: "Message"
            }
            
            type_name = type_names.get(res_type, f"Type{res_type:02X}")
            
            # Afficher seulement les types importants
            if res_type in [0x0C, 0x02]:  # Robot/Wave et Scripts
                nums = sorted(set(numbers))
                print(f"   {type_name:12s}: {len(nums):3d} ressources - {nums[:15]}")
                if len(nums) > 15:
                    print(f"                      ... et {len(nums)-15} autres")
                
                if res_type == 0x0C:
                    all_robots.update(nums)
        
        print()
    
    print("=" * 60)
    if all_robots:
        robot_list = sorted(all_robots)
        print(f"📊 Total: {len(robot_list)} ressources Robot/Wave trouvées")
        print(f"Numéros: {robot_list}")
        print()
        
        # Vérifier nos fichiers
        our_rbts = [91, 161, 230, 1000, 1014, 1180]
        print("Correspondance avec nos fichiers RBT:")
        for rbt in our_rbts:
            if rbt in all_robots:
                print(f"   ✅ {rbt}.RBT trouvé dans les ressources !")
            else:
                print(f"   ❌ {rbt}.RBT NON trouvé")
    else:
        print("❌ Aucune ressource Robot trouvée")
        print("   Le type 0x0C pourrait ne pas être Robot dans cette version")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
