# Robot OpenEXR Export Format

## Vue d'ensemble

Ce document décrit le format OpenEXR multi-couches utilisé pour exporter les vidéos Robot tout en préservant les trois types de pixels distincts du système Robot :

1. **Pixels opaques** (0-235 PC / 0-236 Mac) : Couleurs fixes
2. **Pixels remap** (236-254 PC / 237-254 Mac) : Zones de recoloration dynamique
3. **Pixel transparent** (255) : Skip color

Le format OpenEXR multi-couches permet de stocker toutes ces informations séparément, facilitant la recomposition et l'édition dans des logiciels modernes de compositing (Nuke, Blender, After Effects, etc.).

---

## Structure des couches OpenEXR

Chaque frame Robot exporté contient les couches suivantes :

### 1. Couche Base RGB (`base.R`, `base.G`, `base.B`)

- **Type** : HALF (16-bit float, normalisé 0.0-1.0)
- **Contenu** : Couleurs fixes des pixels opaques (0-235/236)
- **Usage** : Couche principale d'affichage pour les couleurs standards

Pour les pixels de type opaque, cette couche contient la couleur réelle de la palette.
Pour les pixels remap, elle contient aussi la couleur originale (avant transformation dynamique) comme fallback.
Pour les pixels transparents (255), elle contient du noir (0, 0, 0).

### 2. Couche Masque Remap (`remap_mask.Y`)

- **Type** : FLOAT (32-bit)
- **Valeurs** : 
  - `1.0` si le pixel original est dans la plage remap (236-254 PC / 237-254 Mac)
  - `0.0` sinon
- **Usage** : Masque binaire pour identifier les zones de recoloration dynamique

Cette couche permet d'isoler précisément les zones qui étaient soumises à la recoloration dynamique dans le moteur Robot original.

### 3. Couche Couleurs Remap (`remap_color.R`, `remap_color.G`, `remap_color.B`)

- **Type** : HALF (16-bit float, normalisé 0.0-1.0)
- **Contenu** : Couleurs originales de la palette pour les pixels remap
- **Usage** : Référence des couleurs avant transformation dynamique

Ces canaux stockent les couleurs d'origine de la palette pour les pixels qui étaient dans la plage de remap. Pour les autres pixels, ils contiennent du noir.

### 4. Couche Alpha (`alpha.A`)

- **Type** : FLOAT (32-bit)
- **Valeurs** :
  - `0.0` si pixel original = 255 (skip color transparent)
  - `1.0` pour tous les autres pixels
- **Usage** : Canal de transparence standard

### 5. Couche Index Debug (`pixel_index.Y`) [OPTIONNELLE]

- **Type** : HALF (16-bit float)
- **Valeurs** : Index palette original (0-255) normalisé (0.0-1.0)
- **Usage** : Debugging et analyse technique
- **Note** : Peut être désactivée avec l'option `include_pixel_indices = false`

Pour retrouver l'index original : `index = floor(pixel_index.Y * 255.0 + 0.5)`

---

## Métadonnées

Les fichiers EXR incluent les métadonnées suivantes :

| Attribut | Type | Description | Exemple |
|----------|------|-------------|---------|
| `robot:version` | string | Version du format Robot | `"5/6"` |
| `robot:platform` | string | Plateforme d'origine | `"PC"` ou `"Mac"` |
| `robot:remap_range` | string | Plage de remap selon plateforme | `"236-254"` (PC) ou `"237-254"` (Mac) |
| `robot:skip_color` | string | Index du skip color | `"255"` |
| `robot:frame_number` | int | Numéro de frame dans la séquence | `0`, `1`, `2`, ... |
| `robot:palette` | string | Palette complète (768 valeurs CSV) [OPTIONNEL] | `"0,0,0,255,255,255,..."` |

La métadonnée `robot:palette` contient les 256 entrées RGB de la palette originale au format CSV (768 valeurs). Elle peut être désactivée avec `include_palette_metadata = false`.

---

## Workflow de recomposition

### Dans Nuke

```python
# Lire le fichier EXR
Read1 = nuke.createNode('Read')
Read1['file'].setValue('frame_0001.exr')

# Composer l'image finale
# Option 1: Affichage simple (ignore remap)
Shuffle1 = nuke.createNode('Shuffle')
Shuffle1['red'].setValue('base.R')
Shuffle1['green'].setValue('base.G')
Shuffle1['blue'].setValue('base.B')
Shuffle1['alpha'].setValue('alpha.A')

# Option 2: Recoloration des zones remap
# Créer une couleur de remplacement
Constant1 = nuke.createNode('Constant')
Constant1['color'].setValue([1.0, 0.5, 0.0, 1.0])  # Nouvelle couleur

# Mélanger selon le masque
Merge1 = nuke.createNode('Merge2')
Merge1['operation'].setValue('over')
Merge1.setInput(0, Read1)  # Background: base
Merge1.setInput(1, Constant1)  # Foreground: nouvelle couleur
Merge1['maskChannelMask'].setValue('remap_mask.Y')

# Appliquer l'alpha
Premult1 = nuke.createNode('Premult')
Premult1['alpha'].setValue('alpha.A')
```

### Dans Blender (Compositor)

```python
# Configuration du compositing pour frames Robot EXR

import bpy

# Activer le compositor
bpy.context.scene.use_nodes = True
tree = bpy.context.scene.node_tree

# Clear default nodes
for node in tree.nodes:
    tree.nodes.remove(node)

# Créer les nodes
# 1. Input: Lire l'EXR
image_node = tree.nodes.new('CompositorNodeImage')
image_node.image = bpy.data.images.load('//frame_0001.exr')

# 2. Séparer les couches
split_node = tree.nodes.new('CompositorNodeSepRGBA')
tree.links.new(image_node.outputs['Image'], split_node.inputs['Image'])

# 3. Masque remap
remap_mask = tree.nodes.new('CompositorNodeOutputFile')
remap_mask.layer_slots[0].name = 'remap_mask'

# 4. Composer l'image finale
mix_node = tree.nodes.new('CompositorNodeMixRGB')
mix_node.blend_type = 'MIX'

# 5. Output final
output_node = tree.nodes.new('CompositorNodeComposite')
tree.links.new(mix_node.outputs['Image'], output_node.inputs['Image'])
```

### Dans After Effects

1. **Importer la séquence EXR**
   - File → Import → File
   - Sélectionner le premier fichier `frame_0001.exr`
   - Cocher "EXR Sequence"

2. **Extraire les couches**
   - Layer → New → Adjustment Layer (pour chaque couche)
   - Effect → Channel → Shift Channels
   - Configurer selon les couches désirées

3. **Recoloration dynamique**
   - Utiliser le masque `remap_mask.Y` comme masque de fusion
   - Appliquer des effets de couleur (Colorama, Tint, etc.) sur les zones masquées

---

## Exemples de code

### Lecture en Python (OpenEXR)

```python
import OpenEXR
import Imath
import numpy as np

def read_robot_exr(filepath):
    """Lit un fichier Robot EXR et retourne les couches séparées"""
    
    exr_file = OpenEXR.InputFile(filepath)
    header = exr_file.header()
    
    # Récupérer les dimensions
    dw = header['dataWindow']
    width = dw.max.x - dw.min.x + 1
    height = dw.max.y - dw.min.y + 1
    
    # Lire les métadonnées Robot
    metadata = {}
    if 'robot:platform' in header:
        metadata['platform'] = header['robot:platform'].value
    if 'robot:remap_range' in header:
        metadata['remap_range'] = header['robot:remap_range'].value
    if 'robot:frame_number' in header:
        metadata['frame_number'] = header['robot:frame_number'].value
    
    # Lire les couches
    FLOAT = Imath.PixelType(Imath.PixelType.FLOAT)
    HALF = Imath.PixelType(Imath.PixelType.HALF)
    
    # Base RGB (HALF)
    base_r = np.frombuffer(exr_file.channel('base.R', HALF), dtype=np.float16)
    base_g = np.frombuffer(exr_file.channel('base.G', HALF), dtype=np.float16)
    base_b = np.frombuffer(exr_file.channel('base.B', HALF), dtype=np.float16)
    
    base_rgb = np.dstack([
        base_r.reshape(height, width),
        base_g.reshape(height, width),
        base_b.reshape(height, width)
    ])
    
    # Masque remap (FLOAT)
    remap_mask = np.frombuffer(exr_file.channel('remap_mask.Y', FLOAT), dtype=np.float32)
    remap_mask = remap_mask.reshape(height, width)
    
    # Couleurs remap (HALF)
    remap_r = np.frombuffer(exr_file.channel('remap_color.R', HALF), dtype=np.float16)
    remap_g = np.frombuffer(exr_file.channel('remap_color.G', HALF), dtype=np.float16)
    remap_b = np.frombuffer(exr_file.channel('remap_color.B', HALF), dtype=np.float16)
    
    remap_colors = np.dstack([
        remap_r.reshape(height, width),
        remap_g.reshape(height, width),
        remap_b.reshape(height, width)
    ])
    
    # Alpha (FLOAT)
    alpha = np.frombuffer(exr_file.channel('alpha.A', FLOAT), dtype=np.float32)
    alpha = alpha.reshape(height, width)
    
    return {
        'width': width,
        'height': height,
        'base_rgb': base_rgb,
        'remap_mask': remap_mask,
        'remap_colors': remap_colors,
        'alpha': alpha,
        'metadata': metadata
    }

# Utilisation
data = read_robot_exr('frame_0001.exr')
print(f"Dimensions: {data['width']}x{data['height']}")
print(f"Platform: {data['metadata'].get('platform', 'unknown')}")
print(f"Remap range: {data['metadata'].get('remap_range', 'unknown')}")
```

### Recomposition simple en Python

```python
import numpy as np
from PIL import Image

def render_robot_frame(exr_data, remap_color=None):
    """
    Render un frame Robot EXR en image RGB standard
    
    Args:
        exr_data: Dictionnaire retourné par read_robot_exr()
        remap_color: Tuple RGB (0.0-1.0) pour recolorer les zones remap,
                     ou None pour utiliser les couleurs originales
    
    Returns:
        Image PIL RGBA
    """
    height, width = exr_data['base_rgb'].shape[:2]
    
    # Commencer avec la couche base
    result_rgb = exr_data['base_rgb'].copy().astype(np.float32)
    
    # Appliquer la recoloration si demandée
    if remap_color is not None:
        mask = exr_data['remap_mask']
        for c in range(3):
            result_rgb[:, :, c] = (
                result_rgb[:, :, c] * (1.0 - mask) +
                remap_color[c] * mask
            )
    
    # Convertir en uint8
    rgb_uint8 = np.clip(result_rgb * 255.0, 0, 255).astype(np.uint8)
    alpha_uint8 = np.clip(exr_data['alpha'] * 255.0, 0, 255).astype(np.uint8)
    
    # Créer l'image RGBA
    rgba = np.dstack([rgb_uint8, alpha_uint8])
    
    return Image.fromarray(rgba, 'RGBA')

# Exemple d'utilisation
exr_data = read_robot_exr('frame_0001.exr')

# Rendu avec couleurs originales
img_original = render_robot_frame(exr_data)
img_original.save('output_original.png')

# Rendu avec recoloration en orange
img_recolored = render_robot_frame(exr_data, remap_color=(1.0, 0.5, 0.0))
img_recolored.save('output_recolored.png')
```

---

## Avantages du format

### Préservation complète des données

- **Aucune perte d'information** : Les trois types de pixels sont parfaitement séparés
- **Palette originale** : Stockée dans les métadonnées pour référence
- **Indices originaux** : Disponibles pour analyse technique (couche optionnelle)

### Compatibilité moderne

- **Standard VFX** : OpenEXR est le format de référence en production
- **Support universel** : Nuke, Blender, After Effects, Fusion, Natron, etc.
- **High Dynamic Range** : Prêt pour workflows HDR si besoin

### Flexibilité de post-production

- **Recoloration facile** : Les zones remap sont isolées par masque
- **Compositing avancé** : Toutes les couches accessibles séparément
- **Effets par zone** : Appliquer des effets uniquement sur opaque/remap/transparent

### Optimisation

- **Compression efficace** : ZIP/PIZ réduisent la taille sans perte
- **Accès rapide** : Scanline access pour processing en temps réel
- **Métadonnées riches** : Toute l'information contextuelle disponible

---

## Comparaison avec autres formats

| Format | Transparence | Couches multiples | Métadonnées | Compression lossless | Remap support |
|--------|--------------|-------------------|-------------|---------------------|---------------|
| **Robot EXR** | ✅ | ✅ | ✅ | ✅ | ✅ (natif) |
| PNG | ✅ | ❌ | ⚠️ (limité) | ✅ | ❌ |
| WebP | ✅ | ❌ | ⚠️ (limité) | ✅/❌ | ❌ |
| GIF | ✅ | ❌ | ❌ | ✅ | ❌ |
| APNG | ✅ | ❌ | ❌ | ✅ | ❌ |
| ProRes 4444 | ✅ | ❌ | ⚠️ (limité) | ❌ | ❌ |
| DPX | ⚠️ | ⚠️ | ✅ | ✅ | ❌ |

**Légende** :
- ✅ : Support complet et natif
- ⚠️ : Support partiel ou avec limitations
- ❌ : Pas de support

---

## Configuration recommandée

### Pour archivage long terme

```cpp
EXRExportConfig config;
config.compression = EXRExportConfig::Compression::ZIP;
config.include_pixel_indices = true;
config.include_palette_metadata = true;
```

- **Compression ZIP** : Bon ratio sans perte
- **Tous les indices** : Préservation complète pour analyse future
- **Palette en metadata** : Référence exacte des couleurs

### Pour compositing intermédiaire

```cpp
EXRExportConfig config;
config.compression = EXRExportConfig::Compression::PIZ;
config.include_pixel_indices = false;
config.include_palette_metadata = false;
```

- **Compression PIZ** : Meilleure pour images naturelles
- **Pas d'indices** : Réduction de taille
- **Pas de palette metadata** : Focus sur les couches visuelles

### Pour preview rapide

```cpp
EXRExportConfig config;
config.compression = EXRExportConfig::Compression::NONE;
config.include_pixel_indices = false;
config.include_palette_metadata = false;
```

- **Pas de compression** : Décodage plus rapide
- **Données minimales** : Focus sur affichage

---

## Références

- **OpenEXR Specification** : https://openexr.com/
- **IlmBase Library** : https://github.com/AcademySoftwareFoundation/openexr
- **Robot Format Documentation** : Voir `ROBOT_PALETTE_DECODING.md`
- **GfxPalette32 System** : Voir `GFXPALETTE32_SYSTEM.md`

---

## Notes techniques

### Gestion de la gamma

Les couleurs Robot sont stockées en espace linéaire dans l'EXR (après conversion de la palette 8-bit). Pour affichage sRGB correct :

```python
# Appliquer gamma 2.2 pour affichage
display_rgb = np.power(linear_rgb, 1.0/2.2)
```

### Scanline Order

Les fichiers EXR sont stockés en **increasing Y** (top-to-bottom), comme Robot.

### Byte Order

OpenEXR gère automatiquement l'endianness multi-plateforme.

---

**Date de création** : 2025  
**Version du format** : 1.0  
**Auteur** : Robot Extractor Project
