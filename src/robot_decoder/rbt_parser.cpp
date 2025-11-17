#include "rbt_parser.h"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>

#include "lzs.h"
#include "dpcm.h"
#include "memory_stream.h"
#include "decompressor_lzs.h"
#include "sci_util.h"

// Local constants copied from ScummVM behaviour.
static const size_t kRobotZeroCompressSize = 2048;

// file access helpers used by the parser
static uint16_t read_sci11_u16_file(FILE *f) {
    uint8_t tmp[2];
    if (fread(tmp,1,2,f) != 2) return 0;
    return SciHelpers::READ_SCI11ENDIAN_UINT16(tmp);
}
static uint32_t read_sci11_u32_file(FILE *f) {
    uint8_t tmp[4];
    if (fread(tmp,1,4,f) != 4) return 0;
    return SciHelpers::READ_SCI11ENDIAN_UINT32(tmp);
}

// Constructor
RbtParser::RbtParser(FILE *f) : _f(f), _fileOffset(0) {}

// Destructor
RbtParser::~RbtParser() {}

bool RbtParser::parseHeader() {
    // Ensure `_fileOffset` has a sensible default for standalone .RBT files.
    _fileOffset = 0;

    // Sanity check signature and determine endianness.
    // Read first 2 bytes as little-endian (id) — ScummVM uses 0x16.
    if (!seekSet(0)) return false;
    uint16_t id = readUint16LE();
    if (id != 0x16) {
        std::fprintf(stderr, "parseHeader: invalid signature id=0x%04x\n", id);
        return false;
    }

    // Version decision: read 16-bit at offset 6 as BE to detect big-endian
    if (!seekSet(6)) return false;
    uint16_t v = readUint16BE();
    _bigEndian = (0 < v && v <= 0x00ff);
    SciHelpers::setPlatformMacintosh(_bigEndian);

    // Verify "SOL" signature at offset 2 (4 bytes: 'S' 'O' 'L' 0x00)
    if (!seekSet(2)) return false;
    uint32_t tag = readUint32(true); // read as big-endian
    if (tag != 0x534f4c00) { // 'S' 'O' 'L' '\0'
        std::fprintf(stderr, "parseHeader: invalid SOL tag=0x%08x\n", tag);
        return false;
    }

    // After reading the tag (4 bytes), we're at offset 6.
    // Now read the version field using detected endianness.
    _version = _bigEndian ? readUint16BE() : readUint16LE();
    if (_version < 5 || _version > 6) {
        std::fprintf(stderr, "Unsupported robot version=%u\n", _version);
        return false;
    }

    _audioBlockSize = _bigEndian ? readUint16BE() : readUint16LE();
    _primerZeroCompressFlag = (int16_t)( _bigEndian ? (int16_t)readUint16BE() : (int16_t)readUint16LE());

    // skip 2 bytes
    fseek(_f, 2, SEEK_CUR);

    _numFramesTotal = _bigEndian ? readUint16BE() : readUint16LE();
    _paletteSize = _bigEndian ? readUint16BE() : readUint16LE();
    _primerReservedSize = _bigEndian ? readUint16BE() : readUint16LE();

    // reading x/y resolution
    int16_t xRes = _bigEndian ? (int16_t)readUint16BE() : (int16_t)readUint16LE();
    int16_t yRes = _bigEndian ? (int16_t)readUint16BE() : (int16_t)readUint16LE();

    // hasPalette + hasAudio
    uint8_t hasPalette = 0;
    fread(&hasPalette, 1, 1, _f);
    uint8_t hasAudio = 0;
    fread(&hasAudio, 1, 1, _f);
    _hasAudio = (hasAudio != 0);

    // skip 2 bytes
    fseek(_f, 2, SEEK_CUR);

    _frameRate = _bigEndian ? (int16_t)readUint16BE() : (int16_t)readUint16LE();
    _isHiRes = _bigEndian ? (int16_t)readUint16BE() : (int16_t)readUint16LE();
    _maxSkippablePackets = _bigEndian ? (int16_t)readUint16BE() : (int16_t)readUint16LE();
    _maxCelsPerFrame = _bigEndian ? (int16_t)readUint16BE() : (int16_t)readUint16LE();

    // read four max cel areas
    for (int ii = 0; ii < 4; ++ii) {
        int32_t val = readSint32(_bigEndian);
        (void)val;
    }

    // skip 8 reserved bytes
    fseek(_f, 8, SEEK_CUR);

    // Primer / audio metadata
    if (_hasAudio) {
        if (_primerReservedSize != 0) {
            long primerHeaderPosition = ftell(_f);
            _totalPrimerSize = readSint32(_bigEndian);
            _primerCompressionType = (int16_t)readSint32(_bigEndian);
            _evenPrimerSize = readSint32(_bigEndian);
            _oddPrimerSize = readSint32(_bigEndian);
            _primerPosition = ftell(_f);

            if (_primerCompressionType != 0) {
                std::fprintf(stderr, "Unknown primer compression type=%d\n", _primerCompressionType);
            }

            if (_evenPrimerSize + _oddPrimerSize != _primerReservedSize) {
                // move pointer to after reserved area
                fseek(_f, primerHeaderPosition + _primerReservedSize, SEEK_SET);
            } else {
                // read primer data into raw buffers
                if (_evenPrimerSize > 0) {
                    _primerEvenRaw.resize((size_t)_evenPrimerSize);
                    if (fread(_primerEvenRaw.data(), 1, _primerEvenRaw.size(), _f) != _primerEvenRaw.size()) {
                        _primerEvenRaw.clear();
                    }
                }
                if (_oddPrimerSize > 0) {
                    _primerOddRaw.resize((size_t)_oddPrimerSize);
                    if (fread(_primerOddRaw.data(), 1, _primerOddRaw.size(), _f) != _primerOddRaw.size()) {
                        _primerOddRaw.clear();
                    }
                }
                fseek(_f, primerHeaderPosition + _primerReservedSize, SEEK_SET);
            }
        } else if (_primerZeroCompressFlag) {
            _evenPrimerSize = 19922;
            _oddPrimerSize = 21024;
            _totalPrimerSize = _evenPrimerSize + _oddPrimerSize;
            _primerPosition = -1;
            if (_evenPrimerSize > 0) _primerEvenRaw.assign((size_t)_evenPrimerSize, 0);
            if (_oddPrimerSize > 0) _primerOddRaw.assign((size_t)_oddPrimerSize, 0);
        }
    }

    // Palette
    if (hasPalette) {
        _paletteData.resize(_paletteSize);
        fread(_paletteData.data(),1,_paletteSize,_f);
    } else {
        fseek(_f, _paletteSize, SEEK_CUR);
    }

    // Continue with existing code: read tables etc.

    // read the two tables that appear next in the file. Their intended meaning
    // should be: videoSizes then packetSizes, but some files appear to have the
    // tables reversed. Read both into temporaries and auto-detect the correct
    // ordering by probing the candidate record positions and checking whether
    // the first field (screenItemCount) looks plausible (<= kScreenItemListSize).
    std::vector<uint32_t> tableA(_numFramesTotal), tableB(_numFramesTotal);
    if (_version == 5) {
        for (int i = 0; i < _numFramesTotal; ++i) tableA[i] = read_sci11_u16_file(_f);
        for (int i = 0; i < _numFramesTotal; ++i) tableB[i] = read_sci11_u16_file(_f);
    } else {
        for (int i = 0; i < _numFramesTotal; ++i) tableA[i] = read_sci11_u32_file(_f);
        for (int i = 0; i < _numFramesTotal; ++i) tableB[i] = read_sci11_u32_file(_f);
    }

    // Debug: print first entries of the raw tables for inspection
    if (!tableA.empty()) {
        std::fprintf(stderr, "tableA[0..4]: %u,%u,%u,%u,%u\n",
                     tableA.size()>0? tableA[0]:0,
                     tableA.size()>1? tableA[1]:0,
                     tableA.size()>2? tableA[2]:0,
                     tableA.size()>3? tableA[3]:0,
                     tableA.size()>4? tableA[4]:0);
    }
    if (!tableB.empty()) {
        std::fprintf(stderr, "tableB[0..4]: %u,%u,%u,%u,%u\n",
                     tableB.size()>0? tableB[0]:0,
                     tableB.size()>1? tableB[1]:0,
                     tableB.size()>2? tableB[2]:0,
                     tableB.size()>3? tableB[3]:0,
                     tableB.size()>4? tableB[4]:0);
    }

    // Helper to count plausible frames when interpreting 'video' and 'packet'
    auto countPlausible = [this](const std::vector<uint32_t> &video, const std::vector<uint32_t> &packet) {
        const uint16_t kScreenItemListSize = 10;
        long base = ftell(_f);
        long origPos = base;
        // align as ScummVM would (take into account file offset)
        int bytesRemaining = (int)((base - _fileOffset) % 2048);
        long aligned = base;
        if (bytesRemaining) aligned = base + (2048 - bytesRemaining);

        int good = 0;
        long cur = aligned;
        for (int i = 0; i < std::min((int)this->_numFramesTotal, 8); ++i) {
            // ensure we stay within file
            if (!seekSet((size_t)cur)) break;
            uint8_t tmp[32] = {0};
            if (fread(tmp,1,18,_f) != 18) break;
            uint16_t screenCount = SciHelpers::READ_SCI11ENDIAN_UINT16(tmp);
            if (screenCount > kScreenItemListSize) { cur += packet[i]; continue; }
            // parse first cel header to validate sizes
            // The in-file layout is: [u16 screenItemCount][verticalScale][cel header...]
            // createCels5 is called with pointer = filePos + 2. Therefore the
            // cel width/height are at offsets +4/+6 from the file position.
            uint16_t celW = SciHelpers::READ_SCI11ENDIAN_UINT16(tmp + 4);
            uint16_t celH = SciHelpers::READ_SCI11ENDIAN_UINT16(tmp + 6);
            uint64_t area = (uint64_t)celW * (uint64_t)celH;
            if (celW > 0 && celH > 0 && area < 20000000) ++good;
            cur += packet[i];
        }
        // restore file position before returning
        seekSet((size_t)origPos);
        return good;
    };

    // Try the more expensive plausibility test first: interpret tableA as
    // videoSizes and tableB as packetSizes, and vice-versa; pick the one
    // that yields more plausible frame headers. Fall back to a quick size
    // comparison heuristic if counts tie.
    // NOTE: table ordering and _fileOffset selection will be performed after
    // skipping the cues and before computing recordPositions, so that the
    // plausibility checks use the same base position as the eventual
    // recordPositions calculation.

    // cues: read 256 * 4-byte times and 256 * 2-byte values (store them)
    _cueTimes.clear();
    _cueValues.clear();
    _cueTimes.reserve(256);
    _cueValues.reserve(256);
    for (int i = 0; i < 256; ++i) {
        int32_t t = readSint32(_bigEndian);
        _cueTimes.push_back(t);
    }
    for (int i = 0; i < 256; ++i) {
        uint16_t v = _bigEndian ? readUint16BE() : readUint16LE();
        _cueValues.push_back(v);
    }

    // Deterministic resolution of table ordering and file offset.
    // Try a small set of `_fileOffset` candidates combined with both table
    // orderings; pick the combination that yields the most plausible
    // initial frames. This is a bounded, deterministic check (no forward
    // scanning across the file).
    const int fileOffsetCandidates[] = {0, 6};
    int bestCount = -1;
    int bestOffset = 0;
    bool bestSwap = false; // false => tableA=video, true => tableB=video
    for (int offIdx = 0; offIdx < (int)(sizeof(fileOffsetCandidates)/sizeof(fileOffsetCandidates[0])); ++offIdx) {
        int candidateOffset = fileOffsetCandidates[offIdx];
        for (int swap = 0; swap < 2; ++swap) {
            _fileOffset = candidateOffset;
            int count = 0;
            if (!swap) count = countPlausible(tableA, tableB);
            else count = countPlausible(tableB, tableA);
            std::fprintf(stderr, "candidate offset=%d swap=%d plausible=%d\n", candidateOffset, swap, count);
            if (count > bestCount) {
                bestCount = count;
                bestOffset = candidateOffset;
                bestSwap = swap != 0;
            }
        }
    }
    _fileOffset = bestOffset;
    if (bestSwap) {
        _videoSizes = std::move(tableB);
        _packetSizes = std::move(tableA);
    } else {
        _videoSizes = std::move(tableA);
        _packetSizes = std::move(tableB);
    }
    std::fprintf(stderr, "chosen fileOffset=%ld tableSwap=%d plausible=%d\n", _fileOffset, bestSwap ? 1 : 0, bestCount);

    // align to next 2048-byte sector (respecting file offset)
    long pos = ftell(_f);
    int bytesRemaining = (int)((pos - _fileOffset) % 2048);
    if (bytesRemaining) {
        fseek(_f, 2048 - bytesRemaining, SEEK_CUR);
    }

    // record positions: compute from the record sizes (packetSizes), using
    // the position already aligned above.
    _recordPositions.reserve(_numFramesTotal);
    long cur = ftell(_f);
    _recordPositions.push_back((uint32_t)cur);
    for (int i = 0; i < _numFramesTotal - 1; ++i) {
        cur += _packetSizes[i];
        _recordPositions.push_back((uint32_t)cur);
    }

    // Debug prints
    std::fprintf(stderr, "parseHeader: version=%u frames=%u audioBlockSize=%u hasAudio=%d paletteSize=%u primerReservedSize=%u\n",
                 _version, _numFramesTotal, _audioBlockSize, _hasAudio ? 1 : 0, _paletteSize, _primerReservedSize);
    if (!_videoSizes.empty()) {
        std::fprintf(stderr, "videoSizes[0..4]: %u,%u,%u,%u,%u\n",
                     _videoSizes.size()>0? _videoSizes[0]:0,
                     _videoSizes.size()>1? _videoSizes[1]:0,
                     _videoSizes.size()>2? _videoSizes[2]:0,
                     _videoSizes.size()>3? _videoSizes[3]:0,
                     _videoSizes.size()>4? _videoSizes[4]:0);
    }
    if (!_packetSizes.empty()) {
        std::fprintf(stderr, "packetSizes[0..4]: %u,%u,%u,%u,%u\n",
                     _packetSizes.size()>0? _packetSizes[0]:0,
                     _packetSizes.size()>1? _packetSizes[1]:0,
                     _packetSizes.size()>2? _packetSizes[2]:0,
                     _packetSizes.size()>3? _packetSizes[3]:0,
                     _packetSizes.size()>4? _packetSizes[4]:0);
    }
    if (!_recordPositions.empty()) {
        std::fprintf(stderr, "first record position: %u\n", _recordPositions[0]);
        for (size_t i = 0; i < _recordPositions.size(); ++i) {
            std::fprintf(stderr, "record[%zu]=%u videoSize=%u packetSize=%u\n", i, _recordPositions[i], i < _videoSizes.size() ? _videoSizes[i] : 0, i < _packetSizes.size() ? _packetSizes[i] : 0);
        }
    }

    return true;
}

void RbtParser::dumpMetadata(const char *outDir) {
    std::string meta = std::string(outDir) + "/metadata.txt";
    std::ofstream os(meta);
    os << "version: " << _version << "\n";
    os << "frames: " << _numFramesTotal << "\n";
    os << "hasAudio: " << _hasAudio << "\n";
    os << "paletteSize: " << _paletteSize << "\n";
    if (_hasAudio) {
        os << "audioBlockSize: " << _audioBlockSize << "\n";
        os << "primerReservedSize: " << _primerReservedSize << "\n";
        os << "primerZeroCompressFlag: " << _primerZeroCompressFlag << "\n";
        os << "primer_totalSize: " << _totalPrimerSize << "\n";
        os << "primer_evenSize: " << _evenPrimerSize << "\n";
        os << "primer_oddSize: " << _oddPrimerSize << "\n";
    }
    if (!_paletteData.empty()) {
        std::string palout = std::string(outDir) + "/palette.bin";
        std::ofstream p(palout, std::ios::binary);
        p.write((const char*)_paletteData.data(), _paletteData.size());
        p.close();
    }
    // dump cues
    if (!_cueTimes.empty() && !_cueValues.empty()) {
        std::string cuesout = std::string(outDir) + "/cues.txt";
        std::ofstream co(cuesout);
        co << "index,time,value\n";
        for (size_t i = 0; i < _cueTimes.size() && i < _cueValues.size(); ++i) {
            co << i << "," << _cueTimes[i] << "," << _cueValues[i] << "\n";
        }
        co.close();
    }
    os.close();
}

size_t RbtParser::getNumFrames() const { return _numFramesTotal; }

// helper: reads little-endian uint16 without changing file pos expectation (we already implement readUint16LE above)

static uint16_t read_u16_from_buf(const uint8_t *p, bool be) {
    if (be) return (uint16_t)p[0]<<8 | p[1];
    return (uint16_t)p[0] | (uint16_t)p[1]<<8;
}

static uint32_t read_u32_from_buf(const uint8_t *p, bool be) {
    if (be) return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | (uint32_t)p[3];
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}

bool RbtParser::extractFrame(size_t frameIndex, const char *outDir) {
    if (frameIndex >= _recordPositions.size()) return false;
    // Try to seek to the recorded position. Do NOT implement a fallback
    // forward-scan heuristic here; follow ScummVM behaviour exactly and
    // trust the recorded positions computed from the table data. If the
    // recorded position does not look plausible, skip the frame.
    size_t startPos = _recordPositions[frameIndex];
    auto looksPlausibleAt = [&](size_t pos) -> bool {
        if (!seekSet(pos)) return false;
        uint8_t tmp[24] = {0};
        if (fread(tmp,1,20,_f) != 20) return false;
        std::fprintf(stderr, "  looksPlausibleAt: pos=%u bytes=%02x %02x %02x %02x %02x %02x\n",
                     (uint32_t)pos, tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);
        uint16_t screenCount = SciHelpers::READ_SCI11ENDIAN_UINT16(tmp);
        if (screenCount > 10) return false;
        // parse cel header at offset 2
        uint16_t vertical = tmp[2];
        uint16_t celW = SciHelpers::READ_SCI11ENDIAN_UINT16(tmp + 4);
        uint16_t celH = SciHelpers::READ_SCI11ENDIAN_UINT16(tmp + 6);
        // dataSize and numChunks are located further into the cel header
        // (see createCel5): at file offsets +16 and +18 respectively.
        uint16_t dataSize = SciHelpers::READ_SCI11ENDIAN_UINT16(tmp + 16);
        uint16_t numChunks = SciHelpers::READ_SCI11ENDIAN_UINT16(tmp + 18);
        uint64_t area = (uint64_t)celW * (uint64_t)celH;
        if (celW == 0 || celH == 0 || area >= 20000000) return false;
        if (dataSize == 0 || dataSize > 200000) return false;
        if (numChunks == 0 || numChunks > 100) return false;
        return true;
    };

    // No scanning: if the recorded position is not plausible, skip frame.
    if (!looksPlausibleAt(startPos)) {
        std::fprintf(stderr, "extractFrame: idx=%zu pos=%u not plausible at recorded position; skipping (no fallback)\n",
                     frameIndex, (uint32_t)startPos);
        return true;
    }
    if (!seekSet(startPos)) return false;

    uint32_t videoSize = _videoSizes[frameIndex];
    std::fprintf(stderr, "extractFrame: idx=%zu pos=%u videoSize=%u\n", frameIndex, _recordPositions[frameIndex], videoSize);
    if (videoSize == 0) return true;

    std::vector<uint8_t> buf(videoSize);
    if (fread(buf.data(),1,videoSize,_f) != videoSize) return false;

    std::fprintf(stderr, "  first bytes: %02x %02x %02x %02x %02x %02x\n",
                 buf.size()>0?buf[0]:0, buf.size()>1?buf[1]:0, buf.size()>2?buf[2]:0,
                 buf.size()>3?buf[3]:0, buf.size()>4?buf[4]:0, buf.size()>5?buf[5]:0);

    // first field: number of cels (SCI11 endian 16)
    uint16_t screenItemCount = SciHelpers::READ_SCI11ENDIAN_UINT16(buf.data());
    std::fprintf(stderr, "  screenItemCount(raw LE)=%u\n", screenItemCount);

    // ScummVM guard: if too many screen items, ignore this frame
    const uint16_t kScreenItemListSize = 10;
    if (screenItemCount > kScreenItemListSize) {
        std::fprintf(stderr, "  screenItemCount %u > %u; skipping frame (ScummVM behaviour)\n", screenItemCount, kScreenItemListSize);
        return true;
    }

    // Delegate to createCels5 (port of ScummVM RobotDecoder::createCels5)
    createCels5(buf.data()+2, (int16_t)screenItemCount, outDir, frameIndex);

    return true;
}

// Implement trivial read helpers for the local FILE* APIs used above
// compression constants used by robot format
static const uint16_t kCompressionLZS = 0;
static const uint16_t kCompressionNone = 2;

// Helper functions to read Uint16/32 using file operations
uint16_t RbtParser::readUint16LE() {
    uint8_t tmp[2];
    if (fread(tmp,1,2,_f) != 2) return 0;
    return (uint16_t)tmp[0] | (uint16_t)tmp[1] << 8;
}
uint16_t RbtParser::readUint16BE() {
    uint8_t tmp[2];
    if (fread(tmp,1,2,_f) != 2) return 0;
    return (uint16_t)tmp[0] << 8 | (uint16_t)tmp[1];
}
int32_t RbtParser::readSint32(bool asBE) {
    uint8_t tmp[4];
    if (fread(tmp,1,4,_f) != 4) return 0;
    if (asBE) return (int32_t)((uint32_t)tmp[0] << 24 | (uint32_t)tmp[1] << 16 | (uint32_t)tmp[2] << 8 | (uint32_t)tmp[3]);
    return (int32_t)((uint32_t)tmp[0] | (uint32_t)tmp[1] << 8 | (uint32_t)tmp[2] << 16 | (uint32_t)tmp[3] << 24);
}
uint32_t RbtParser::readUint32(bool asBE) {
    uint8_t tmp[4];
    if (fread(tmp,1,4,_f) != 4) return 0;
    if (asBE) return (uint32_t)tmp[0] << 24 | (uint32_t)tmp[1] << 16 | (uint32_t)tmp[2] << 8 | (uint32_t)tmp[3];
    return (uint32_t)tmp[0] | (uint32_t)tmp[1] << 8 | (uint32_t)tmp[2] << 16 | (uint32_t)tmp[3] << 24;
}

bool RbtParser::seekSet(size_t pos) {
    return fseek(_f, (long)pos, SEEK_SET) == 0;
}

uint32_t RbtParser::createCel5(const uint8_t *rawVideoData, const int16_t screenItemIndex, const char *outDir, size_t frameIndex) {
    const uint8_t verticalScale = rawVideoData[1];
    const uint16_t celWidth = SciHelpers::READ_SCI11ENDIAN_UINT16(rawVideoData + 2);
    const uint16_t celHeight = SciHelpers::READ_SCI11ENDIAN_UINT16(rawVideoData + 4);
    const uint16_t celX = SciHelpers::READ_SCI11ENDIAN_UINT16(rawVideoData + 10);
    const uint16_t celY = SciHelpers::READ_SCI11ENDIAN_UINT16(rawVideoData + 12);
    const uint16_t dataSize = SciHelpers::READ_SCI11ENDIAN_UINT16(rawVideoData + 14);
    const int16_t numDataChunks = (int16_t)SciHelpers::READ_SCI11ENDIAN_UINT16(rawVideoData + 16);

    const uint8_t *p = rawVideoData + 22; // kCelHeaderSize

    std::fprintf(stderr, "    cel %d: w=%u h=%u dataSize=%u chunks=%d\n", screenItemIndex, celWidth, celHeight, dataSize, numDataChunks);

    const uint64_t area = (uint64_t)celWidth * (uint64_t)celHeight;
    if (area > 20000000) {
        std::fprintf(stderr, "    cel area too large (%lu), skipping\n", (unsigned long)area);
        return 0;
    }

    // If verticalScale != 100 we will decompress into a temporary "squashed"
    // buffer and then expand to the target size.
    const int verticalScaleFactor = verticalScale;
    int sourceHeight = (celHeight * verticalScaleFactor) / 100;
    const size_t decompressedArea = (verticalScaleFactor == 100) ? (size_t)area : (size_t)celWidth * (size_t)sourceHeight;

    std::vector<uint8_t> decompressed;
    decompressed.reserve(decompressedArea);

    for (int i = 0; i < numDataChunks; ++i) {
        const uint32_t compSize = SciHelpers::READ_SCI11ENDIAN_UINT32(p);
        const uint32_t decompSize = SciHelpers::READ_SCI11ENDIAN_UINT32(p + 4);
        const uint16_t compressionType = SciHelpers::READ_SCI11ENDIAN_UINT16(p + 8);
        p += 10;

        std::fprintf(stderr, "      chunk %d: compSize=%u decompSize=%u compType=%u ptr=%ld\n", i, compSize, decompSize, compressionType, (long)(p - rawVideoData));

            if (compressionType == kCompressionNone) {
            // raw copy
            decompressed.insert(decompressed.end(), p, p + decompSize);
        } else if (compressionType == kCompressionLZS) {
            // Use Common::MemoryReadStream + DecompressorLZS::unpack (mimic ScummVM path)
            Common::MemoryReadStream mrs(p, compSize);
            DecompressorLZS dec;
            std::vector<uint8_t> out(decompSize);
            int rc = dec.unpack(&mrs, out.data(), compSize, decompSize);
            if (rc != 0) {
                std::fprintf(stderr, "      DecompressorLZS::unpack failed rc=%d\n", rc);
                return 0;
            }
            decompressed.insert(decompressed.end(), out.begin(), out.end());
        } else {
            std::fprintf(stderr, "Unknown compression type %u\n", compressionType);
            return 0;
        }

        p += compSize;
    }

    // Now produce final pixel indices, applying vertical expansion if needed
    std::vector<uint8_t> finalPixels;
    finalPixels.resize(area);

    if (verticalScaleFactor == 100) {
        if (decompressed.size() >= area) {
            std::copy_n(decompressed.data(), area, finalPixels.data());
        } else {
            // truncated decompressed data - fill with zeros
            std::fill(finalPixels.begin(), finalPixels.end(), 0);
        }
    } else {
        // source height is shrunk, expand lines
        if (sourceHeight <= 0) sourceHeight = 1;
        int numerator = celHeight;
        int denominator = sourceHeight;
        int remainder = 0;
        const uint8_t *srcPtr = decompressed.data();
        uint8_t *dstPtr = finalPixels.data();
        for (int y = sourceHeight - 1; y >= 0; --y) {
            remainder += numerator;
            int linesToDraw = remainder / denominator;
            remainder %= denominator;

            for (int l = 0; l < linesToDraw; ++l) {
                if ((size_t)(srcPtr - decompressed.data()) + celWidth <= decompressed.size()) {
                    std::copy_n(srcPtr, celWidth, dstPtr);
                } else {
                    // out of data: fill with 0
                    std::fill_n(dstPtr, celWidth, 0);
                }
                dstPtr += celWidth;
            }
            srcPtr += celWidth;
        }
    }

    // write PGM
    char name[512];
    snprintf(name, sizeof(name), "%s/frame_%04zu_cel_%02d.pgm", outDir, frameIndex, screenItemIndex);
    std::ofstream img(name, std::ios::binary);
    img << "P5\n" << celWidth << " " << celHeight << "\n255\n";
    if (!finalPixels.empty()) img.write((const char*)finalPixels.data(), finalPixels.size());
    img.close();

    return 22 + dataSize;
}

void RbtParser::createCels5(const uint8_t *rawVideoData, const int16_t numCels, const char *outDir, size_t frameIndex) {
    const uint8_t *p = rawVideoData;
    for (int16_t i = 0; i < numCels; ++i) {
        uint32_t consumed = createCel5(p, i, outDir, frameIndex);
        if (consumed == 0) break;
        p += consumed;
    }
}

void RbtParser::extractAllAudio(std::function<void(const int16_t*, size_t)> cb) {
    if (!_hasAudio) return;

    struct Packet { int32_t position; std::vector<int16_t> samples; };
    std::vector<Packet> packets;

    // If primer raw buffers were read during header parsing, decode them
    // and insert them as initial packets so the RobotAudioStream can be primed.
    if (!_primerEvenRaw.empty() || !_primerOddRaw.empty()) {
        int32_t basePos = (_primerPosition > 0) ? (int32_t)_primerPosition : 0;
        if (!_primerEvenRaw.empty()) {
            std::vector<int16_t> evsamples(_primerEvenRaw.size());
            int16_t predictor = 0;
            deDPCM16Mono(evsamples.data(), _primerEvenRaw.data(), (uint32_t)_primerEvenRaw.size(), predictor);
            packets.push_back(Packet{ basePos, std::move(evsamples) });
        }
        if (!_primerOddRaw.empty()) {
            std::vector<int16_t> odsamples(_primerOddRaw.size());
            int16_t predictor = 0;
            deDPCM16Mono(odsamples.data(), _primerOddRaw.data(), (uint32_t)_primerOddRaw.size(), predictor);
            // odd primer should be offset by 2 ticks so it maps to the alternate channel
            packets.push_back(Packet{ basePos + 2, std::move(odsamples) });
        }
    }

    // Collect and decompress packets located in the contiguous audio region.
    // Observation: audio blocks are laid out sequentially starting at
    // audioStart = recordPositions[0] + videoSizes[0]; each block length is
    // _packetSizes[i]. Decode each block and append samples sequentially.
    std::fprintf(stderr, "audio: primer packets inserted=%zu\n", packets.size());

    if (_recordPositions.empty() || _videoSizes.empty() || _packetSizes.empty()) {
        std::fprintf(stderr, "audio: missing layout info, aborting\n");
        return;
    }

    uint32_t audioStart = _recordPositions[0] + _videoSizes[0];
    std::vector<int16_t> outSeq;

    // If there are primer-decoded packets, prepend them to the output sequence
    for (const auto &p : packets) {
        if (!p.samples.empty()) {
            outSeq.insert(outSeq.end(), p.samples.begin(), p.samples.end());
        }
    }

    // First try per-frame audio headers: read `position`/`size` at
    // _recordPositions[i] + _videoSizes[i] for each frame. If at least one
    // frame contains a plausible `position` (>0) we'll use these headers to
    // place packets by position (ScummVM-style). Otherwise, fall back to the
    // contiguous audio region that follows the last header.
    std::vector<Packet> headerPackets;
    headerPackets.reserve(_numFramesTotal);
    for (size_t i = 0; i < _numFramesTotal && i < _recordPositions.size(); ++i) {
        uint64_t hdrOff = (uint64_t)_recordPositions[i] + (uint64_t)_videoSizes[i];
        if (!seekSet((size_t)hdrOff)) continue;

        int32_t audioPos = readSint32(_bigEndian);
        int32_t audioSize = readSint32(_bigEndian);

        // invalid / empty audio for this frame
        if (audioPos == 0 || audioSize <= 0) continue;

        // Some files contain bogus (0xffffffff / very large) sizes. Ignore those.
        if ((uint32_t)audioSize > 10 * 1024 * 1024) continue;

        // read compressed block (prepend zeros if zero-compress)
        if (audioSize != _audioBlockSize && _audioBlockSize != 0) {
            // prepend kRobotZeroCompressSize zeros
            std::vector<uint8_t> compBuf(kRobotZeroCompressSize + audioSize);
            std::memset(compBuf.data(), 0, kRobotZeroCompressSize);
            if (fread(compBuf.data() + kRobotZeroCompressSize, 1, audioSize, _f) != (size_t)audioSize) continue;
            std::vector<int16_t> samples(kRobotZeroCompressSize + audioSize);
            int16_t predictor = 0;
            deDPCM16Mono(samples.data(), compBuf.data(), (uint32_t)(kRobotZeroCompressSize + audioSize), predictor);
            headerPackets.push_back(Packet{ audioPos, std::move(samples) });
        } else {
            std::vector<uint8_t> compBuf(audioSize);
            if (fread(compBuf.data(), 1, audioSize, _f) != (size_t)audioSize) continue;
            std::vector<int16_t> samples(audioSize);
            int16_t predictor = 0;
            deDPCM16Mono(samples.data(), compBuf.data(), (uint32_t)audioSize, predictor);
            headerPackets.push_back(Packet{ audioPos, std::move(samples) });
        }
    }

    if (!headerPackets.empty()) {
        // Normalize positions by the minimum reported position (start offset),
        // then insert primer and per-frame packets using the file's semantics
        // (_position - startOffset) * 2 when creating RobotAudioPacket.
        int32_t minPos = INT32_MAX;
        for (const auto &p : headerPackets) if (p.position < minPos) minPos = p.position;
        // If primer was present, let it participate in normalization so that
        // primers end up placed correctly relative to real packets.
        if (!_primerEvenRaw.empty() || !_primerOddRaw.empty()) {
            int32_t basePos = (_primerPosition > 0) ? (int32_t)_primerPosition : 0;
            // primer packets use positions basePos and basePos+2
            if (basePos < minPos) minPos = basePos;
        }

        // insert primer packets (normalized) first
        if (!_primerEvenRaw.empty()) {
            std::vector<int16_t> evsamples(_primerEvenRaw.size());
            int16_t predictor = 0;
            deDPCM16Mono(evsamples.data(), _primerEvenRaw.data(), (uint32_t)_primerEvenRaw.size(), predictor);
            int32_t primerBase = (_primerPosition > 0) ? (int32_t)_primerPosition : 0;
            int32_t normBase = (primerBase - minPos) * 2;
            packets.push_back(Packet{ normBase, std::move(evsamples) });
        }
        if (!_primerOddRaw.empty()) {
            std::vector<int16_t> odsamples(_primerOddRaw.size());
            int16_t predictor = 0;
            deDPCM16Mono(odsamples.data(), _primerOddRaw.data(), (uint32_t)_primerOddRaw.size(), predictor);
            int32_t primerBase = (_primerPosition > 0) ? (int32_t)_primerPosition : 0;
            int32_t normBase = (primerBase - minPos) * 2 + 2;
            packets.push_back(Packet{ normBase, std::move(odsamples) });
        }

        // insert all header packets (normalized)
        for (auto &hp : headerPackets) {
            int32_t norm = (hp.position - minPos) * 2;
            packets.push_back(Packet{ norm, std::move(hp.samples) });
        }
    } else {
        // Now append per-frame decoded packets from the contiguous audio area
    uint64_t accum = 0;
    for (size_t i = 0; i < _packetSizes.size(); ++i) {
        uint32_t compSize = _packetSizes[i];
        if (compSize == 0) continue;
        uint64_t off = (uint64_t)audioStart + accum;
        if (!seekSet((size_t)off)) break;

        std::vector<uint8_t> compBuf;
        size_t finalCompSize = compSize;
        if (_audioBlockSize != 0 && compSize != _audioBlockSize) {
            compBuf.resize(kRobotZeroCompressSize + compSize);
            std::memset(compBuf.data(), 0, kRobotZeroCompressSize);
            if (fread(compBuf.data() + kRobotZeroCompressSize, 1, compSize, _f) != compSize) break;
            finalCompSize += kRobotZeroCompressSize;
        } else {
            compBuf.resize(compSize);
            if (fread(compBuf.data(), 1, compSize, _f) != compSize) break;
        }

        std::vector<int16_t> samples(finalCompSize);
        int16_t predictor = 0;
        deDPCM16Mono(samples.data(), compBuf.data(), (uint32_t)finalCompSize, predictor);

        // append decoded samples sequentially
        outSeq.insert(outSeq.end(), samples.begin(), samples.end());

        accum += compSize;
    }

    std::fprintf(stderr, "audio: decoded sequence samples(total packets)=%zu (incl primer)\n", outSeq.size());

    if (outSeq.empty()) return;
    }

    // Heuristic interleaving (ScummVM-like): write packets alternately into
    // even/odd sample positions. Use packet order to determine parity. This
    // approximates the RobotAudioStream behaviour when per-packet positions
    // are not available. Each packet is written sequentially at the next
    // available tick slot.
    size_t totalSamples = outSeq.size();
    // We'll place samples into an interleaved buffer where final size is
    // roughly totalSamples * 2 (every-other layout). Allocate conservatively.
    size_t approxOutSize = totalSamples * 2 + 1024;
    std::vector<int16_t> interleaved(approxOutSize, 0);
    std::vector<char> written(approxOutSize, 0);

    size_t writeTick = 0; // logical tick cursor (in source-sample units)
    size_t packetIndex = 0;
    for (size_t i = 0; i < _packetSizes.size(); ++i) {
        uint32_t compSize = _packetSizes[i];
        if (compSize == 0) continue;

        // decode block at this position (we already decoded above into outSeq
        // as consecutive samples). Instead, read/decode again here for
        // correctness of boundaries (performant enough for extraction tool).
        // Seek to proper offset
        // compute file offset for this block
        uint64_t fileOffset = (uint64_t)(_recordPositions[0] + _videoSizes[0]);
        // compute accumulative offset up to this block
        uint64_t accum = 0;
        for (size_t j = 0; j < i; ++j) accum += _packetSizes[j];
        fileOffset += accum;
        if (!seekSet((size_t)fileOffset)) break;

        std::vector<uint8_t> compBuf;
        size_t finalCompSize = compSize;
        if (_audioBlockSize != 0 && compSize != _audioBlockSize) {
            compBuf.resize(kRobotZeroCompressSize + compSize);
            std::memset(compBuf.data(), 0, kRobotZeroCompressSize);
            if (fread(compBuf.data() + kRobotZeroCompressSize, 1, compSize, _f) != compSize) break;
            finalCompSize += kRobotZeroCompressSize;
        } else {
            compBuf.resize(compSize);
            if (fread(compBuf.data(), 1, compSize, _f) != compSize) break;
        }

        std::vector<int16_t> samples(finalCompSize);
        int16_t predictor = 0;
        deDPCM16Mono(samples.data(), compBuf.data(), (uint32_t)finalCompSize, predictor);

        // ensure capacity
        size_t needed = (writeTick + samples.size()) * 2 + 4;
        if (needed > interleaved.size()) {
            size_t newSize = std::max(needed, interleaved.size() * 2);
            interleaved.resize(newSize, 0);
            written.resize(newSize, 0);
        }

        // parity: even packets -> bufIdx 0, odd -> bufIdx 1
        size_t bufIdx = (packetIndex % 2 == 0) ? 0 : 1;
        size_t dest = writeTick * 2 + bufIdx;
        for (size_t s = 0; s < samples.size(); ++s) {
            size_t idx = dest + s * 2;
            if (idx < interleaved.size()) {
                interleaved[idx] = samples[s];
                written[idx] = 1;
            }
        }

        writeTick += samples.size();
        ++packetIndex;
    }

    // Also write primer packets (they were decoded earlier) at the start
    // if present: they should already be present in 'packets' vector; but in
    // some flows we decoded them earlier into 'packets' — ensure they are in
    // the interleaved buffer by prepending if necessary.
    // (Simpler approach: already handled above by decoding each packet in order.)

    // Interpolate missing samples in interleaved buffer
    ssize_t lastIndex = (ssize_t)interleaved.size() - 1;
    for (ssize_t i = 0; i <= lastIndex; ++i) {
        if (written[i]) continue;
        ssize_t l = i - 1;
        while (l >= 0 && !written[l]) --l;
        ssize_t r = i + 1;
        while (r <= lastIndex && !written[r]) ++r;
        if (l >= 0 && r <= lastIndex) {
            int16_t lv = interleaved[l];
            int16_t rv = interleaved[r];
            double t = double(i - l) / double(r - l);
            interleaved[i] = (int16_t)((1.0 - t) * lv + t * rv);
            written[i] = 1;
        } else if (l >= 0) {
            interleaved[i] = interleaved[l];
            written[i] = 1;
        } else if (r <= lastIndex) {
            interleaved[i] = interleaved[r];
            written[i] = 1;
        }
    }

    // Compute final length (trim trailing zeros)
    ssize_t finalLen = (ssize_t)interleaved.size();
    while (finalLen > 0 && !written[finalLen-1]) --finalLen;
    if (finalLen <= 0) return;

    // Deliver via callback in chunks
    const size_t CHUNK = 4096;
    size_t sent = 0;
    while (sent < (size_t)finalLen) {
        size_t toSend = std::min(CHUNK, (size_t)finalLen - sent);
        cb(interleaved.data() + sent, toSend);
        sent += toSend;
    }
}
