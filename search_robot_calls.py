#!/usr/bin/env python3
"""
Script pour chercher les appels Robot dans les fichiers SCI de Phantasmagoria
"""

import struct
import os
import sys

def parse_resmap(resmap_path):
    """Parse le fichier RESMAP pour trouver les offsets des scripts"""
    scripts = []
    
    with open(resmap_path, 'rb') as f:
        data = f.read()
    
    # Format RESMAP SCI32: entrées de 10 octets
    # Les scripts ont le type 0x04
    pos = 0
    while pos < len(data) - 10:
        # Essayer de parser une entrée
        if pos + 10 > len(data):
            break
            
        # Type de ressource (script = 0x04)
        res_type = data[pos]
        
        if res_type == 0x04:  # Script
            number = struct.unpack('<H', data[pos+1:pos+3])[0]
            offset = struct.unpack('<I', data[pos+3:pos+7])[0] & 0xFFFFFFFF
            scripts.append((number, offset))
            
        pos += 1
    
    return scripts

def search_robot_in_script(ressci_path, offset, size=100000):
    """Cherche les références à Robot dans un script à l'offset donné"""
    with open(ressci_path, 'rb') as f:
        f.seek(offset)
        data = f.read(size)
    
    # Chercher le pattern Robot 1000 (0x03E8 en little-endian)
    # Dans SCI, les kernel calls sont CALLK suivi de l'index kernel
    # Robot est généralement kernel 0x3A (58 decimal)
    
    results = []
    
    # Chercher 0x3A (Robot kernel call)
    for i in range(len(data) - 10):
        if data[i] == 0x46:  # CALLK opcode en SCI32
            kernel_id = data[i+1]
            if kernel_id == 0x3A:  # Robot kernel
                # Chercher les paramètres autour
                context = data[max(0, i-20):min(len(data), i+30)]
                
                # Chercher 1000 (0xE8 0x03 en little-endian) dans le contexte
                for j in range(len(context)-1):
                    if context[j] == 0xE8 and context[j+1] == 0x03:
                        results.append((i, context.hex()))
                        break
    
    return results

def main():
    resource_dir = "/workspaces/extractor_sierra/Resource"
    
    print("🔍 Recherche des appels Robot dans les scripts SCI...")
    print()
    
    # Trouver tous les fichiers RESMAP et RESSCI
    import glob
    resmap_files = sorted(glob.glob(os.path.join(resource_dir, "RESMAP.*")))
    ressci_files = sorted(glob.glob(os.path.join(resource_dir, "RESSCI.*")))
    
    print(f"📀 CD disponibles:")
    print(f"   RESMAP: {len(resmap_files)} fichier(s)")
    print(f"   RESSCI: {len(ressci_files)} fichier(s)")
    
    for resmap in resmap_files:
        cd_num = os.path.splitext(resmap)[1][1:]  # .001 -> 001
        ressci = os.path.join(resource_dir, f"RESSCI.{cd_num}")
        
        if not os.path.exists(ressci):
            print(f"   ⚠️  CD{cd_num}: RESMAP trouvé mais RESSCI manquant")
            continue
        
        print(f"   ✅ CD{cd_num}: {os.path.basename(resmap)} + {os.path.basename(ressci)}")
    
    print()
    
    # Chercher dans tous les CD
    found = False
    for resmap in resmap_files:
        cd_num = os.path.splitext(resmap)[1][1:]
        ressci = os.path.join(resource_dir, f"RESSCI.{cd_num}")
        
        if not os.path.exists(ressci):
            continue
        
        print(f"📂 Analyse du CD{cd_num}...")
        scripts = parse_resmap(resmap)
        print(f"   {len(scripts)} scripts trouvés")
        
        # Chercher Robot dans chaque script
        for script_num, offset in scripts:
            results = search_robot_in_script(ressci, offset)
            if results:
                found = True
                print(f"   🎯 Script {script_num} (offset 0x{offset:08X}):")
                for pos, context in results:
                    print(f"      Position +{pos}: {context}")
        print()
    
    if not found:
        print("❌ Aucun appel Robot à 1000 trouvé")
        print()
        print("💡 Suggestions:")
        print("   - Vérifiez que tous les CD sont copiés dans Resource/")
        print("   - Le robot 1000 pourrait être sur un CD non encore copié")
        print("   - Phantasmagoria utilisait 7 CD au total")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
