# Trouvailles ScummVM pour SCI32 (Phantasmagoria)

## Source : scummvm/engines/sci/resource/resource.cpp

### 1. Lecture RESMAP SCI1.1/SCI2

```cpp
// Ligne 1973-1988
int ResourceManager::readResourceMapSCI1(ResourceSource *map) {
    // ...
    byte nEntrySize = _mapVersion == kResVersionSci11 ? 
                      SCI11_RESMAP_ENTRIES_SIZE :  // 5 bytes
                      SCI1_RESMAP_ENTRIES_SIZE;     // 6 bytes
    
    // Read resource type and offsets
    do {
        type = fileStream->readByte() & 0x1F;  // Type (retirer bit 0x80)
        resMap[type].wOffset = fileStream->readUint16LE();
        // ...
    } while (type != 0x1F); // FF = terminateur
    
    // Pour chaque type, lire les entrées
    for (type = 0; type < 32; type++) {
        if (resMap[type].wOffset == 0) continue;
        
        fileStream->seek(resMap[type].wOffset);
        
        for (int i = 0; i < resMap[type].wSize; i++) {
            uint16 number = fileStream->readUint16LE();
            
            if (_mapVersion == kResVersionSci11) {
                // SCI1.1 : offset sur 3 bytes, décalé de 1 bit (x2)
                fileOffset = fileStream->readUint16LE();
                fileOffset |= fileStream->readByte() << 16;
                fileOffset <<= 1;  // ✅ ALIGNEMENT WORD
            } else {
                // SCI1 : offset/volume sur 4 bytes
                fileOffset = fileStream->readUint32LE();
                volume_nr = fileOffset >> 28;  // 4 bits MSB
                fileOffset &= 0x0FFFFFFF;      // 28 bits LSB
            }
        }
    }
}
```

### 2. Lecture Headers de Ressources dans RESSCI

```cpp
// Ligne 2296-2314 : resource.cpp::readResourceInfo()
int Resource::readResourceInfo(ResVersion volVersion, Common::SeekableReadStream *file,
                                uint32 &szPacked, ResourceCompression &compression) {
    
    switch (volVersion) {
    case kResVersionSci11:
        type = _resMan->convertResType(file->readByte());
        number = file->readUint16LE();
        szPacked = file->readUint16LE();       // Taille compressée
        szUnpacked = file->readUint16LE();     // Taille décompressée
        wCompression = file->readUint16LE();   // Méthode
        break;
        
    case kResVersionSci2:
    case kResVersionSci3:
        type = _resMan->convertResType(file->readByte());
        number = file->readUint16LE();
        szPacked = file->readUint32LE();       // 4 bytes en SCI2
        szUnpacked = file->readUint32LE();     // 4 bytes en SCI2
        wCompression = file->readUint16LE();
        
        // SCI3 : compression automatique
        if (volVersion == kResVersionSci3)
            wCompression = szPacked != szUnpacked ? 32 : 0;
        break;
    }
}
```

### 3. Méthodes de Compression

```cpp
// Ligne 2340 : resource.cpp
switch (wCompression) {
case 0:
    compression = kCompNone;
    break;
case 1:
    compression = (getSciVersion() <= SCI_VERSION_01) ? kCompLZW : kCompHuffman;
    break;
case 2:
    compression = (getSciVersion() <= SCI_VERSION_01) ? kCompHuffman : kCompLZW1;
    break;
case 3:
    compression = kCompLZW1View;
    break;
case 4:
    compression = kCompLZW1Pic;
    break;
case 18:
case 19:
case 20:
    compression = kCompDCL;
    break;
case 32:
    compression = kCompSTACpack;  // ✅ SCI32
    break;
}
```

### 4. IMPORTANT : Format Phantasmagoria

D'après `detection_tables.h` ligne 1097-1176 :

```
Phantasmagoria CD (7 CDs) :
    resmap.001, ressci.001 (50 MB)
    resmap.002, ressci.002 (42 MB)
    resmap.003, ressci.003 (35 MB)
    resmap.004, ressci.004 (58 MB)
    resmap.005, ressci.005 (37 MB)
    resmap.006, ressci.006 (60 MB)
    resmap.007, ressci.007 (...)
```

**Format détecté : SCI2.1** (ligne 2683-2702)

```cpp
// detectSciVersion()
if (_volVersion >= kResVersionSci2) {
    Common::List<ResourceId> heaps = listResources(kResourceTypeHeap);
    bool hasHeapResources = !heaps.empty();
    
    if (_mapVersion == kResVersionSci1Late) {
        g_sciVersion = SCI_VERSION_2;  // ❌ Phantasmagoria n'est PAS ici
    } else if (hasHeapResources) {
        g_sciVersion = SCI_VERSION_2_1_EARLY;  // ✅ PHANTASMAGORIA
    }
}
```

## 5. Structure CORRECTE pour Phantasmagoria

**Version : SCI 2.1 Early**

### RESMAP.00X :
```
Pre-entries (3 bytes chacune) :
    byte  type & 0x1F
    word  offset dans RESMAP
    
Entrées (5 bytes - SCI1.1 format) :
    word  resource number
    word  offset low (dans RESSCI)
    byte  offset high
    
    Offset réel = ((low | (high << 16)) << 1)  // Alignement WORD
```

### RESSCI.00X :
**IMPORTANT : Headers dans RESSCI pour SCI2.1 !**

```
Pour chaque ressource :
    byte  type
    word  number
    dword packed_size
    dword unpacked_size
    word  compression_method
    byte  data[packed_size]
```

**Différence avec SCI1.1 :**
- SCI1.1 : sizes sur 2 bytes (word)
- SCI2/2.1 : sizes sur 4 bytes (dword)

### RESMDT.00X :
Probablement métadonnées additionnelles (non utilisé par ScummVM ?)

## 6. Code de Décompression ScummVM

**STACpack (méthode 32) :**
- Fichier : `engines/sci/resource/decompressor.cpp`
- Classe : `DecompressorLZS`
- Basé sur LZ77 avec offsets 7/11 bits

**DCL (méthodes 18-20) :**
- Fichier : `engines/sci/resource/decompressor.cpp`  
- Classe : `DecompressorDCL`
- Huffman + LZ

## 7. Prochaine Étape

**Implémenter en C++ :**

1. **Parser RESMAP SCI1.1 correctement** :
   - Lire pre-entries (type & 0x1F, offset)
   - Pour chaque type, lire entrées 5 bytes
   - Calculer offset = ((low | (high << 16)) << 1)

2. **Lire headers dans RESSCI** :
   - Format SCI2.1 : Type(1) Number(2) PackedSize(4) UnpackedSize(4) Method(2)
   - **Total : 13 bytes de header**

3. **Décompresser avec méthode appropriée** :
   - Si méthode == 32 : STACpack/LZS
   - Si méthode == 18-20 : DCL
   - Si méthode == 0 : Non compressé

4. **Parser bytecode décompressé** :
   - Chercher PUSHI + CALLK 0x3A
   - Extraire x, y

## 8. Différence Clé

**Erreur précédente :**
- On cherchait headers dans RESSCI à l'offset du RESMAP
- Mais Phantasmagoria = SCI2.1 avec headers **AVANT** les données

**Correction :**
- Lire header de 13 bytes à l'offset
- Puis lire packed_size bytes
- Décompresser vers unpacked_size
- Parser le bytecode décompressé
