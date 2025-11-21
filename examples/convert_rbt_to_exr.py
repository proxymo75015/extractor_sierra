#!/usr/bin/env python3
"""
Script d'exemple pour convertir un fichier Robot (.RBT) en séquence OpenEXR

Ce script montre comment utiliser RobotEXRExporter pour exporter des frames
Robot en format OpenEXR multi-couches préservant les 3 types de pixels :
- Opaque (0-235 PC / 0-236 Mac)
- Remap (236-254 PC / 237-254 Mac)
- Transparent (255)

Usage:
    python convert_rbt_to_exr.py input.rbt output_dir/ [--platform mac] [--frames 0-100]
"""

import subprocess
import sys
import os
import argparse
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(
        description="Convertir un fichier Robot (.RBT) en séquence OpenEXR"
    )
    parser.add_argument(
        "input_rbt",
        help="Chemin du fichier .RBT à convertir"
    )
    parser.add_argument(
        "output_dir",
        help="Répertoire de sortie pour les fichiers .exr"
    )
    parser.add_argument(
        "--platform",
        choices=["pc", "mac"],
        default="pc",
        help="Plateforme (PC: remap 236-254, Mac: remap 237-254)"
    )
    parser.add_argument(
        "--frames",
        help="Plage de frames à exporter (ex: '0-100' ou '50-150')"
    )
    parser.add_argument(
        "--compression",
        choices=["none", "zip", "piz", "rle", "zips"],
        default="zip",
        help="Type de compression OpenEXR"
    )
    parser.add_argument(
        "--no-indices",
        action="store_true",
        help="Ne pas inclure la couche pixel_index.Y (debug)"
    )
    parser.add_argument(
        "--no-palette-metadata",
        action="store_true",
        help="Ne pas inclure la palette dans les métadonnées"
    )
    
    args = parser.parse_args()
    
    # Vérifier que le fichier d'entrée existe
    if not os.path.isfile(args.input_rbt):
        print(f"Erreur: Fichier introuvable: {args.input_rbt}", file=sys.stderr)
        return 1
    
    # Créer le répertoire de sortie si nécessaire
    output_path = Path(args.output_dir)
    output_path.mkdir(parents=True, exist_ok=True)
    
    # Construire la commande robot_decoder
    # Note: Ceci est un exemple - adapter selon l'interface réelle du robot_decoder
    cmd = [
        "./robot_decoder",
        args.input_rbt,
        "--export-exr",
        "--output-dir", str(output_path),
        "--platform", args.platform,
        "--compression", args.compression
    ]
    
    if args.frames:
        cmd.extend(["--frames", args.frames])
    
    if args.no_indices:
        cmd.append("--no-pixel-indices")
    
    if args.no_palette_metadata:
        cmd.append("--no-palette-metadata")
    
    print(f"Conversion de {args.input_rbt} vers {output_path}/")
    print(f"Platform: {args.platform.upper()}")
    print(f"Compression: {args.compression.upper()}")
    
    # Exécuter la commande
    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        print(result.stdout)
        print(f"\n✓ Conversion terminée avec succès!")
        print(f"  Fichiers EXR générés dans: {output_path}/")
        return 0
        
    except subprocess.CalledProcessError as e:
        print(f"Erreur lors de la conversion:", file=sys.stderr)
        print(e.stderr, file=sys.stderr)
        return 1
    except FileNotFoundError:
        print(
            "Erreur: robot_decoder introuvable.\n"
            "Assurez-vous que le binaire est compilé et dans le PATH.",
            file=sys.stderr
        )
        return 1

if __name__ == "__main__":
    sys.exit(main())
