#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include <functional>

class RbtParser {
public:
    RbtParser(FILE *f);
    ~RbtParser();

    bool parseHeader();
    void dumpMetadata(const char *outDir);
    size_t getNumFrames() const;
    int16_t getFrameRate() const { return _frameRate; }
    // Frame audio helpers (return 0 if none)
    int32_t getFrameAudioPosition(size_t frameIndex);
    int32_t getFrameAudioSize(size_t frameIndex);
    bool extractFrame(size_t frameIndex, const char *outDir);
    bool hasAudio() const { return _hasAudio; }
    
    /**
     * Active le mode composition sur canvas avec coordonnées
     * @param x Position X du Robot sur le canvas
     * @param y Position Y du Robot sur le canvas
     * @param canvasWidth Largeur du canvas (défaut: 630 pour Phantasmagoria)
     * @param canvasHeight Hauteur du canvas (défaut: 450 pour Phantasmagoria)
     */
    void setCanvasMode(int16_t x, int16_t y, uint16_t canvasWidth = 630, uint16_t canvasHeight = 450);
    
    /**
     * Désactive le mode canvas (retour au mode crop serré)
     */
    void disableCanvasMode();
    
    /**
     * Calcule les dimensions maximales du Robot (scan toutes les frames)
     * Utilisé en mode crop pour avoir un canvas cohérent
     */
    void computeMaxDimensions();
    
    /**
     * Récupère les dimensions maximales calculées
     * @return true si dimensions disponibles, sinon false
     */
    bool getMaxDimensions(uint16_t& width, uint16_t& height) const {
        if (_maxDimensionsComputed && _maxCelWidth > 0 && _maxCelHeight > 0) {
            width = _maxCelWidth;
            height = _maxCelHeight;
            return true;
        }
        return false;
    }
    
    /**
     * Extrait les pixels indexés d'une frame (sans conversion RGB)
     * @return true si succès, pixels stockés dans outPixels (320x240)
     */
    bool extractFramePixels(size_t frameIndex, std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight);
    bool extractFramePixels(size_t frameIndex, std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight, int& outOffsetX, int& outOffsetY);
    
    /**
     * Extrait les pixels d'une frame avec métadonnées cel complètes (celX, celY)
     */
    bool extractFramePixelsWithMetadata(size_t frameIndex, std::vector<uint8_t>& outPixels, 
                                        int& outWidth, int& outHeight, 
                                        int& outCelX, int& outCelY);
    
    /**
     * Récupère la palette RGB courante
     */
    const std::vector<uint8_t>& getPalette() const { return _paletteData; }
    
    /**
     * Extrait l'audio complet au format WAV (22050 Hz mono)
     * 
     * Format audio Robot:
     * - 2 canaux DPCM16 (EVEN/ODD) à 11025 Hz chacun
     * - Entrelacés pour produire 22050 Hz mono
     * - Chaque paquet contient 8 bytes de "runway" DPCM ignorés
     * 
     * @param outDir    Dossier de sortie (fichier audio.wav créé)
     * @param maxFrames Nombre max de frames à extraire (0 = toutes)
     * 
     * Références:
     * - FORMAT_RBT_DOCUMENTATION.md (section "Format audio")
     * - DPCM16_DECODER_DOCUMENTATION.md
     */
    void extractAudio(const char *outDir, size_t maxFrames = 0);
    void extractAudio(const std::string& outputWavPath, size_t maxFrames = 0);

private:
    FILE *_f;
    bool _bigEndian = false;
    uint16_t _version = 0;
    uint16_t _audioBlockSize = 0;
    bool _hasAudio = false;
    uint16_t _numFramesTotal = 0;
    uint16_t _paletteSize = 0;
    uint16_t _primerReservedSize = 0;
    int16_t _primerZeroCompressFlag = 0;
    int16_t _frameRate = 0;
    int16_t _isHiRes = 0;
    int16_t _maxSkippablePackets = 0;
    int16_t _maxCelsPerFrame = 0;
    int32_t _totalPrimerSize = 0;
    int16_t _primerCompressionType = 0;
    int32_t _evenPrimerSize = 0;
    int32_t _oddPrimerSize = 0;
    long _primerPosition = 0;
    // raw primer buffers (if present and read during header parse)
    std::vector<uint8_t> _primerEvenRaw;
    std::vector<uint8_t> _primerOddRaw;
    std::vector<int32_t> _cueTimes;
    std::vector<uint16_t> _cueValues;
    std::vector<uint32_t> _videoSizes;
    std::vector<uint32_t> _recordPositions;
    std::vector<uint32_t> _packetSizes;
    std::vector<uint8_t> _paletteData;
    // Offset within the containing file/archive (ScummVM uses this when aligning).
    // For a raw, standalone `.rbt` file the resource is stored at the start
    // of the file, so the default `_fileOffset` should be 0.
    long _fileOffset = 0;
    
    // Mode canvas pour composition avec coordonnées
    bool _useCanvasMode = false;
    int16_t _canvasX = 0;
    int16_t _canvasY = 0;
    uint16_t _canvasWidth = 630;
    uint16_t _canvasHeight = 450;
    
    // Dimensions maximales pour mode crop cohérent
    uint16_t _maxCelWidth = 0;
    uint16_t _maxCelHeight = 0;
    bool _maxDimensionsComputed = false;

    // helpers
    uint16_t readUint16LE();
    uint16_t readUint16BE();
    int32_t readSint32(bool asBE=false);
    uint32_t readUint32(bool asBE=false);
    bool seekSet(size_t pos);
    size_t tell()
    {
        return ftell(_f);
    }
    // ScummVM-like cel creation helpers (version 5/6)
    uint32_t createCel5(const uint8_t *rawVideoData, const int16_t screenItemIndex, const char *outDir, size_t frameIndex);
    void createCels5(const uint8_t *rawVideoData, const int16_t numCels, const char *outDir, size_t frameIndex);
};
