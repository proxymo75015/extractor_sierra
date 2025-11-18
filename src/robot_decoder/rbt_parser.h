#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
#include <functional>

class RbtParser {
public:
    RbtParser(FILE *f);
    ~RbtParser();

    bool parseHeader();
    void dumpMetadata(const char *outDir);
    size_t getNumFrames() const;
    // Frame audio helpers (return 0 if none)
    int32_t getFrameAudioPosition(size_t frameIndex);
    int32_t getFrameAudioSize(size_t frameIndex);
    bool extractFrame(size_t frameIndex, const char *outDir);
    bool hasAudio() const { return _hasAudio; }
    void extractAllAudio(std::function<void(const int16_t*, size_t)> cb);

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
