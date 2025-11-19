#pragma once

#include <cstdint>
#include <vector>
#include <cstdlib>

/**
 * RobotAudioStream - Implémentation fidèle du buffer circulaire audio de ScummVM
 * 
 * Cette classe reproduit exactement le comportement de ScummVM pour le décodage
 * audio Robot, incluant:
 * - Buffer circulaire avec gestion des positions absolues
 * - Deux canaux entrelacés (EVEN/ODD) formant un flux mono 22050 Hz
 * - Interpolation linéaire pour reconstruire l'entrelacement stéréo (stride de 4)
 * - Gestion des primers et packets audio
 * 
 * SYNCHRONISATION TEMPORELLE:
 * Les packets DPCM décompressés contiennent exactement 2205 samples (100ms),
 * soit la durée exacte d'une frame vidéo (10 fps). Aucune élongation temporelle
 * n'est nécessaire - la synchronisation est intrinsèque au format DPCM de Sierra.
 */
class RobotAudioStream {
public:
    enum {
        /**
         * Taux d'échantillonnage utilisé pour tout l'audio robot.
         */
        kRobotSampleRate = 22050,

        /**
         * Multiplicateur pour la taille d'un paquet qui est expansé en écrivant
         * à chaque autre octet du buffer de destination.
         */
        kEOSExpansion = 2
    };

    /**
     * Un paquet audio compressé provenant d'un flux de données Robot.
     */
    struct RobotAudioPacket {
        /**
         * Données audio DPCM compressées brutes.
         */
        const uint8_t *data;

        /**
         * La taille des données audio compressées, en octets.
         */
        int dataSize;

        /**
         * La position non compressée, relative au fichier, de ce paquet audio.
         */
        int position;

        RobotAudioPacket(const uint8_t *data_, const int dataSize_, const int position_) :
            data(data_), dataSize(dataSize_), position(position_) {}
    };

    RobotAudioStream(const int32_t bufferSize);
    ~RobotAudioStream();

    /**
     * Ajoute un nouveau paquet audio au flux.
     * @returns `true` si le paquet audio a été entièrement consommé, sinon `false`.
     */
    bool addPacket(const RobotAudioPacket &packet);

    /**
     * Empêche l'ajout de paquets audio supplémentaires au flux audio.
     * @param endPosition Position absolue de fin (optionnel, 0 = auto)
     */
    void finish(int32_t endPosition = 0);

    /**
     * Lit des échantillons depuis le buffer circulaire.
     * @param outBuffer Buffer de sortie pour les échantillons PCM 16-bit mono
     * @param numSamples Nombre d'échantillons à lire
     * @returns Nombre d'échantillons effectivement lus
     */
    int readBuffer(int16_t *outBuffer, int numSamples);

    /**
     * Vérifie si le flux a terminé et qu'il n'y a plus de données.
     */
    bool endOfData() const {
        return _readHeadAbs >= _writeHeadAbs;
    }

    bool endOfStream() const {
        return _finished && endOfData();
    }

    /**
     * Retourne la position de lecture actuelle (pour diagnostics).
     */
    int32_t getReadPosition() const { return _readHeadAbs; }
    int32_t getWritePosition() const { return _writeHeadAbs; }

private:
    /**
     * Buffer circulaire pour la lecture. Contient des échantillons PCM 16-bit décompressés.
     */
    uint8_t *_loopBuffer;

    /**
     * La taille du buffer circulaire, en octets.
     */
    int32_t _loopBufferSize;

    /**
     * La position de la tête de lecture dans le buffer circulaire, en octets.
     */
    int32_t _readHead;

    /**
     * La position absolue de la tête de lecture, en octets non compressés.
     */
    int32_t _readHeadAbs;

    /**
     * La position de fichier la plus haute pouvant être bufferisée, en octets non compressés.
     */
    int32_t _maxWriteAbs;

    /**
     * La position de fichier la plus haute, en octets non compressés, qui a été écrite
     * dans le flux. Ceci est différent de `_maxWriteAbs`, qui est la position non
     * compressée la plus haute qui *peut* être écrite maintenant.
     */
    int32_t _writeHeadAbs;

    /**
     * La position de fichier la plus haute, en octets non compressés, qui a été écrite
     * aux côtés even & odd du flux.
     *
     * Index 0 correspond au côté 'even'; index 1 correspond au côté 'odd'.
     */
    int32_t _jointMin[2];

    /**
     * Quand `true`, le flux attend que tous les blocs primer soient reçus
     * avant de permettre la lecture.
     */
    bool _waiting;

    /**
     * Quand `true`, le flux n'acceptera plus de blocs audio.
     */
    bool _finished;

    /**
     * La position non compressée du premier paquet de données robot. Utilisée pour
     * décider si tous les blocs primer ont été reçus et si le flux doit être démarré.
     */
    int32_t _firstPacketPosition;

    /**
     * Buffer de décompression, utilisé pour stocker temporairement un bloc d'audio
     * décompressé.
     */
    uint8_t *_decompressionBuffer;

    /**
     * La taille du buffer de décompression, en octets.
     */
    int32_t _decompressionBufferSize;

    /**
     * La position du paquet actuellement dans le buffer de décompression. Utilisée pour
     * éviter de re-décompresser des données audio déjà décompressées lors d'une lecture
     * partielle de paquet.
     */
    int32_t _decompressionBufferPosition;

    /**
     * Calcule les plages absolues pour les nouveaux remplissages dans le buffer circulaire.
     */
    void fillRobotBuffer(const RobotAudioPacket &packet, const int8_t bufferIndex);

    /**
     * Reconstruit l'entrelacement stéréo en interpolant `numSamples` échantillons 
     * depuis la tête de lecture, pour les canaux qui n'ont pas de données réelles
     * aux positions intermédiaires (stride de 4).
     * 
     * NOTE: Les packets DPCM décompressés contiennent déjà exactement 2205 samples (100ms),
     * parfaitement synchronisés avec les frames vidéo. Cette fonction ne fait PAS 
     * d'élongation temporelle, elle reconstruit uniquement l'entrelacement stéréo
     * dans le buffer circulaire.
     */
    void interpolateMissingSamples(const int32_t numSamples);
};
