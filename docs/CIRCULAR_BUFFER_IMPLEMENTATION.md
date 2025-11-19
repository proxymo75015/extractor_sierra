# Implémentation du Buffer Circulaire Audio Robot - Fidèle à ScummVM

## Vue d'ensemble

Cette implémentation reproduit **fidèlement** le système de buffer circulaire audio utilisé par ScummVM pour décoder les vidéos Robot de Sierra SCI. Le code est basé directement sur `engines/sci/video/robot_decoder.cpp` et `robot_decoder.h` de ScummVM.

## Architecture

### Classe `RobotAudioStream`

La classe principale qui gère le buffer circulaire avec toutes les spécificités de ScummVM :

#### Caractéristiques clés

1. **Buffer Circulaire** (`_loopBuffer`)
   - Taille: 88200 bytes (2 secondes @ 22050Hz mono 16-bit)
   - Permet le wrapping automatique quand on atteint la fin
   - Évite les réallocations coûteuses en mémoire

2. **Gestion des Positions Absolues**
   - `_readHeadAbs`: Position absolue de lecture dans le flux audio
   - `_writeHeadAbs`: Position absolue d'écriture 
   - `_maxWriteAbs`: Position maximale pouvant être écrite actuellement
   - `_readHead`: Position de lecture dans le buffer circulaire (0 à _loopBufferSize)

3. **Canaux EVEN/ODD Entrelacés**
   - Canal EVEN (0): Échantillons aux indices 0, 2, 4, 6, 8...
   - Canal ODD (1): Échantillons aux indices 1, 3, 5, 7, 9...
   - Détection automatique: `bufferIndex = (position % 4) ? 1 : 0`
   - Tracking indépendant: `_jointMin[0]` et `_jointMin[1]`

4. **Interpolation Automatique**
   - Remplit les "trous" dans chaque canal
   - Calcule la moyenne des échantillons adjacents
   - Appelée automatiquement avant chaque lecture

5. **Gestion des Primers**
   - Position 0: Primer EVEN
   - Position 2: Primer ODD  
   - Le stream attend (`_waiting = true`) jusqu'à réception des deux primers
   - Une fois les deux primers reçus, `_waiting = false` et la lecture peut commencer

## Algorithme Détaillé

### 1. Ajout d'un Paquet (`addPacket`)

```cpp
bool addPacket(const RobotAudioPacket &packet) {
    // 1. Déterminer le canal (EVEN=0 ou ODD=1)
    bufferIndex = (packet.position % 4) ? 1 : 0
    
    // 2. Gérer les primers spécialement
    if (position <= 2 && _firstPacketPosition == -1) {
        // Initialiser le stream
        _readHeadAbs = 0
        _writeHeadAbs = 2
        _jointMin[0] = 0, _jointMin[1] = 2
        _waiting = true
        fillRobotBuffer(packet, bufferIndex)
        return true
    }
    
    // 3. Vérifier si le paquet est obsolète
    if (packetEndByte <= max(_readHeadAbs, _jointMin[bufferIndex])) {
        return true  // Rejeter silencieusement
    }
    
    // 4. Vérifier si le buffer est plein
    if (_maxWriteAbs <= _jointMin[bufferIndex]) {
        return false  // Réessayer plus tard
    }
    
    // 5. Remplir le buffer
    fillRobotBuffer(packet, bufferIndex)
    
    // 6. Si c'est le 2ème primer, autoriser la lecture
    if (_firstPacketPosition != -1 && position != _firstPacketPosition) {
        _waiting = false
    }
    
    return true
}
```

### 2. Remplissage du Buffer (`fillRobotBuffer`)

```cpp
void fillRobotBuffer(packet, bufferIndex) {
    // 1. Décompresser DPCM si nécessaire
    if (_decompressionBufferPosition != packet.position) {
        deDPCM16Mono(_decompressionBuffer, packet.data, packet.dataSize, carry=0)
        _decompressionBufferPosition = packet.position
    }
    
    // 2. Calculer les positions et limites
    packetPosition = packet.position
    endByte = packet.position + decompressedSize * kEOSExpansion (2)
    startByte = max(_readHeadAbs + bufferIndex*2, _jointMin[bufferIndex])
    maxWriteByte = _maxWriteAbs + bufferIndex*2
    
    // 3. Ajuster les limites (clipping)
    if (packetPosition < startByte) {
        sourceByte = (startByte - packetPosition) / 2
        ...
    }
    
    // 4. Remplir les trous dans le canal opposé avec des zéros
    if ((packetPosition & ~3) > (_jointMin[1-bufferIndex] & ~3)) {
        memset(zones non écrites, 0)
    }
    
    // 5. Interpoler le canal actuel pour les zones non encore écrites
    interpolateChannel(zones entre _jointMin[bufferIndex] et packetPosition)
    
    // 6. Copier les échantillons avec entrelacement
    copyEveryOtherSample(destination, source, numSamples)
    // Cette fonction copie en sautant un échantillon (stride=2)
    
    // 7. Mettre à jour jointMin
    _jointMin[bufferIndex] = endByte
}
```

### 3. Lecture depuis le Buffer (`readBuffer`)

```cpp
int readBuffer(int16_t *outBuffer, int numSamples) {
    // 1. Vérifier si on attend encore les primers
    if (_waiting) return 0
    
    // 2. Limiter au nombre d'échantillons disponibles
    maxSamples = (_writeHeadAbs - _readHeadAbs) / sizeof(int16_t)
    numSamples = min(numSamples, maxSamples)
    
    // 3. Interpoler les échantillons manquants AVANT de lire
    interpolateMissingSamples(numSamples)
    
    // 4. Lire depuis le buffer circulaire
    inBuffer = _loopBuffer + _readHead
    copy(inBuffer, outBuffer, numSamplesToEnd)
    
    // 5. Gérer le wrapping si nécessaire
    if (wrapped) {
        inBuffer = _loopBuffer
        copy(inBuffer, outBuffer + numSamplesToEnd, remainder)
    }
    
    // 6. Mettre à jour les positions
    _readHead = (_readHead + numBytes) % _loopBufferSize
    _readHeadAbs += numBytes
    _maxWriteAbs += numBytes  // Libérer de l'espace
    
    return numSamples
}
```

### 4. Interpolation des Canaux (`interpolateChannel`)

```cpp
void interpolateChannel(int16_t *buffer, numSamples, bufferIndex) {
    if (bufferIndex == 1) {  // ODD
        outBuffer = buffer + 1  // Commencer à l'indice 1
        inBuffer = buffer + 2   // Source à l'indice 2
        stride = 2
    } else {  // EVEN
        outBuffer = buffer      // Commencer à l'indice 0
        inBuffer = buffer + 1   // Source à l'indice 1
        stride = 2
    }
    
    while (numSamples--) {
        sample = (*inBuffer + previousSample) >> 1  // Moyenne
        previousSample = *inBuffer
        *outBuffer = sample
        inBuffer += stride
        outBuffer += stride
    }
}
```

### 5. Copie Entrelacée (`copyEveryOtherSample`)

```cpp
void copyEveryOtherSample(int16_t *out, const int16_t *in, int numSamples) {
    while (numSamples--) {
        *out = *in++
        out += 2  // Sauter un échantillon (entrelacement)
    }
}
```

## Détails Techniques Importants

### 1. Calcul de la Taille du Buffer

ScummVM utilise la formule suivante :
```
bufferSize = ((bytesPerSample * channels * sampleRate * 2000ms) / 1000ms) & ~3
           = ((2 * 1 * 22050 * 2000) / 1000) & ~3
           = (88200000 / 1000) & ~3
           = 88200 & ~3
           = 88200 bytes
```

Cela correspond à **2 secondes** d'audio à 22050Hz mono 16-bit.

### 2. Expansion par 2 (kEOSExpansion)

Chaque échantillon décompressé occupe 2 bytes (16-bit), mais il est écrit dans le buffer
avec un stride de 2, donc occupe effectivement 4 bytes dans le buffer final :
- Canal EVEN écrit aux positions 0, 4, 8, 12...
- Canal ODD écrit aux positions 2, 6, 10, 14...

### 3. Gestion du DPCM

#### Pour les Primers :
```cpp
int16_t carry = 0;
deDPCM16Mono(output, primerData, size, carry);
// Le prédicteur 'carry' est persistant entre les appels
```

#### Pour les Packets Audio :
```cpp
int16_t carry = 0;  // RESET à chaque packet
deDPCM16Mono(output, packetData, size, carry);
```

### 4. Table DPCM16

```cpp
static const uint16_t tableDPCM16[128] = {
    0x0000, 0x0008, 0x0010, 0x0020, 0x0030, 0x0040, 0x0050, 0x0060,
    // ... (128 valeurs de différences)
    0x1000, 0x1400, 0x1800, 0x1C00, 0x2000, 0x3000, 0x4000
};
```

### 5. Positions Absolues vs Positions dans le Buffer

```
Position Absolue (audioPos):
- Position dans le flux audio complet (0 → fin du fichier)
- Utilisée pour déterminer EVEN/ODD: bufferIndex = (audioPos % 4) ? 1 : 0

Position dans le Buffer:
- Position circulaire: bufferPosition = audioPos % _loopBufferSize
- Se wrappe automatiquement à 0 quand >= _loopBufferSize
```

## Différences avec l'Approche Séquentielle

### Ancienne Approche (Séquentielle)
```cpp
// Créer un grand buffer continu
vector<int16_t> monoBuffer(maxPos, 0);

// Écrire chaque packet à sa position absolue
for (packet : packets) {
    copy samples à monoBuffer[audioPos]
}

// Interpoler les trous avec plusieurs passes
while (zerosRemaining > 0) {
    for (i : monoBuffer) {
        if (monoBuffer[i] == 0) {
            interpoler avec voisins
        }
    }
}
```

**Problèmes** :
- Consommation mémoire proportionnelle à la durée totale
- Interpolation post-traitement inefficace
- Pas de streaming en temps réel possible

### Nouvelle Approche (Buffer Circulaire)
```cpp
// Buffer fixe de 2 secondes
RobotAudioStream stream(88200);

// Ajouter les packets au fur et à mesure
for (packet : packets) {
    stream.addPacket(packet);
}

// Lire par chunks
while (!stream.endOfStream()) {
    numRead = stream.readBuffer(chunk, CHUNK_SIZE);
}
```

**Avantages** :
- Mémoire constante (88200 bytes)
- Interpolation à la volée pendant la lecture
- Compatible avec le streaming temps réel
- **Fidèle à ScummVM**

## Validation

### Tests Effectués

1. **Test des Primers**
   - ✅ Ajout primer EVEN (position 0)
   - ✅ Ajout primer ODD (position 2)
   - ✅ Transition de `_waiting = true` à `false`

2. **Test des Packets Réguliers**
   - ✅ Détection automatique EVEN/ODD
   - ✅ Entrelacement correct
   - ✅ Rejet des packets obsolètes
   - ✅ Gestion du buffer plein

3. **Test de Lecture**
   - ✅ Lecture sans attente après les primers
   - ✅ Interpolation automatique
   - ✅ Wrapping du buffer circulaire
   - ✅ Mise à jour correcte des positions

4. **Test de Finalisation**
   - ✅ `finish()` arrête l'acceptation de nouveaux packets
   - ✅ `endOfStream()` retourne true quand tout est lu

### Correspondance avec ScummVM

| Aspect | ScummVM | Notre Implémentation | Status |
|--------|---------|---------------------|--------|
| Taille buffer | 88200 bytes | 88200 bytes | ✅ |
| Canal EVEN/ODD | position % 4 | position % 4 | ✅ |
| Interpolation | interpolateChannel() | interpolateChannel() | ✅ |
| Copie entrelacée | copyEveryOtherSample() | copyEveryOtherSample() | ✅ |
| Wrapping | Modulo _loopBufferSize | Modulo _loopBufferSize | ✅ |
| jointMin[2] | Oui | Oui | ✅ |
| Prédicteur DPCM | carry=0 pour packets | carry=0 pour packets | ✅ |
| Gestion primers | Position 0 et 2 | Position 0 et 2 | ✅ |

## Utilisation

### Exemple Simple

```cpp
#include "robot_audio_stream.h"

// 1. Créer le stream
RobotAudioStream stream(88200);

// 2. Ajouter les primers
RobotAudioStream::RobotAudioPacket primerEven(evenData, evenSize, 0);
stream.addPacket(primerEven);

RobotAudioStream::RobotAudioPacket primerOdd(oddData, oddSize, 2);
stream.addPacket(primerOdd);

// 3. Ajouter les packets audio
for (auto& packet : audioPackets) {
    RobotAudioStream::RobotAudioPacket pkt(packet.data, packet.size, packet.position);
    stream.addPacket(pkt);
}

// 4. Finaliser
stream.finish();

// 5. Lire l'audio
std::vector<int16_t> audioBuffer;
int16_t chunk[4096];
while (!stream.endOfStream()) {
    int numRead = stream.readBuffer(chunk, 4096);
    if (numRead == 0) break;
    audioBuffer.insert(audioBuffer.end(), chunk, chunk + numRead);
}
```

### Intégration dans RbtParser

```cpp
void RbtParser::extractAllAudio(callback) {
    RobotAudioStream audioStream(88200);
    
    // Primers
    audioStream.addPacket({evenPrimerData, evenPrimerSize, 0});
    audioStream.addPacket({oddPrimerData, oddPrimerSize, 2});
    
    // Packets per-frame
    for (frame : frames) {
        audioStream.addPacket({frameAudioData, frameAudioSize, audioPos});
    }
    
    audioStream.finish();
    
    // Lecture et callback
    while (!audioStream.endOfStream()) {
        int numRead = audioStream.readBuffer(chunk, CHUNK_SIZE);
        callback(chunk, numRead);
    }
}
```

## Références

- **ScummVM Source**: `engines/sci/video/robot_decoder.cpp` (lignes 41-407)
- **ScummVM Header**: `engines/sci/video/robot_decoder.h` (lignes 262-446)
- **DPCM Decoder**: `engines/sci/sound/decoders/sol.cpp` (lignes 60-112)
- **Documentation**: `docs/AUDIO_ENCODING.md`

## Conclusion

Cette implémentation reproduit **fidèlement** le comportement du buffer circulaire audio de ScummVM, garantissant :

1. ✅ Décodage audio identique à ScummVM
2. ✅ Gestion mémoire efficace (buffer fixe)
3. ✅ Support complet des primers et packets
4. ✅ Interpolation automatique correcte
5. ✅ Entrelacement EVEN/ODD conforme
6. ✅ Compatibilité avec le streaming temps réel

Le code est prêt pour l'extraction audio de fichiers Robot (.rbt) de manière fiable et performante.
