#include "robot_audio_stream.h"
#include "formats/dpcm.h"
#include <cstring>
#include <algorithm>
#include <cstdio>

// Fonction externe pour la décompression DPCM
// extern void deDPCM16Mono(int16_t *out, const uint8_t *in, uint32_t numBytes, int16_t &sample);

/**
 * Interpole un canal en calculant la moyenne entre les échantillons adjacents.
 * 
 * RÔLE: Reconstruction de l'entrelacement stéréo dans le buffer circulaire.
 * Cette fonction remplit les positions intermédiaires (stride de 4) en interpolant
 * linéairement entre les samples d'un même canal (EVEN ou ODD).
 * 
 * NOTE: N'effectue PAS d'élongation temporelle! Chaque packet décompressé DPCM
 * contient déjà exactement 2205 samples = 100ms, parfaitement synchronisé avec
 * les frames vidéo (10 fps). L'interpolation sert uniquement à reconstruire
 * les canaux gauche/droite entrelacés dans le buffer avec stride de 4.
 */
static void interpolateChannel(int16_t *buffer, int32_t numSamples, const int8_t bufferIndex) {
    if (numSamples <= 0) {
        return;
    }

    int16_t *inBuffer, *outBuffer;
    int16_t sample, previousSample;

    if (bufferIndex) {
        // Canal ODD (indices 1, 3, 5, 7, ...)
        outBuffer = buffer + 1;
        inBuffer = buffer + 2;
        previousSample = sample = *buffer;
        --numSamples;
    } else {
        // Canal EVEN (indices 0, 2, 4, 6, ...)
        outBuffer = buffer;
        inBuffer = buffer + 1;
        previousSample = sample = *inBuffer;
    }

    while (numSamples--) {
        sample = (*inBuffer + previousSample) >> 1;
        previousSample = *inBuffer;
        *outBuffer = sample;
        inBuffer += RobotAudioStream::kEOSExpansion;
        outBuffer += RobotAudioStream::kEOSExpansion;
    }

    if (bufferIndex) {
        *outBuffer = sample;
    }
}

/**
 * Copie un échantillon sur deux depuis le buffer source vers le buffer de destination.
 * Utilisé pour créer l'entrelacement des canaux EVEN/ODD.
 */
static void copyEveryOtherSample(int16_t *out, const int16_t *in, int numSamples) {
    while (numSamples--) {
        *out = *in++;
        out += 2;  // Stride de 2 pour l'entrelacement
    }
}

RobotAudioStream::RobotAudioStream(const int32_t bufferSize) :
    _loopBuffer((uint8_t *)malloc(bufferSize)),
    _loopBufferSize(bufferSize),
    _readHead(0),
    _readHeadAbs(0),
    _maxWriteAbs(0),
    _writeHeadAbs(0),
    _decompressionBuffer(nullptr),
    _decompressionBufferSize(0),
    _decompressionBufferPosition(-1),
    _waiting(true),
    _finished(false),
    _firstPacketPosition(-1) {
    
    _jointMin[0] = 0;
    _jointMin[1] = 0;
    
    if (!_loopBuffer) {
        std::fprintf(stderr, "RobotAudioStream: Failed to allocate loop buffer of size %d\n", bufferSize);
    }
}

RobotAudioStream::~RobotAudioStream() {
    free(_loopBuffer);
    free(_decompressionBuffer);
}

bool RobotAudioStream::addPacket(const RobotAudioPacket &packet) {
    if (_finished) {
        std::fprintf(stderr, "Packet %d sent to finished robot audio stream\n", packet.position);
        return false;
    }

    // `packet.position` est la position décompressée (doublée) du paquet,
    // donc les valeurs de `position` seront toujours divisibles soit par 2 (even) soit par 4 (odd).
    const int8_t bufferIndex = packet.position % 4 ? 1 : 0;

    // Paquet 0 est le premier primer, paquet 2 est le second primer, paquet 4+
    // sont des données audio régulières
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

    const int32_t packetEndByte = packet.position + (packet.dataSize * (sizeof(int16_t) + kEOSExpansion));

    // Déjà lu tout le chemin passé ce paquet (ou déjà écrit des échantillons valides
    // à ce canal tout le chemin passé ce paquet), donc le rejeter
    if (packetEndByte <= std::max(_readHeadAbs, _jointMin[bufferIndex])) {
        // fprintf(stderr, "Rejecting packet %d, read past %d / %d\n", packet.position, _readHeadAbs, _jointMin[bufferIndex]);
        return true;
    }

    // Le buffer circulaire est plein, donc dire à l'appelant de renvoyer le paquet plus tard
    if (_maxWriteAbs <= _jointMin[bufferIndex]) {
        // fprintf(stderr, "  addPacket REJECTED (buffer full): pos=%d maxWrite=%d jointMin[%d]=%d\n", 
        //         packet.position, _maxWriteAbs, bufferIndex, _jointMin[bufferIndex]);
        return false;
    }

    fillRobotBuffer(packet, bufferIndex);

    // Ce paquet est le second primer, donc permettre la lecture de commencer
    if (_firstPacketPosition != -1 && _firstPacketPosition != packet.position) {
        // fprintf(stderr, "Done waiting. Robot audio begins\n");
        _waiting = false;
        _firstPacketPosition = -1;
    }

    // Seulement une partie du paquet a pu être lue dans le buffer circulaire avant qu'il soit
    // plein, donc dire à l'appelant de renvoyer le paquet plus tard
    if (packetEndByte > _maxWriteAbs) {
        // fprintf(stderr, "Partial read of packet %d (%d / %d)\n", packet.position, packetEndByte - _maxWriteAbs, packetEndByte - packet.position);
        return false;
    }

    return true;
}

void RobotAudioStream::fillRobotBuffer(const RobotAudioPacket &packet, const int8_t bufferIndex) {
    int32_t sourceByte = 0;

    // Décompresser le paquet si ce n'est pas déjà fait
    const int32_t decompressedSize = packet.dataSize * sizeof(int16_t);
    if (_decompressionBufferPosition != packet.position) {
        if (decompressedSize != _decompressionBufferSize) {
            _decompressionBuffer = (uint8_t *)realloc(_decompressionBuffer, decompressedSize);
            _decompressionBufferSize = decompressedSize;
        }

        int16_t carry = 0;
        deDPCM16Mono((int16_t *)_decompressionBuffer, packet.data, packet.dataSize, carry);
        _decompressionBufferPosition = packet.position;
    }

    int32_t numBytes = decompressedSize;
    int32_t packetPosition = packet.position;
    int32_t endByte = packet.position + decompressedSize * kEOSExpansion;
    int32_t startByte = std::max(_readHeadAbs + bufferIndex * 2, _jointMin[bufferIndex]);
    int32_t maxWriteByte = _maxWriteAbs + bufferIndex * 2;

    // Ajuster si le paquet commence avant la position de départ autorisée
    if (packetPosition < startByte) {
        sourceByte = (startByte - packetPosition) / kEOSExpansion;
        numBytes -= sourceByte;
        packetPosition = startByte;
    }
    if (packetPosition > maxWriteByte) {
        numBytes += (packetPosition - maxWriteByte) / kEOSExpansion;
        packetPosition = maxWriteByte;
    }
    if (endByte > maxWriteByte) {
        numBytes -= (endByte - maxWriteByte) / kEOSExpansion;
        endByte = maxWriteByte;
    }

    const int32_t maxJointMin = std::max(_jointMin[0], _jointMin[1]);
    if (endByte > maxJointMin) {
        _writeHeadAbs += endByte - maxJointMin;
    }

    if (packetPosition > _jointMin[bufferIndex]) {
        int32_t packetEndByte = packetPosition % _loopBufferSize;
        int32_t numBytesToEnd;

        // Remplir avec des zéros le canal opposé pour les zones non encore écrites
        if ((packetPosition & ~3) > (_jointMin[1 - bufferIndex] & ~3)) {
            int32_t targetBytePosition = _jointMin[1 - bufferIndex] % _loopBufferSize;
            if (targetBytePosition >= packetEndByte) {
                numBytesToEnd = _loopBufferSize - targetBytePosition;
                memset(_loopBuffer + targetBytePosition, 0, numBytesToEnd);
                targetBytePosition = (1 - bufferIndex) ? 2 : 0;
            }
            numBytesToEnd = packetEndByte - targetBytePosition;
            if (numBytesToEnd > 0) {
                memset(_loopBuffer + targetBytePosition, 0, numBytesToEnd);
            }
        }

        // Interpoler le canal actuel pour les zones non encore écrites
        int32_t targetBytePosition = _jointMin[bufferIndex] % _loopBufferSize;
        if (targetBytePosition >= packetEndByte) {
            numBytesToEnd = _loopBufferSize - targetBytePosition;
            interpolateChannel((int16_t *)(_loopBuffer + targetBytePosition), 
                             numBytesToEnd / (sizeof(int16_t) + kEOSExpansion), 0);
            targetBytePosition = bufferIndex ? 2 : 0;
        }
        numBytesToEnd = packetEndByte - targetBytePosition;
        if (numBytesToEnd > 0) {
            interpolateChannel((int16_t *)(_loopBuffer + targetBytePosition), 
                             numBytesToEnd / (sizeof(int16_t) + kEOSExpansion), 0);
        }
    }

    // Copier les échantillons décompressés dans le buffer circulaire avec entrelacement
    if (numBytes > 0) {
        int32_t targetBytePosition = packetPosition % _loopBufferSize;
        int32_t packetEndByte = endByte % _loopBufferSize;
        int32_t numBytesToEnd = 0;

        // Gérer le wrapping du buffer circulaire
        if (targetBytePosition >= packetEndByte) {
            numBytesToEnd = (_loopBufferSize - (targetBytePosition & ~3)) / kEOSExpansion;
            copyEveryOtherSample((int16_t *)(_loopBuffer + targetBytePosition), 
                               (int16_t *)(_decompressionBuffer + sourceByte), 
                               numBytesToEnd / kEOSExpansion);
            targetBytePosition = bufferIndex ? 2 : 0;
        }
        copyEveryOtherSample((int16_t *)(_loopBuffer + targetBytePosition), 
                           (int16_t *)(_decompressionBuffer + sourceByte + numBytesToEnd), 
                           (packetEndByte - targetBytePosition) / (sizeof(int16_t) + kEOSExpansion));
    }
    
    _jointMin[bufferIndex] = endByte;
}

void RobotAudioStream::interpolateMissingSamples(const int32_t numSamples) {
    // Reconstruit les canaux stéréo entrelacés en interpolant les positions intermédiaires
    // 
    // Chaque packet DPCM décompressé contient 2205 samples = 100ms (durée exacte d'une frame vidéo)
    // Cette fonction ne fait PAS d'élongation temporelle, elle reconstruit simplement
    // l'entrelacement stéréo dans le buffer circulaire avec stride de 4.
    // 
    // IMPORTANT: numBytes = numSamples * (sizeof(int16_t) + kEOSExpansion)
    // car le buffer est entrelacé avec un stride de kEOSExpansion
    const int32_t numBytes = numSamples * (sizeof(int16_t) + kEOSExpansion);
    const int32_t readHeadPosition = _readHead % _loopBufferSize;
    const int32_t readHeadEndPosition = (_readHead + numBytes) % _loopBufferSize;

    // Interpoler le canal EVEN (0)
    if (_jointMin[0] < _readHeadAbs + numBytes) {
        if (readHeadPosition < readHeadEndPosition) {
            interpolateChannel((int16_t *)(_loopBuffer + readHeadPosition), numSamples, 0);
        } else {
            const int32_t samplesBeforeEnd = (_loopBufferSize - readHeadPosition) / (sizeof(int16_t) + kEOSExpansion);
            interpolateChannel((int16_t *)(_loopBuffer + readHeadPosition), samplesBeforeEnd, 0);
            interpolateChannel((int16_t *)_loopBuffer, numSamples - samplesBeforeEnd, 0);
        }
    }

    // Interpoler le canal ODD (1)
    if (_jointMin[1] < _readHeadAbs + numBytes) {
        if (readHeadPosition < readHeadEndPosition) {
            interpolateChannel((int16_t *)(_loopBuffer + readHeadPosition), numSamples, 1);
        } else {
            const int32_t samplesBeforeEnd = (_loopBufferSize - readHeadPosition) / (sizeof(int16_t) + kEOSExpansion);
            interpolateChannel((int16_t *)(_loopBuffer + readHeadPosition), samplesBeforeEnd, 1);
            interpolateChannel((int16_t *)_loopBuffer, numSamples - samplesBeforeEnd, 1);
        }
    }
}

int RobotAudioStream::readBuffer(int16_t *outBuffer, int numSamples) {
    if (_waiting) {
        return 0;
    }

    // Calculer le nombre maximal d'échantillons disponibles
    const int maxNumSamples = (_writeHeadAbs - _readHeadAbs) / sizeof(int16_t);
    numSamples = std::min(numSamples, maxNumSamples);

    if (!numSamples) {
        return 0;
    }

    // Interpoler les échantillons manquants avant la lecture
    interpolateMissingSamples(numSamples);

    int16_t *inBuffer = (int16_t *)(_loopBuffer + _readHead);

    const int numSamplesToEnd = (_loopBufferSize - _readHead) / sizeof(int16_t);

    int numSamplesToRead = std::min(numSamples, numSamplesToEnd);
    std::copy(inBuffer, inBuffer + numSamplesToRead, outBuffer);

    // Gérer le wrapping du buffer circulaire
    if (numSamplesToRead < numSamples) {
        inBuffer = (int16_t *)_loopBuffer;
        outBuffer += numSamplesToRead;
        numSamplesToRead = numSamples - numSamplesToRead;
        std::copy(inBuffer, inBuffer + numSamplesToRead, outBuffer);
    }

    const int32_t numBytes = numSamples * sizeof(int16_t);

    _readHead += numBytes;
    if (_readHead >= _loopBufferSize) {
        _readHead -= _loopBufferSize;
    }
    _readHeadAbs += numBytes;
    _maxWriteAbs += numBytes;

    return numSamples;
}

void RobotAudioStream::finish(int32_t endPosition) {
    _finished = true;
    
    // Calculer la position de fin effective
    const int32_t maxJointMin = std::max(_jointMin[0], _jointMin[1]);
    
    // Utiliser la plus grande valeur entre endPosition et maxJointMin
    int32_t targetEnd = std::max(endPosition, maxJointMin);
    
    // IMPORTANT: Pour l'extraction complète, on doit permettre la lecture
    // bien au-delà de la fin des packets réels, car ScummVM continue
    // à interpoler les données manquantes pour maintenir la synchronisation.
    // On ajoute une marge généreuse pour capturer toute l'interpolation.
    targetEnd += 441000;  // ~10 sec @ 22050Hz stéréo (4 bytes/sample)
    
    // Forcer _writeHeadAbs pour permettre la lecture jusqu'à targetEnd
    if (targetEnd > _writeHeadAbs) {
        _writeHeadAbs = targetEnd;
        _maxWriteAbs = targetEnd;
    }
}
