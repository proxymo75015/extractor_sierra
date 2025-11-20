# Implémentation audio ScummVM - Référence

> Analyse détaillée de l'implémentation audio dans ScummVM  
> Source : `scummvm/engines/sci/video/robot_decoder.cpp` et `sol.cpp`

## Architecture

### RobotAudioStream

Classe principale gérant le streaming audio avec buffer circulaire.

```cpp
class RobotAudioStream {
    byte *_loopBuffer;          // Buffer circulaire principal
    int32 _loopBufferSize;      // Taille : 88200 bytes (2s @ 22050Hz stereo)
    
    byte *_decompressionBuffer; // Buffer temporaire pour DPCM
    int32 _decompressionBufferSize;
    int32 _decompressionBufferPosition;
    
    int32 _readHead;            // Position de lecture (relative)
    int32 _readHeadAbs;         // Position de lecture (absolue)
    int32 _writeHeadAbs;        // Position d'écriture (absolue)
    int32 _maxWriteAbs;         // Limite d'écriture
    int32 _jointMin[2];         // Positions min pour EVEN/ODD
    
    bool _waiting;              // En attente des primers
    bool _finished;             // Flux terminé
    int32 _firstPacketPosition; // Position du premier primer
};
```

### Constantes

```cpp
enum {
    kEOSExpansion = 2  // Multiplicateur pour entrelacement (every other sample)
};
```

## Algorithme DPCM16

### Table de décompression

```cpp
static const uint16 tableDPCM16[128] = {
    0x0000, 0x0008, 0x0010, 0x0020, 0x0030, 0x0040, 0x0050, 0x0060,
    0x0070, 0x0080, 0x0090, 0x00A0, 0x00B0, 0x00C0, 0x00D0, 0x00E0,
    // ... 128 valeurs au total
    0x3000, 0x4000
};
```

### Décodage

```cpp
static void deDPCM16Channel(int16 *out, int16 &sample, uint8 delta) {
    int32 nextSample = sample;
    
    if (delta & 0x80) {
        nextSample -= tableDPCM16[delta & 0x7f];
    } else {
        nextSample += tableDPCM16[delta];
    }
    
    // Emulation du overflow 16-bit x86
    if (nextSample > 32767) {
        nextSample -= 65536;  // Wrapping (pas clamping!)
    } else if (nextSample < -32768) {
        nextSample += 65536;
    }
    
    *out = sample = nextSample;
}

void deDPCM16Mono(int16 *out, const byte *in, const uint32 numBytes, int16 &sample) {
    for (uint32 i = 0; i < numBytes; ++i) {
        const uint8 delta = *in++;
        deDPCM16Channel(out++, sample, delta);
    }
}
```

**Note** : ScummVM utilise le **wrapping** pour émuler le comportement x86 original. Notre implémentation utilise le **clamping** qui produit une meilleure qualité audio.

## Gestion des packets

### addPacket()

```cpp
bool RobotAudioStream::addPacket(const RobotAudioPacket &packet) {
    const int8 bufferIndex = packet.position % 4 ? 1 : 0;
    
    // Primers (position 0 ou 2)
    if (packet.position <= 2 && _firstPacketPosition == -1) {
        _readHead = 0;
        _readHeadAbs = 0;
        _maxWriteAbs = _loopBufferSize;
        _writeHeadAbs = 2;
        _jointMin[0] = 0;
        _jointMin[1] = 2;
        _waiting = true;
        _finished = false;
        _firstPacketPosition = packet.position;
        fillRobotBuffer(packet, bufferIndex);
        return true;
    }
    
    const int32 packetEndByte = packet.position + 
                                (packet.dataSize * (sizeof(int16) + kEOSExpansion));
    
    // Déjà lu ou écrit au-delà de ce packet → rejeter
    if (packetEndByte <= MAX(_readHeadAbs, _jointMin[bufferIndex])) {
        return true;  // Packet rejeté silencieusement
    }
    
    // Buffer plein → demander de renvoyer plus tard
    if (_maxWriteAbs <= _jointMin[bufferIndex]) {
        return false;  // Buffer plein
    }
    
    fillRobotBuffer(packet, bufferIndex);
    
    // Deuxième primer reçu → autoriser la lecture
    if (_firstPacketPosition != -1 && _firstPacketPosition != packet.position) {
        _waiting = false;
        _firstPacketPosition = -1;
    }
    
    return true;
}
```

### fillRobotBuffer()

```cpp
void RobotAudioStream::fillRobotBuffer(const RobotAudioPacket &packet, 
                                       const int8 bufferIndex) {
    int32 sourceByte = 0;
    const int32 decompressedSize = packet.dataSize * sizeof(int16);
    
    // 1. Décompression DPCM (si pas déjà fait pour cette position)
    if (_decompressionBufferPosition != packet.position) {
        if (decompressedSize != _decompressionBufferSize) {
            _decompressionBuffer = (byte *)realloc(_decompressionBuffer, decompressedSize);
            _decompressionBufferSize = decompressedSize;
        }
        
        int16 carry = 0;
        deDPCM16Mono((int16 *)_decompressionBuffer, packet.data, 
                     packet.dataSize, carry);
        _decompressionBufferPosition = packet.position;
    }
    
    // 2. Calcul des limites de copie
    int32 numBytes = decompressedSize;
    int32 packetPosition = packet.position;
    int32 endByte = packet.position + decompressedSize * kEOSExpansion;
    int32 startByte = MAX(_readHeadAbs + bufferIndex * 2, _jointMin[bufferIndex]);
    int32 maxWriteByte = _maxWriteAbs + bufferIndex * 2;
    
    // Ajuster si on commence après le début du packet (skip runway)
    if (packetPosition < startByte) {
        sourceByte = (startByte - packetPosition) / kEOSExpansion;
        numBytes -= sourceByte;
        packetPosition = startByte;
    }
    
    // Ajuster si on dépasse la limite d'écriture
    if (endByte > maxWriteByte) {
        numBytes -= (endByte - maxWriteByte) / kEOSExpansion;
        endByte = maxWriteByte;
    }
    
    // 3. Mettre à jour le writeHead
    const int32 maxJointMin = MAX(_jointMin[0], _jointMin[1]);
    if (endByte > maxJointMin) {
        _writeHeadAbs += endByte - maxJointMin;
    }
    
    // 4. Interpoler les gaps dans le canal opposé
    if (packetPosition > _jointMin[bufferIndex]) {
        // ... code d'interpolation complexe ...
    }
    
    // 5. Copier les samples (every other sample = entrelacement)
    if (numBytes > 0) {
        int32 targetBytePosition = packetPosition % _loopBufferSize;
        int32 packetEndByte = endByte % _loopBufferSize;
        
        // Gestion du wrap-around du buffer circulaire
        if (targetBytePosition >= packetEndByte) {
            int32 numBytesToEnd = (_loopBufferSize - (targetBytePosition & ~3)) / kEOSExpansion;
            copyEveryOtherSample((int16 *)(_loopBuffer + targetBytePosition),
                               (int16 *)(_decompressionBuffer + sourceByte),
                               numBytesToEnd / kEOSExpansion);
            targetBytePosition = bufferIndex ? 2 : 0;
        }
        
        copyEveryOtherSample((int16 *)(_loopBuffer + targetBytePosition),
                           (int16 *)(_decompressionBuffer + sourceByte + numBytesToEnd),
                           (packetEndByte - targetBytePosition) / (sizeof(int16) + kEOSExpansion));
    }
    
    _jointMin[bufferIndex] = endByte;
}
```

## Fonctions utilitaires

### copyEveryOtherSample()

```cpp
static void copyEveryOtherSample(int16 *out, const int16 *in, int numSamples) {
    while (numSamples--) {
        *out = *in++;
        out += 2;  // Sauter 1 sample (entrelacement EVEN/ODD)
    }
}
```

### interpolateChannel()

```cpp
static void interpolateChannel(int16 *buffer, int32 numSamples, const int8 bufferIndex) {
    if (numSamples <= 0) return;
    
    int16 *inBuffer, *outBuffer;
    int16 sample, previousSample;
    
    // Initialisation selon le canal (EVEN ou ODD)
    if (bufferIndex) {  // ODD
        outBuffer = buffer + 1;
        inBuffer = buffer + 2;
        previousSample = sample = *buffer;
        --numSamples;
    } else {  // EVEN
        outBuffer = buffer;
        inBuffer = buffer + 1;
        previousSample = sample = *inBuffer;
    }
    
    // Interpolation : moyenne des échantillons voisins
    while (numSamples--) {
        sample = (*inBuffer + previousSample) >> 1;
        previousSample = *inBuffer;
        *outBuffer = sample;
        inBuffer += kEOSExpansion;   // +2
        outBuffer += kEOSExpansion;  // +2
    }
    
    if (bufferIndex) {
        *outBuffer = sample;
    }
}
```

## Lecture du flux

### readBuffer()

```cpp
int RobotAudioStream::readBuffer(int16 *buffer, const int numSamples) {
    const int startSample = _readHead;
    const int16 *const bufferStart = (const int16 *)_loopBuffer + startSample;
    
    // Copier depuis le buffer circulaire
    memcpy(buffer, bufferStart, numSamples * sizeof(int16));
    
    _readHead = (_readHead + numSamples) % (_loopBufferSize / sizeof(int16));
    _readHeadAbs += numSamples;
    
    // Mettre à jour maxWriteAbs pour permettre plus d'écriture
    const int32 numBytes = numSamples * sizeof(int16);
    if (numBytes > 0) {
        _maxWriteAbs += numBytes;
    }
    
    return numSamples;
}
```

## Points clés

1. **Buffer circulaire** : Permet le streaming asynchrone vidéo/audio
2. **Entrelacement** : EVEN/ODD écrits à chaque 2ème position
3. **Interpolation** : Comble les gaps si un canal est en retard
4. **Wrapping** : ScummVM wrappe les valeurs (émulation x86)
5. **Runway** : Géré implicitement par `sourceByte` et positions

## Différences avec notre implémentation

| Aspect | ScummVM | Notre projet |
|--------|---------|--------------|
| **Buffer** | Circulaire (streaming) | Linéaire (offline) |
| **DPCM overflow** | Wrapping | Clamping ✅ |
| **Interpolation** | Par canal (complexe) | Multi-pass global ✅ |
| **Usage** | Playback temps-réel | Extraction batch |

## Références

- `scummvm/engines/sci/video/robot_decoder.cpp` (lignes 40-290)
- `scummvm/engines/sci/sound/decoders/sol.cpp` (lignes 40-100)
