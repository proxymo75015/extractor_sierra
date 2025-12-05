#!/usr/bin/env python3
"""
Analyse approfondie des fichiers RESSCI pour trouver TOUS les numéros
qui pourraient être des références à des fichiers Robot
"""

import struct
import os
import sys
import glob

def search_all_numbers_in_ressci(ressci_path, min_num=1, max_num=10000):
    """Cherche tous les nombres entre min_num et max_num dans le fichier RESSCI"""
    numbers = set()
    
    with open(ressci_path, 'rb') as f:
        data = f.read()
    
    # Chercher tous les nombres encodés en little-endian (2 octets)
    for i in range(len(data) - 1):
        num = struct.unpack('<H', data[i:i+2])[0]
        if min_num <= num <= max_num:
            numbers.add(num)
    
    return numbers

def search_push_instructions(ressci_path):
    """Cherche les instructions PUSHI dans les scripts (opcode 0x38 en SCI32)"""
    numbers = set()
    
    with open(ressci_path, 'rb') as f:
        data = f.read()
    
    # 0x38 = PUSHI (push immediate value)
    # 0x39 = PUSHI avec valeur sur 1 octet
    for i in range(len(data) - 2):
        if data[i] == 0x38:  # PUSHI 16-bit
            num = struct.unpack('<H', data[i+1:i+3])[0]
            if 1 <= num <= 10000:
                numbers.add(num)
        elif data[i] == 0x39:  # PUSHI 8-bit
            num = data[i+1]
            if 1 <= num <= 10000:
                numbers.add(num)
    
    return numbers

def main():
    resource_dir = "/workspaces/extractor_sierra/Resource"
    
    print("🔍 Extraction de TOUS les numéros potentiels de Robot")
    print("=" * 70)
    print()
    
    ressci_files = sorted(glob.glob(os.path.join(resource_dir, "RESSCI.*")))
    
    all_numbers = set()
    
    for ressci in ressci_files:
        cd_num = os.path.splitext(ressci)[1][1:]
        
        print(f"📀 Analyse de RESSCI.{cd_num}...")
        
        # Méthode 1 : Chercher les PUSHI (plus précis)
        numbers = search_push_instructions(ressci)
        print(f"   Nombres trouvés via PUSHI: {len(numbers)}")
        
        all_numbers.update(numbers)
    
    print()
    print("=" * 70)
    print(f"📊 Total: {len(all_numbers)} numéros différents trouvés")
    print()
    
    # Trier et grouper par plages
    sorted_numbers = sorted(all_numbers)
    
    # Afficher par plages de 100
    print("📋 Numéros trouvés (par plages de 100):")
    print()
    
    for start in range(0, 10000, 100):
        end = start + 99
        in_range = [n for n in sorted_numbers if start <= n <= end]
        if in_range:
            print(f"   {start:4d}-{end:4d}: {len(in_range):3d} numéros")
            # Afficher les premiers de chaque plage
            preview = ', '.join(map(str, in_range[:10]))
            if len(in_range) > 10:
                preview += f", ... +{len(in_range)-10}"
            print(f"            {preview}")
    
    print()
    print("=" * 70)
    print("🎯 Vérification des fichiers que vous avez:")
    print()
    
    our_files = [91, 161, 230, 1000, 1014, 1180]
    for num in our_files:
        if num in all_numbers:
            print(f"   ✅ {num} - TROUVÉ dans les scripts !")
        else:
            print(f"   ❌ {num} - NON référencé")
    
    print()
    print("=" * 70)
    print("💾 Export de la liste complète vers robot_numbers.txt")
    
    with open('/workspaces/extractor_sierra/robot_numbers.txt', 'w') as f:
        f.write("# Numéros potentiels de fichiers Robot trouvés dans RESSCI\n")
        f.write(f"# Total: {len(all_numbers)} numéros\n")
        f.write("#\n")
        f.write("# Format: un numéro par ligne\n")
        f.write("#\n\n")
        
        for num in sorted_numbers:
            f.write(f"{num}\n")
    
    print(f"   ✅ {len(all_numbers)} numéros exportés")
    print()
    print("📝 Vérifiez maintenant sur vos CD si ces fichiers .RBT existent:")
    print(f"   cat robot_numbers.txt | head -50")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
