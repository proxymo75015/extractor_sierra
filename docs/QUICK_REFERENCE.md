# Référence Rapide - Décodeurs DPCM16 et LZS

Guide rapide pour comprendre et utiliser les décodeurs.

---

## DPCM16 - Décodeur Audio

### Utilisation Basique

```cpp
#include "formats/dpcm.h"

// Décompresser un bloc audio
std::vector<uint8_t> compressedData = /* ... */;
std::vector<int16_t> samples(compressedData.size());
int16_t sampleValue = 0;  // Valeur initiale

deDPCM16Mono(samples.data(), compressedData.data(), 
             compressedData.size(), sampleValue);
```

### Format d'Entrée

Chaque octet compressé encode un delta :
```
Bit 7    : Signe (0=+, 1=-)
Bits 0-6 : Index table (0-127)
```

### Principe

```
Sample[0] = 0 + delta[0]
Sample[1] = Sample[0] + delta[1]
Sample[2] = Sample[1] + delta[2]
...
```

### Overflow x86

```cpp
if (sample > 32767)  sample -= 65536;
if (sample < -32768) sample += 65536;
```

### Runway Robot Audio

**Important** : Les paquets audio Robot ont 8 bytes de runway :
```cpp
// Décompresser TOUT le paquet (runway inclus)
deDPCM16Mono(samples.data(), compressed.data(), size, carry);

// Sauter les 8 premiers samples lors de l'écriture
for (size_t i = 8; i < samples.size(); ++i) {
    output[writePos] = samples[i];
    writePos += 2;  // Entrelacement
}
```

---

## LZS - Décodeur Vidéo

### Utilisation Basique

```cpp
#include "formats/lzs.h"

// Décompresser un bloc vidéo
std::vector<uint8_t> compressed = /* ... */;
std::vector<uint8_t> decompressed(expectedSize);

int result = LZSDecompress(compressed.data(), compressed.size(),
                           decompressed.data(), decompressed.size());
                           
if (result == 0) {
    // Succès
} else {
    // Erreur de décompression
}
```

### Format de Flux

```
Bit 0 = 0 : Littéral (8 bits suivants = octet)
Bit 0 = 1 : Correspondance
    Bit 1 = 1 : Offset 7 bits + longueur
    Bit 1 = 0 : Offset 11 bits + longueur
```

### Encodage de Longueur

| Bits | Longueur |
|------|----------|
| `00` | 2 |
| `01` | 3 |
| `10` | 4 |
| `11 00` | 5 |
| `11 01` | 6 |
| `11 10` | 7 |
| `11 11 nnnn...` | 8 + somme(nibbles) |

### Marqueur de Fin

Correspondance avec **offset 7 bits = 0** termine le flux.

### Copie de Correspondance

```
Position source = position_actuelle - offset
Copier 'longueur' octets depuis position source
```

**Note** : Permet les chevauchements (auto-référence).

---

## Exemple Complet : Extraction Audio RBT

```cpp
#include "core/rbt_parser.h"

int main() {
    FILE *f = fopen("video.rbt", "rb");
    RbtParser parser(f);
    
    if (!parser.parseHeader()) {
        fprintf(stderr, "Erreur de parsing\n");
        return 1;
    }
    
    if (parser.hasAudio()) {
        // Extraire tout l'audio en audio.wav
        parser.extractAudio("./output");
        
        // Ou seulement les N premières frames
        parser.extractAudio("./output", 100);
    }
    
    fclose(f);
    return 0;
}
```

---

## Exemple : Décompression Manuelle Cel Vidéo

```cpp
#include "formats/decompressor_lzs.h"
#include "utils/memstream.h"

void decompressCel(const uint8_t *compressed, uint32_t compSize,
                   uint8_t *output, uint32_t decompSize) {
    Common::MemoryReadStream stream(compressed, compSize);
    DecompressorLZS decompressor;
    
    int result = decompressor.unpack(&stream, output, compSize, decompSize);
    
    if (result != 0) {
        fprintf(stderr, "Erreur LZS\n");
    }
}
```

---

## Structure Audio Robot

```
Audio Robot = Primer EVEN + Primer ODD + Paquets audio

Primer EVEN:  ~19922 bytes → 19922 samples @ position 0, 2, 4, 6...
Primer ODD:   ~21024 bytes → 21024 samples @ position 1, 3, 5, 7...

Paquet audio: [header 8 bytes][runway 8 bytes][data ~2205 bytes]
              ↓
              Position absolue + taille bloc
              ↓
              Canal déterminé par (position % 4 == 0) → EVEN/ODD
              ↓
              Décompression DPCM16 (sample initial = 0)
              ↓
              Écriture en sautant runway (8 premiers samples)
              ↓
              Entrelacement aux positions paires/impaires

Résultat: 22050 Hz mono
```

---

## Codes d'Erreur

### LZSDecompress

- `0` : Succès
- `1` : Erreur (offset invalide, overflow, taille incorrecte)

### DecompressorLZS::unpack

- `0` : Succès
- `-1` : Erreur de stream
- `1` : Erreur de décompression

---

## Tables de Référence

### Table DPCM16 (extrait)

```cpp
Index  0-9  : 0x0000, 0x0008, 0x0010, 0x0020, 0x0030, 0x0040, 0x0050, 0x0060, 0x0070, 0x0080
Index 10-19 : 0x0090, 0x00A0, 0x00B0, 0x00C0, 0x00D0, 0x00E0, 0x00F0, 0x0100, 0x0110, 0x0120
...
Index 118-127: 0x0900, 0x0A00, 0x0B00, 0x0C00, 0x0D00, 0x0E00, 0x0F00, 0x1000, 0x1400, 0x1800, 0x1C00, 0x2000, 0x3000, 0x4000
```

### Tailles Typiques Robot

| Élément | Taille | Description |
|---------|--------|-------------|
| Header RBT | 60 bytes | Métadonnées |
| Primer EVEN | ~19922 bytes | Audio canal pair |
| Primer ODD | ~21024 bytes | Audio canal impair |
| Paquet audio | ~2213 bytes | Header 8 + runway 8 + data ~2197 |
| Samples utiles | 2205 samples | Par paquet @ 10 fps |
| Frame vidéo | Variable | Dépend compression |

---

## Debugging

### Vérifier DPCM

```cpp
// Premier sample doit être proche de delta[0]
int16_t sample = 0;
uint8_t delta = compressedData[0];
deDPCM16Channel(&result, sample, delta);
printf("First sample: %d\n", result);
```

### Vérifier LZS

```cpp
// Afficher les premiers octets décompressés
uint8_t output[1024];
LZSDecompress(compressed, compSize, output, 1024);
for (int i = 0; i < 16; ++i) {
    printf("%02X ", output[i]);
}
```

### Vérifier Audio Robot

```cpp
// Canal EVEN : audioPos % 4 == 0
// Canal ODD  : audioPos % 4 != 0
int32_t audioPos = /* lire de l'en-tête */;
bool isEven = (audioPos % 4 == 0);
printf("Canal: %s\n", isEven ? "EVEN" : "ODD");
```

---

## Compilation

```bash
cd src/build
cmake ..
make -j4

# Exécuter
./robot_decoder <fichier.rbt> <dossier_sortie>
```

---

## Documentation Complète

- **LZS_DECODER_DOCUMENTATION.md** : Détails complets algorithme LZS
- **DPCM16_DECODER_DOCUMENTATION.md** : Détails complets algorithme DPCM16
- **FORMAT_RBT_DOCUMENTATION.md** : Structure fichier RBT
- **AUDIO_EXTRACTION_NOTES.md** : Guide extraction audio
- **VERIFICATION_REPORT.md** : Rapport de conformité ScummVM

---

**Version** : 1.0  
**Date** : Novembre 2024
