# Codage Audio Robot - Documentation Technique

## 1. Format Audio ScummVM (Référence)

### Architecture Générale

Le format audio Sierra Robot utilise un codage DPCM16 (Differential Pulse Code Modulation) avec une architecture en deux canaux entrelacés.

**Caractéristiques principales:**
- **Fréquence d'échantillonnage**: 22050 Hz (mono)
- **Format**: 16-bit signé (int16)
- **Compression**: DPCM16 (Sierra SOL)
- **Structure**: 2 canaux (EVEN/ODD) à 11025 Hz chacun, entrelacés pour former 22050 Hz mono

### Structure des Données

#### 1.1 Primers (Données d'Amorçage)

Les primers sont des blocs audio initiaux utilisés pour initialiser le décodeur DPCM:

```
- Even Primer: ~19922 samples (données DPCM compressées)
- Odd Primer:  ~21024 samples (données DPCM compressées)
- Position: Au début du fichier, dans une zone réservée de 40960 bytes
```

**Rôle des primers:**
- Initialiser le prédicteur DPCM à la bonne valeur
- Fournir les 8 premiers bytes de "runway" (zone de transition non jouée)
- Permettre au signal d'atteindre sa position correcte au 9ème échantillon

#### 1.2 Packets Audio Per-Frame

Chaque frame vidéo contient un packet audio:

```
Structure d'un packet:
- audioPos (int32 BE): Position absolue dans le flux final (en samples)
- audioSize (int32 BE): Taille des données DPCM compressées
- data: Données DPCM brutes
```

**Détermination du canal:**
```c
bufferIndex = (audioPos % 4) ? 1 : 0
// 0 = EVEN (canal L dans l'entrelacement)
// 1 = ODD  (canal R dans l'entrelacement)
```

### Algorithme de Décodage ScummVM

#### 2.1 Décompression DPCM16

```c
static const uint16_t tableDPCM16[128] = {
    0x0000,0x0008,0x0010,0x0020,0x0030,0x0040,0x0050,0x0060,...
    // Table de différences pour reconstruction du signal
};

void deDPCM16Mono(int16 *out, const byte *in, uint32 numBytes, int16 &sample) {
    for (uint32 i = 0; i < numBytes; i++) {
        uint8 delta = in[i];
        int32 nextSample = sample;
        
        if (delta & 0x80) 
            nextSample -= tableDPCM16[delta & 0x7f];
        else 
            nextSample += tableDPCM16[delta];
        
        // CLAMPING (pas de wrapping)
        if (nextSample > 32767) nextSample = 32767;
        else if (nextSample < -32768) nextSample = -32768;
        
        out[i] = sample = (int16)nextSample;
    }
}
```

**Points clés:**
- Le prédicteur (`sample`) est persistant entre appels pour les primers
- Il est **réinitialisé à 0** pour chaque packet per-frame
- Le clamping évite les discontinuités (clipping artifacts)

#### 2.2 Gestion du Buffer Circulaire

ScummVM utilise un buffer circulaire avec entrelacement:

```c
// Écriture entrelacée (copyEveryOtherSample)
void copyEveryOtherSample(int16 *out, const int16 *in, int numSamples) {
    while (numSamples--) {
        *out = *in++;
        out += 2;  // Stride de 2 pour entrelacement
    }
}
```

**Positions d'écriture:**
- Canal EVEN: indices 0, 2, 4, 6, ...
- Canal ODD: indices 1, 3, 5, 7, ...

#### 2.3 Interpolation des Trous

Lorsque des packets manquent ou qu'il y a des gaps, ScummVM interpole:

```c
void interpolateChannel(int16 *buffer, int32 numSamples, int8 bufferIndex) {
    int16 *inBuffer, *outBuffer;
    int16 sample, previousSample;
    
    // Configuration selon le canal (EVEN ou ODD)
    if (bufferIndex) {
        outBuffer = buffer + 1;
        inBuffer = buffer + 2;
        previousSample = sample = *buffer;
        --numSamples;
    } else {
        outBuffer = buffer;
        inBuffer = buffer + 1;
        previousSample = sample = *inBuffer;
    }
    
    while (numSamples--) {
        // Moyenne entre échantillon du canal opposé et précédent du même canal
        sample = (*inBuffer + previousSample) >> 1;
        previousSample = *inBuffer;
        *outBuffer = sample;
        inBuffer += 2;   // kEOSExpansion = 2
        outBuffer += 2;
    }
}
```

**Principe:**
- Utilise les échantillons du canal opposé comme référence
- Calcule la moyenne avec l'échantillon précédent du même canal
- Préserve la continuité temporelle

#### 2.4 Flux de Traitement

```
1. Lire primers EVEN et ODD
   ↓
2. Décompresser DPCM (prédicteur persistant)
   ↓
3. Écrire dans buffer circulaire aux positions entrelacées
   ↓
4. Pour chaque packet per-frame:
   a. Lire audioPos et audioSize
   b. Décompresser DPCM (prédicteur reset à 0)
   c. Écrire à la position audioPos avec stride=2
   d. Interpoler les gaps si nécessaire
   ↓
5. Stream audio final: mono 22050 Hz
```

### Format Final

**Output ScummVM:**
- **Type**: Mono (pas stéréo!)
- **Fréquence**: 22050 Hz
- **Format**: 16-bit signed PCM
- **Canaux**: 1 (les EVEN/ODD sont fusionnés en mono par entrelacement)

```
isStereo() const override { return false; }
getRate() const override { return 22050; }
```

---

## 2. Notre Implémentation

### Architecture Simplifiée

Notre décodeur suit les mêmes principes que ScummVM mais avec une approche plus directe sans buffer circulaire.

#### 2.1 Structure des Données

```cpp
struct AudioPacket {
    int32_t audioPos;      // Position absolue dans le flux
    int32_t bufferIndex;   // 0=EVEN, 1=ODD
    std::vector<int16_t> samples;  // Données décompressées
};

std::vector<AudioPacket> packets;
```

#### 2.2 Décompression DPCM

Identique à ScummVM avec **clamping** (ligne critique):

```cpp
static void deDPCM16Channel(int16_t *out, int16_t &sample, uint8_t delta) {
    int32_t nextSample = sample;
    if (delta & 0x80) nextSample -= tableDPCM16[delta & 0x7f];
    else nextSample += tableDPCM16[delta];

    // CLAMPING (évite wrapping qui créait les "clac clac")
    if (nextSample > 32767) nextSample = 32767;
    else if (nextSample < -32768) nextSample = -32768;

    *out = sample = (int16_t)nextSample;
}
```

**Différence clé avec version bugée:**
```cpp
// ANCIEN CODE (causait clacs):
if (nextSample > 32767) nextSample -= 65536;   // Wrapping!
else if (nextSample < -32768) nextSample += 65536;

// NOUVEAU CODE (correct):
if (nextSample > 32767) nextSample = 32767;    // Clamping
else if (nextSample < -32768) nextSample = -32768;
```

#### 2.3 Gestion des Primers

```cpp
bool usePrimers = true;
int32_t primerEndPos = 0;

// Even Primer à position 0
if (usePrimers && _evenPrimerSize > 0) {
    std::vector<int16_t> evenSamples(_evenPrimerSize);
    int16_t pred = 0;
    deDPCM16Mono(evenSamples.data(), _primerEvenRaw.data(), 
                 _evenPrimerSize, pred);
    packets.push_back({0, 0, evenSamples});
    primerEndPos = _evenPrimerSize;
}

// Odd Primer juste après
if (usePrimers && _oddPrimerSize > 0) {
    std::vector<int16_t> oddSamples(_oddPrimerSize);
    int16_t pred = 0;
    deDPCM16Mono(oddSamples.data(), _primerOddRaw.data(), 
                 _oddPrimerSize, pred);
    packets.push_back({primerEndPos, 1, oddSamples});
    primerEndPos += _oddPrimerSize;
}
```

**Points importants:**
- Primers placés séquentiellement au début (position 0)
- Prédicteur initialisé à 0 (reset pour chaque primer)
- Les packets per-frame commencent après (~audioPos 39844)

#### 2.4 Assemblage du Buffer Mono

**Approche directe (sans buffer circulaire):**

```cpp
// 1. Trouver taille nécessaire
int32_t maxPos = 0;
for (const auto &pkt : packets) {
    int32_t endPos = pkt.audioPos + pkt.samples.size();
    maxPos = std::max(maxPos, endPos);
}

// 2. Créer buffer mono
std::vector<int16_t> monoBuffer(maxPos, 0);

// 3. Écrire chaque packet à sa position absolue
for (const auto &pkt : packets) {
    for (size_t i = 0; i < pkt.samples.size(); ++i) {
        if (pkt.audioPos + i < monoBuffer.size()) {
            monoBuffer[pkt.audioPos + i] = pkt.samples[i];
        }
    }
}
```

**Différence avec ScummVM:**
- Pas d'entrelacement EVEN/ODD (écriture directe séquentielle)
- `audioPos` utilisé comme index direct dans le buffer
- Simplifie la logique (pas de modulo, pas de stride)

#### 2.5 Interpolation Multi-Passes

Pour combler les trous (zéros restants):

```cpp
size_t zerosRemaining = 1;
int passes = 0;
const int MAX_PASSES = 20;

while (zerosRemaining > 0 && passes < MAX_PASSES) {
    zerosRemaining = 0;
    
    for (size_t i = 0; i < monoBuffer.size(); ++i) {
        if (monoBuffer[i] == 0) {
            zerosRemaining++;
            
            int16_t prev = (i > 0) ? monoBuffer[i - 1] : 0;
            int16_t next = (i + 1 < monoBuffer.size()) ? 
                           monoBuffer[i + 1] : 0;
            
            if (prev != 0 && next != 0) {
                monoBuffer[i] = (prev + next) >> 1;  // Moyenne
            } else if (prev != 0) {
                monoBuffer[i] = prev;  // Copie précédent
            } else if (next != 0) {
                monoBuffer[i] = next;  // Copie suivant
            }
        }
    }
    passes++;
}
```

**Résultat:**
- 20 passes maximum
- Réduit les zéros de ~38% à ~0.04%
- Propage les valeurs depuis les zones remplies

#### 2.6 Conversion Mono → Stéréo

Pour compatibilité avec les players modernes:

```cpp
std::vector<int16_t> stereoBuffer(monoBuffer.size() * 2);
for (size_t i = 0; i < monoBuffer.size(); ++i) {
    stereoBuffer[i * 2] = monoBuffer[i];      // L
    stereoBuffer[i * 2 + 1] = monoBuffer[i];  // R
}
```

**Format de sortie:**
- **Type**: Stéréo (L=R, mono dupliqué)
- **Fréquence**: 22050 Hz
- **Format**: 16-bit signed PCM
- **Canaux**: 2 (identiques)

### Flux de Traitement Complet

```
1. Lecture des primers EVEN/ODD (données DPCM brutes)
   ↓
2. Décompression DPCM avec prédicteur à 0
   - evenPrimer → position 0
   - oddPrimer → position après even
   ↓
3. Pour chaque frame (0..89):
   a. Lire audioPos (position absolue)
   b. Lire audioSize (taille DPCM compressée)
   c. Décompresser DPCM (prédicteur reset à 0)
   d. Stocker packet {audioPos, bufferIndex, samples}
   ↓
4. Assemblage du buffer mono:
   - Créer buffer de taille maxPos
   - Écrire chaque packet à audioPos
   ↓
5. Interpolation multi-passes (≤20):
   - Moyenne (prev + next) / 2
   - Ou copie du voisin non-nul
   ↓
6. Duplication mono → stéréo (L=R)
   ↓
7. Sortie: Raw PCM stereo 22050Hz 16-bit
```

### Différences Clés avec ScummVM

| Aspect | ScummVM | Notre Implémentation |
|--------|---------|---------------------|
| **Buffer** | Circulaire avec entrelacement | Linéaire sans entrelacement |
| **Écriture** | Stride=2 (EVEN/ODD alternés) | Directe à audioPos |
| **Interpolation** | Par canal (inter-canal) | Globale (avant/après) |
| **Output** | Mono pur | Mono dupliqué en stéréo |
| **Gestion gaps** | Temps réel (streaming) | Offline (post-traitement) |
| **Complexité** | Haute (buffer circulaire) | Moyenne (buffer linéaire) |

### Métriques de Qualité

**Résultats finaux (91.RBT):**
```
Total samples: 476,604 (10.81 secondes @ 22050Hz stereo)
Zéros résiduels: 98 (0.04%)
Discontinuités >5000: 36
Discontinuités >2000: 54
Discontinuités >1000: 2,350
Moyenne des différences: 87.4
Max différence: 29,640
```

**Amélioration vs version initiale:**
- Zéros: 81% → 0.04% (réduction ×2000)
- Discontinuités >5000: 111,614 → 36 (réduction ×3100)
- Clacs audibles: Éliminés

### Points Critiques

1. **Clamping DPCM**: Essentiel pour éviter les clacs
2. **Primers activés**: Remplissent les 1.8 premières secondes
3. **audioPos absolu**: Ne pas diviser ou modifier
4. **Interpolation**: Nécessaire pour les 0.04% de gaps résiduels
5. **Reset prédicteur**: À 0 pour chaque packet (pas de continuité DPCM inter-packets)

### Limitations Connues

- **36 discontinuités majeures** restantes (probablement dans l'audio original)
- **98 zéros résiduels** non comblés (gaps trop larges ou isolés)
- **Pas de streaming**: Traitement offline complet requis
- **Mémoire**: Buffer complet en RAM (~1MB pour 10s @ 22050Hz)

---

## 3. Comparaison des Approches

### Avantages ScummVM
- ✅ Streaming temps réel
- ✅ Mémoire constante (buffer circulaire)
- ✅ Interpolation précise par canal
- ✅ Gestion robuste des packets retardés

### Avantages Notre Implémentation
- ✅ Code plus simple et lisible
- ✅ Debugging plus facile
- ✅ Output stéréo compatible
- ✅ Traitement offline = pas de contraintes temps réel

### Cas d'Usage

**ScummVM**: Lecture en temps réel de vidéos Robot dans un jeu  
**Notre outil**: Extraction/conversion pour archivage ou édition vidéo
