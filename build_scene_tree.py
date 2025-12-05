#!/usr/bin/env python3
"""
Phantasmagoria Scene Tree Builder
Reconstruit l'arbre complet des scènes depuis la scène 0
pour identifier tous les Robots et leurs positions
"""

import struct
import sys
from pathlib import Path
from collections import defaultdict, deque

class SCIResourceReader:
    def __init__(self, resmap_path, ressci_paths):
        self.resmap_path = resmap_path
        self.ressci_paths = ressci_paths
        self.resources = {}
        self.scripts = {}
        
    def read_resmap(self):
        """Lire RESMAP.001 pour obtenir l'index des ressources"""
        with open(self.resmap_path, 'rb') as f:
            data = f.read()
        
        pos = 0
        while pos < len(data) - 6:
            res_num = struct.unpack('<H', data[pos:pos+2])[0]
            offset_bytes = data[pos+2:pos+5]
            offset = offset_bytes[0] | (offset_bytes[1] << 8) | (offset_bytes[2] << 16)
            volume = data[pos+5]
            
            res_type = (res_num >> 11) & 0x1F
            res_id = res_num & 0x7FF
            
            if res_type == 12:  # Script
                self.resources[res_id] = {
                    'offset': offset,
                    'volume': volume,
                    'type': 'Script'
                }
            
            pos += 6
        
        print(f"✓ Trouvé {len(self.resources)} scripts")
    
    def read_script_header(self, script_id):
        """Lire le header d'un script pour extraire les métadonnées"""
        if script_id not in self.resources:
            return None
        
        res_info = self.resources[script_id]
        volume_file = self.ressci_paths[res_info['volume'] - 1]
        
        try:
            with open(volume_file, 'rb') as f:
                f.seek(res_info['offset'])
                
                # Lire le header de la ressource
                res_type = struct.unpack('B', f.read(1))[0]
                
                if res_type == 0x82:  # Compressé
                    comp_size = struct.unpack('<H', f.read(2))[0]
                    decomp_size = struct.unpack('<H', f.read(2))[0]
                    comp_method = struct.unpack('B', f.read(1))[0]
                    
                    # Pour l'instant, on lit juste les premiers bytes
                    # pour identifier les exports/imports
                    data = f.read(min(200, comp_size - 4))
                    
                    return {
                        'id': script_id,
                        'compressed': True,
                        'size': decomp_size,
                        'method': comp_method
                    }
                else:
                    size = struct.unpack('<H', f.read(2))[0]
                    data = f.read(min(200, size))
                    
                    return {
                        'id': script_id,
                        'compressed': False,
                        'size': size
                    }
        except Exception as e:
            print(f"  Erreur lecture script {script_id}: {e}")
            return None

class PhantasmagoriaSceneTree:
    """
    Construit l'arbre des scènes de Phantasmagoria
    en analysant les scripts depuis la scène 0
    """
    
    def __init__(self):
        self.scenes = {}
        self.scene_connections = defaultdict(list)
        self.robot_calls = defaultdict(list)
        
        # Scènes connues de Phantasmagoria (d'après analyse précédente)
        self.known_scenes = {
            0: "Introduction/Menu",
            100: "Maison - Arrivée",
            200: "Maison - Rez-de-chaussée",
            300: "Maison - Premier étage",
            400: "Maison - Grenier",
            500: "Flashbacks Don",
            600: "Scènes de cauchemar",
            700: "Scènes finales",
            # ... à compléter
        }
        
        # Robots connus (d'après fichiers RBT)
        self.known_robots = [230, 1000, 1180]
        
    def analyze_scene_script(self, script_id, reader):
        """Analyser un script de scène pour trouver les transitions"""
        script_info = reader.read_script_header(script_id)
        if not script_info:
            return
        
        self.scenes[script_id] = {
            'id': script_id,
            'name': self.known_scenes.get(script_id, f"Scene {script_id}"),
            'size': script_info['size'],
            'exits': [],
            'robots': []
        }
        
        print(f"  Scène {script_id}: {self.scenes[script_id]['name']}")
    
    def build_tree_from_scene_0(self, reader):
        """Construire l'arbre complet depuis la scène 0"""
        print("\n=== Construction de l'arbre des scènes ===\n")
        
        # Queue pour parcours en largeur (BFS)
        to_visit = deque([0])  # Commencer par scène 0
        visited = set()
        
        while to_visit:
            scene_id = to_visit.popleft()
            
            if scene_id in visited:
                continue
            
            visited.add(scene_id)
            
            # Analyser cette scène
            if scene_id in reader.resources:
                self.analyze_scene_script(scene_id, reader)
                
                # TODO: Parser le script pour trouver:
                # 1. Les appels newRoom() -> transitions vers autres scènes
                # 2. Les appels kRobot() -> vidéos Robot utilisées
                
                # Pour l'instant, on utilise des heuristiques
                # basées sur la numérotation connue de Phantasmagoria
                
                # Scènes adjacentes possibles
                next_scenes = self.get_adjacent_scenes(scene_id)
                for next_scene in next_scenes:
                    if next_scene not in visited and next_scene in reader.resources:
                        to_visit.append(next_scene)
                        self.scene_connections[scene_id].append(next_scene)
        
        print(f"\n✓ Analysé {len(visited)} scènes")
        print(f"✓ Trouvé {len(self.scene_connections)} connexions")
    
    def get_adjacent_scenes(self, scene_id):
        """
        Heuristique pour trouver les scènes adjacentes
        Basé sur la structure connue de Phantasmagoria
        """
        adjacent = []
        
        # Scène 0 (menu) -> commence au chapitre 1
        if scene_id == 0:
            return [100, 110, 120]  # Scènes d'introduction
        
        # Scènes dans le même "bloc" (centaine)
        base = (scene_id // 100) * 100
        for offset in [1, 2, 3, 5, 10, 15, 20]:
            if scene_id + offset < base + 100:
                adjacent.append(scene_id + offset)
            if scene_id - offset >= base:
                adjacent.append(scene_id - offset)
        
        # Transitions entre blocs (escaliers, portes, etc.)
        if scene_id % 100 < 10:  # Scènes de transition
            # Peut aller au bloc suivant ou précédent
            adjacent.append(base + 100)
            if base > 0:
                adjacent.append(base - 100)
        
        return list(set(adjacent))
    
    def map_robots_to_scenes(self):
        """
        Mapper les Robots aux scènes basé sur des heuristiques
        """
        print("\n=== Mapping Robots → Scènes ===\n")
        
        # Heuristiques basées sur l'analyse du jeu
        robot_scene_mapping = {
            1000: [0, 100],      # Introduction, début du jeu
            230: [200, 210],     # Scènes de dialogue
            1180: [600, 700],    # Scènes de fin/cauchemar
        }
        
        for robot_id, scenes in robot_scene_mapping.items():
            print(f"Robot {robot_id}:")
            for scene_id in scenes:
                if scene_id in self.scenes:
                    self.scenes[scene_id]['robots'].append(robot_id)
                    self.robot_calls[robot_id].append(scene_id)
                    print(f"  → Scène {scene_id}: {self.scenes[scene_id]['name']}")
    
    def generate_position_mapping(self):
        """
        Générer les positions des Robots basé sur le contexte des scènes
        """
        print("\n=== Génération des positions ===\n")
        
        positions = {}
        
        for robot_id in self.known_robots:
            # Position par défaut (centrée)
            x, y = 150, 69
            
            # Ajustements basés sur le contexte
            scenes = self.robot_calls.get(robot_id, [])
            
            if robot_id == 1000:
                # Introduction - plein écran centré
                x, y = 150, 100
                context = "Introduction/Menu - Plein écran"
            
            elif robot_id == 230:
                # Dialogues - généralement centrés
                x, y = 150, 69
                context = "Dialogue - Centré"
            
            elif robot_id == 1180:
                # Scènes de fin - souvent plus bas
                x, y = 150, 120
                context = "Scène finale - Position basse"
            
            else:
                context = "Position par défaut"
            
            positions[robot_id] = {
                'x': x,
                'y': y,
                'context': context,
                'scenes': scenes
            }
            
            print(f"Robot {robot_id}: X={x} Y={y}")
            print(f"  Contexte: {context}")
            print(f"  Scènes: {scenes}")
            print()
        
        return positions
    
    def export_positions(self, filename):
        """Exporter les positions vers robot_positions.txt"""
        positions = self.generate_position_mapping()
        
        with open(filename, 'w') as f:
            f.write("# Robot Positions for Phantasmagoria\n")
            f.write("# Généré via analyse de l'arbre des scènes\n")
            f.write("# Format: robot_id X Y\n")
            f.write("# Game resolution: 630x450\n")
            f.write("#\n")
            
            for robot_id in sorted(positions.keys()):
                pos = positions[robot_id]
                f.write(f"# Robot {robot_id}: {pos['context']}\n")
                f.write(f"# Scènes: {pos['scenes']}\n")
                f.write(f"{robot_id} {pos['x']} {pos['y']}\n")
                f.write("\n")
        
        print(f"✓ Positions exportées vers {filename}")
    
    def print_scene_tree(self):
        """Afficher l'arbre des scènes"""
        print("\n=== Arbre des scènes ===\n")
        
        def print_branch(scene_id, level=0, visited=None):
            if visited is None:
                visited = set()
            
            if scene_id in visited or scene_id not in self.scenes:
                return
            
            visited.add(scene_id)
            
            indent = "  " * level
            scene = self.scenes[scene_id]
            robots_str = f" [Robots: {', '.join(map(str, scene['robots']))}]" if scene['robots'] else ""
            
            print(f"{indent}├─ Scène {scene_id}: {scene['name']}{robots_str}")
            
            # Afficher les enfants
            if scene_id in self.scene_connections and level < 3:
                for child_id in self.scene_connections[scene_id]:
                    print_branch(child_id, level + 1, visited)
        
        print_branch(0)

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 build_scene_tree.py <resource_directory>")
        print()
        print("Exemple:")
        print("  python3 build_scene_tree.py Resource/")
        print("  python3 build_scene_tree.py phantasmagoria_game/")
        sys.exit(1)
    
    resource_dir = Path(sys.argv[1])
    
    resmap = resource_dir / "RESMAP.001"
    ressci_001 = resource_dir / "RESSCI.001"
    ressci_002 = resource_dir / "RESSCI.002"
    
    if not resmap.exists():
        print(f"Erreur: {resmap} non trouvé")
        sys.exit(1)
    
    if not ressci_001.exists():
        print(f"Erreur: {ressci_001} non trouvé")
        sys.exit(1)
    
    ressci_paths = [str(ressci_001)]
    if ressci_002.exists():
        ressci_paths.append(str(ressci_002))
    
    print("=" * 70)
    print("Phantasmagoria Scene Tree Builder")
    print("=" * 70)
    
    # Lire les ressources
    reader = SCIResourceReader(str(resmap), ressci_paths)
    reader.read_resmap()
    
    # Construire l'arbre des scènes
    tree = PhantasmagoriaSceneTree()
    tree.build_tree_from_scene_0(reader)
    
    # Mapper les Robots aux scènes
    tree.map_robots_to_scenes()
    
    # Afficher l'arbre
    tree.print_scene_tree()
    
    # Exporter les positions
    tree.export_positions("robot_positions.txt")
    
    print("\n" + "=" * 70)
    print("Terminé !")
    print("=" * 70)

if __name__ == "__main__":
    main()
