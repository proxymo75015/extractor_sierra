# Analyse du Format SCI32 (Phantasmagoria)

## Informations extraites de SCI Companion

### 1. Structure RESMAP.00X

```cpp
#pragma pack(push, 1)

// Header : Table de pre-entries (lookup table)
struct RESOURCEMAPPREENTRY_SCI1 {
    uint8_t  bType;      // 0x80-0x91 = type ressource, 0xFF = terminateur
    uint16_t wOffset;    // Offset absolu dans RESMAP vers les entrées de ce type
};

// SCI1.1 (Phantasmagoria) : Entrées de 5 octets
struct RESOURCEMAPENTRY_SCI1_1 {
    uint16_t wNumber;     // Numéro de ressource (0 à 65535)
    uint16_t offsetLow;   // 16 bits bas de l'offset
    uint8_t  offsetHigh;  // 8 bits hauts de l'offset
    
    uint32_t GetOffset() const {
        return ((offsetLow | (offsetHigh << 16)) << 1); // Offset * 2 (alignement WORD)
    }
};

#pragma pack(pop)
```

### 2. Structure RESSCI.00X

```cpp
// Header de ressource dans RESSCI.00X
struct RESOURCEHEADER_SCI1 {
    uint16_t iType;           // Type | 0x80
    uint16_t cbCompressed;    // Taille compressée
    uint16_t cbDecompressed;  // Taille décompressée
    uint16_t iMethod;         // Méthode de compression
};
```

### 3. Types de Ressources SCI32

```cpp
enum ResourceType {
    RT_VIEW = 0x00,
    RT_PIC = 0x01,
    RT_SCRIPT = 0x02,       // ✅ Scripts de jeu
    RT_TEXT = 0x03,
    RT_SOUND = 0x04,
    RT_MEMORY = 0x05,
    RT_VOCAB = 0x06,
    RT_FONT = 0x07,
    RT_CURSOR = 0x08,
    RT_PATCH = 0x09,
    RT_BITMAP = 0x0A,
    RT_PALETTE = 0x0B,
    RT_CDAUDIO = 0x0C,
    RT_AUDIO = 0x0D,
    RT_SYNC = 0x0E,
    RT_MESSAGE = 0x0F,
    RT_MAP = 0x10,
    RT_HEAP = 0x11          // ✅ Heap resources (SCI1.1+)
};
```

### 4. Méthodes de Compression

D'après `ResourceBlob.cpp::VersionAndCompressionNumberToAlgorithm()` :

```cpp
switch (compressionNumber) {
    case 0:  return None;
    case 1:  return Huffman (SCI1) ou LZW (SCI0);
    case 2:  return LZW1 (SCI1) ou Huffman (SCI0);
    case 3:  return LZW_View;
    case 4:  return LZW_Pic;
    case 8:  return DCL;
    case 18: return DCL;
    case 19: return DCL;
    case 20: return DCL;
    case 32: return STACpack (LZS);
}
```

**Phantasmagoria utilise :**
- **Méthode 26624 (0x6800)** = Probablement **STACpack/LZS** ou variante DCL

### 5. Algorithmes de Décompression Disponibles

#### A. STACpack/LZS (méthode 32)

```cpp
// CodecSTAC.cpp
bool decompressLZS(byte *dest, byte *src, uint32_t unpackedSize, uint32_t packedSize)
{
    ReadStream readStream(src);
    DecompressorLZS stac;
    return stac.unpack(&readStream, dest, packedSize, unpackedSize);
}
```

**Principe :**
- Basé sur LZ77 avec offsets 7-bit ou 11-bit
- Longueurs variables encodées en bits MSB
- Marqueur de fin : offset 7-bit = 0

**Algorithme :**
```cpp
while (!isFinished()) {
    if (getBitsMSB(1)) { // Compressed bytes
        if (getBitsMSB(1)) { // 7 bit offset
            offs = getBitsMSB(7);
            if (!offs) break; // End marker
            clen = getCompLen();
            copyComp(offs, clen);
        }
        else { // 11 bit offset
            offs = getBitsMSB(11);
            clen = getCompLen();
            copyComp(offs, clen);
        }
    }
    else { // Literal byte
        putByte(getByteMSB());
    }
}
```

#### B. DCL (méthodes 8, 18-20)

```cpp
// CodecDCL.cpp
bool decompressDCL(byte *dest, byte *src, uint32_t unpackedSize, uint32_t packedSize)
{
    ReadStream readStream(src);
    DecompressorDCL dcl;
    return dcl.unpack(&readStream, dest, packedSize, unpackedSize);
}
```

**Principe :**
- DCL = DEFLATE-like compression
- Utilise des arbres de Huffman pour longueurs et distances
- 2 modes : Binary (0x00) ou ASCII (0x01)

**Algorithme :**
```cpp
int mode = getByteLSB();              // 0x00 ou 0x01
int length_param = getByteLSB();      // 3-6

while (_dwWrote < _szUnpacked) {
    if (getBitsLSB(1)) { // (length, distance) pair
        value = huffman_lookup(length_tree);
        if (value < 8)
            val_length = value + 2;
        else
            val_length = 8 + (1 << (value - 7)) + getBitsLSB(value - 7);
        
        value = huffman_lookup(distance_tree);
        
        if (val_length == 2)
            val_distance = (value << 2) | getBitsLSB(2);
        else
            val_distance = (value << length_param) | getBitsLSB(length_param);
        val_distance++;
        
        // Copier val_length bytes depuis -val_distance
        for (uint32_t i = 0; i < val_length; i++)
            putByte(dest[pos + i]);
    }
    else { // Literal byte
        value = (mode == DCL_ASCII_MODE) ? 
                huffman_lookup(ascii_tree) : 
                getByteLSB();
        putByte(value);
    }
}
```

#### C. LZW (méthodes 1-2)

```cpp
int decompressLZW(BYTE *dest, BYTE *src, int length, int complength)
```

**Principe :**
- Dictionnaire dynamique de 9-12 bits
- Tokens 0x100 = reset, 0x101 = EOF
- Reconstruction récursive des chaînes

### 6. Résultats de l'Analyse Phantasmagoria CD1

**RESMAP.001 :**
- 11 types de ressources
- 315 entrées pour Scripts (type 0x82)
- 410 entrées pour Heaps (type 0x91)
- **260 scripts uniques** (après filtrage)
- **266 heaps uniques**

**RESSCI.001 :**
- Taille : 69,963,685 octets (67 MB)
- Méthode de compression : **26624 (0x6800)**
- Scripts à décompresser avant analyse

### 7. Prochaines Étapes

1. **Identifier la méthode 0x6800** :
   - Tester décompression STACpack/LZS
   - Tester décompression DCL
   - Analyser les 2 premiers octets du flux compressé

2. **Décompresser les scripts** :
   - Implémenter le décompresseur approprié
   - Extraire le bytecode brut

3. **Parser le bytecode SCI32** :
   - Chercher séquences `PUSHI robotNum ... CALLK 0x3A`
   - Extraire paramètres x,y de Robot()

### 8. Structure Cible : Appel Robot()

**Signature dans le bytecode :**
```
PUSHI scale       (0x38 + 2 bytes)
PUSHI y           (0x38 + 2 bytes)
PUSHI x           (0x38 + 2 bytes)
PUSHI priority    (0x38 + 2 bytes)
PUSHI planeObj    (0x38 + 2 bytes)
PUSHI robotId     (0x38 + 2 bytes)
CALLK 0x3A        (0x46 0x3A)
```

**Ordre inversé possible selon la stack SCI.**

### 9. Références Code SCI Companion

**Fichiers clés :**
- `SCICompanionLib/Src/Util/CodecSTAC.cpp` - STACpack/LZS
- `SCICompanionLib/Src/Util/CodecDCL.cpp` - DCL
- `SCICompanionLib/Src/Util/Codec.cpp` - LZW, Huffman
- `SCICompanionLib/Src/Resources/ResourceBlob.cpp` - Gestion ressources
- `SCICompanionLib/Src/Compile/CompiledScript.cpp` - Parser scripts
- `SCICompanionLib/Src/Resources/ResourceSources.h` - Lecture RESMAP/RESSCI

### 10. Test Nécessaire

Pour identifier 0x6800, examiner :
```bash
hexdump -C Resource/RESSCI.001 | grep -A5 "68 00"
```

Les 2 premiers octets du flux compressé indiquent le mode :
- DCL : `00` (binary) ou `01` (ASCII)
- LZS : Bits de contrôle MSB

### 11. Opcodes SCI32 Référence

```cpp
#define OP_PUSHI      0x38  // Push immediate 16-bit (little-endian)
#define OP_PUSH_BYTE  0x39  // Push immediate 8-bit
#define OP_CALLK      0x46  // Call kernel function

// Kernel Robot = 0x3A (58 decimal)
```

### 12. Format Robot() Attendu

```cpp
kRobotOpen(robotId, planeObj, priority, x, y, scale)
```

**Paramètres :**
- `robotId` : numéro RBT (ex: 1000)
- `planeObj` : objet plan (généralement variable)
- `priority` : priorité d'affichage
- `x, y` : coordonnées écran **ABSOLUES** (630x450)
- `scale` : échelle (100 = 100%)
