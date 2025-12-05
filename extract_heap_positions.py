#!/usr/bin/env python3
"""
Direct Robot Position Extractor from SCI HEAP
Extracts Robot coordinates from script HEAP sections without playing the game
Based on ScummVM engines/sci/engine/script.cpp
"""

import struct
import sys
from pathlib import Path

class SCIResourceExtractor:
    def __init__(self, resmap_path, ressci_paths):
        self.resmap_path = resmap_path
        self.ressci_paths = ressci_paths
        self.resources = {}
        
    def read_resmap(self):
        """Read RESMAP.001 to get resource directory"""
        with open(self.resmap_path, 'rb') as f:
            data = f.read()
        
        # RESMAP format: entries of 6 bytes
        # Bytes 0-1: resource number (type + id combined)
        # Bytes 2-4: offset in RESSCI (24-bit)
        # Byte 5: volume number
        
        pos = 0
        while pos < len(data):
            if pos + 6 > len(data):
                break
            
            res_num = struct.unpack('<H', data[pos:pos+2])[0]
            offset_bytes = data[pos+2:pos+5]
            offset = offset_bytes[0] | (offset_bytes[1] << 8) | (offset_bytes[2] << 16)
            volume = data[pos+5]
            
            res_type = (res_num >> 11) & 0x1F
            res_id = res_num & 0x7FF
            
            if res_type == 12:  # Script resource
                self.resources[res_id] = {
                    'offset': offset,
                    'volume': volume,
                    'type': 'Script'
                }
            
            pos += 6
        
        print(f"Found {len(self.resources)} Script resources")
        
    def read_resource(self, res_id):
        """Read a resource from RESSCI volumes"""
        if res_id not in self.resources:
            return None
        
        res_info = self.resources[res_id]
        volume_file = self.ressci_paths[res_info['volume'] - 1]
        
        with open(volume_file, 'rb') as f:
            f.seek(res_info['offset'])
            
            # Read resource header
            res_type = struct.unpack('B', f.read(1))[0]
            
            if res_type == 0x82:  # Compressed resource
                comp_size_bytes = f.read(2)
                decomp_size_bytes = f.read(2)
                comp_method = struct.unpack('B', f.read(1))[0]
                
                comp_size = struct.unpack('<H', comp_size_bytes)[0]
                decomp_size = struct.unpack('<H', decomp_size_bytes)[0]
                
                compressed_data = f.read(comp_size - 4)
                
                if comp_method == 3:  # LZS compression
                    return self.lzs_decompress(compressed_data, decomp_size)
                else:
                    print(f"Unknown compression method: {comp_method}")
                    return None
            else:
                # Uncompressed
                size = struct.unpack('<H', f.read(2))[0]
                return f.read(size)
    
    def lzs_decompress(self, data, expected_size):
        """Simple LZS decompression"""
        try:
            from lzs_decompressor import LZSDecompress
            return LZSDecompress(data, expected_size)
        except:
            # Fallback: call compiled decompressor
            import subprocess
            import tempfile
            
            with tempfile.NamedTemporaryFile(delete=False) as tmp:
                tmp.write(data)
                tmp_path = tmp.name
            
            try:
                # This is a placeholder - would need actual LZS implementation
                print(f"Warning: Could not decompress script")
                return None
            finally:
                Path(tmp_path).unlink()

class HEAPParser:
    """Parse SCI script HEAP to find Robot object properties"""
    
    def __init__(self, script_data):
        self.data = script_data
        self.objects = []
        
    def parse(self):
        """Parse script to find HEAP section with objects"""
        if len(self.data) < 4:
            return []
        
        # Look for HEAP section
        # SCI2.1 script format:
        # - Script header
        # - Code section
        # - HEAP section (contains objects with properties)
        
        # Search for object definitions
        # Objects contain property tables with initial values
        
        pos = 0
        while pos < len(self.data) - 20:
            # Look for object signatures
            # In HEAP, objects have a specific structure
            
            # Simple heuristic: look for property values that could be coordinates
            # X: 0-630, Y: 0-450
            
            if pos + 4 <= len(self.data):
                val1 = struct.unpack('<H', self.data[pos:pos+2])[0]
                val2 = struct.unpack('<H', self.data[pos+2:pos+4])[0]
                
                # Check if these could be X,Y coordinates
                if 0 <= val1 <= 630 and 0 <= val2 <= 450:
                    # Potential coordinate pair
                    # Look for Robot class reference nearby
                    
                    # Check surrounding data for Robot indicators
                    if self.is_robot_context(pos):
                        self.objects.append({
                            'offset': pos,
                            'x': val1,
                            'y': val2
                        })
            
            pos += 1
        
        return self.objects
    
    def is_robot_context(self, pos):
        """Check if position is near Robot-related data"""
        # Look for Robot class indicators
        # This is a heuristic - would need full SCI object format
        
        start = max(0, pos - 50)
        end = min(len(self.data), pos + 50)
        context = self.data[start:end]
        
        # Simple check: look for patterns
        # Real implementation would parse object structure
        return True  # Placeholder

def extract_robot_positions_from_heap(resmap_path, ressci_paths):
    """Main extraction function"""
    
    extractor = SCIResourceExtractor(resmap_path, ressci_paths)
    extractor.read_resmap()
    
    robot_positions = {}
    
    # Known scripts that contain Robot calls (from previous analysis)
    # Focus on main game scripts
    important_scripts = [902, 13400, 23, 64994, 64990, 64000]
    
    print("\nAnalyzing scripts for Robot coordinates...\n")
    
    for script_id in important_scripts:
        if script_id not in extractor.resources:
            continue
        
        print(f"Analyzing script {script_id}...")
        script_data = extractor.read_resource(script_id)
        
        if not script_data:
            print(f"  Could not read script {script_id}")
            continue
        
        parser = HEAPParser(script_data)
        objects = parser.parse()
        
        if objects:
            print(f"  Found {len(objects)} potential coordinate pairs")
            for obj in objects:
                print(f"    Offset {obj['offset']:04x}: X={obj['x']} Y={obj['y']}")
    
    return robot_positions

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 extract_heap_positions.py <resource_directory>")
        print()
        print("Example:")
        print("  python3 extract_heap_positions.py Resource/")
        print("  python3 extract_heap_positions.py phantasmagoria_game/")
        sys.exit(1)
    
    resource_dir = Path(sys.argv[1])
    
    resmap = resource_dir / "RESMAP.001"
    ressci_001 = resource_dir / "RESSCI.001"
    ressci_002 = resource_dir / "RESSCI.002"
    
    if not resmap.exists():
        print(f"Error: {resmap} not found")
        sys.exit(1)
    
    if not ressci_001.exists():
        print(f"Error: {ressci_001} not found")
        sys.exit(1)
    
    ressci_paths = [str(ressci_001)]
    if ressci_002.exists():
        ressci_paths.append(str(ressci_002))
    
    print("=" * 80)
    print("Direct Robot Position Extractor from HEAP")
    print("=" * 80)
    print()
    
    positions = extract_robot_positions_from_heap(str(resmap), ressci_paths)
    
    print()
    print("=" * 80)
    print("Analysis complete")
    print("=" * 80)

if __name__ == "__main__":
    main()
