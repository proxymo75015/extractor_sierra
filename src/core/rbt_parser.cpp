#include "rbt_parser.h"
#include <cassert>
#include <cstring>
#include <map>
#include <algorithm>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>

#include "formats/lzs.h"
#include "formats/dpcm.h"
#include "utils/memory_stream.h"
#include "formats/decompressor_lzs.h"
#include "utils/sci_util.h"

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
            _primerCompressionType = (int16_t)(_bigEndian ? readUint16BE() : readUint16LE());
            _evenPrimerSize = readSint32(_bigEndian);
            _oddPrimerSize = readSint32(_bigEndian);
            _primerPosition = ftell(_f);

            if (_primerCompressionType != 0) {
                std::fprintf(stderr, "Unknown primer compression type=%d\n", _primerCompressionType);
            }

            std::fprintf(stderr, "primer: even=%d odd=%d total=%d reserved=%u\n", 
                         _evenPrimerSize, _oddPrimerSize, _evenPrimerSize + _oddPrimerSize, _primerReservedSize);

            // total might be slightly less than reserved (padding)
            if (_evenPrimerSize + _oddPrimerSize > _primerReservedSize) {
                // invalid, move pointer to after reserved area
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

    // Palette (HunkPalette format)
    if (hasPalette) {
        long palOffset = ftell(_f);
        std::vector<uint8_t> rawPalette(_paletteSize);
        fread(rawPalette.data(), 1, _paletteSize, _f);
        
        // Parse HunkPalette header
        // Offset 10: numPalettes (1 byte)
        uint8_t numPalettes = rawPalette[10];
        
        if (numPalettes == 1 && _paletteSize >= 35) {
            // Skip hunk header (13 bytes) + palette offset table (2 bytes)
            size_t entryOffset = 13 + 2 * numPalettes;
            
            // Read entry header (22 bytes total)
            uint8_t startColor = rawPalette[entryOffset + 10];
            uint16_t numColors = rawPalette[entryOffset + 14] | (rawPalette[entryOffset + 15] << 8);
            uint8_t used = rawPalette[entryOffset + 16];
            uint8_t sharedUsed = rawPalette[entryOffset + 17];
            
            std::fprintf(stderr, "HunkPalette: startColor=%u numColors=%u used=%u sharedUsed=%u\n",
                        startColor, numColors, used, sharedUsed);
            
            // Palette data starts at entry offset + 22
            size_t dataOffset = entryOffset + 22;
            
            // Allocate palette for 256 colors (initialized to black)
            _paletteData.assign(768, 0);
            
            if (sharedUsed) {
                // RGB format (3 bytes per color)
                for (int i = 0; i < numColors && i + startColor < 256; ++i) {
                    size_t srcIdx = dataOffset + i * 3;
                    size_t dstIdx = (startColor + i) * 3;
                    if (srcIdx + 2 < rawPalette.size()) {
                        _paletteData[dstIdx] = rawPalette[srcIdx];
                        _paletteData[dstIdx + 1] = rawPalette[srcIdx + 1];
                        _paletteData[dstIdx + 2] = rawPalette[srcIdx + 2];
                    }
                }
            } else {
                // used+RGB format (4 bytes per color)
                for (int i = 0; i < numColors && i + startColor < 256; ++i) {
                    size_t srcIdx = dataOffset + i * 4;
                    size_t dstIdx = (startColor + i) * 3;
                    if (srcIdx + 3 < rawPalette.size()) {
                        // Skip 'used' flag at srcIdx
                        _paletteData[dstIdx] = rawPalette[srcIdx + 1];
                        _paletteData[dstIdx + 1] = rawPalette[srcIdx + 2];
                        _paletteData[dstIdx + 2] = rawPalette[srcIdx + 3];
                    }
                }
            }
        } else {
            std::fprintf(stderr, "Warning: unexpected palette format (numPalettes=%u)\n", numPalettes);
        }
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
    os << "frameRate: " << _frameRate << "\n";
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

int32_t RbtParser::getFrameAudioPosition(size_t frameIndex) {
    if (frameIndex >= _recordPositions.size()) return 0;
    // At each frame, the audio header is at recordPositions + videoSize
    size_t headerPos = (size_t)_recordPositions[frameIndex] + (size_t)_videoSizes[frameIndex];
    if (!_f) return 0;
    long cur = ftell(_f);
    if (!seekSet(headerPos)) return 0;
    int32_t pos = readSint32(_bigEndian);
    seekSet(cur);
    return pos;
}

int32_t RbtParser::getFrameAudioSize(size_t frameIndex) {
    if (frameIndex >= _recordPositions.size()) return 0;
    size_t headerPos = (size_t)_recordPositions[frameIndex] + (size_t)_videoSizes[frameIndex];
    if (!_f) return 0;
    long cur = ftell(_f);
    if (!seekSet(headerPos)) return 0;
    (void)readSint32(_bigEndian); // skip pos
    int32_t size = readSint32(_bigEndian);
    seekSet(cur);
    return size;
}

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

    std::fprintf(stderr, "    cel %d: w=%u h=%u dataSize=%u chunks=%d\n", 
                 screenItemIndex, celWidth, celHeight, dataSize, numDataChunks);

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

    // write PPM (RGB) avec palette
    char name[512];
    
    if (!_paletteData.empty() && _paletteSize >= 768) {
        snprintf(name, sizeof(name), "%s/frame_%04zu_cel_%02d.ppm", outDir, frameIndex, screenItemIndex);
        std::ofstream img(name, std::ios::binary);
        img << "P6\n" << celWidth << " " << celHeight << "\n255\n";
        
        for (size_t i = 0; i < finalPixels.size(); ++i) {
            uint8_t idx = finalPixels[i];
            size_t palOffset = idx * 3;
            
            if (palOffset + 2 < _paletteData.size()) {
                img.put(_paletteData[palOffset]);
                img.put(_paletteData[palOffset + 1]);
                img.put(_paletteData[palOffset + 2]);
            } else {
                img.put(0); img.put(0); img.put(0);
            }
        }
        img.close();
    } else {
        snprintf(name, sizeof(name), "%s/frame_%04zu_cel_%02d.pgm", outDir, frameIndex, screenItemIndex);
        std::ofstream img(name, std::ios::binary);
        img << "P5\n" << celWidth << " " << celHeight << "\n255\n";
        if (!finalPixels.empty()) img.write((const char*)finalPixels.data(), finalPixels.size());
        img.close();
    }

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

// ----------------------------------------------------------------------------
// Helper: interpolateChannel
// Interpolation des échantillons manquants dans un canal
// Basé sur ScummVM robot_decoder.cpp (interpolateChannel)
// ----------------------------------------------------------------------------
static void interpolateChannel(int16_t *buffer, int32_t numSamples, const int8_t bufferIndex) {
    if (numSamples <= 0) {
        return;
    }

    int16_t *inBuffer, *outBuffer;
    int16_t sample, previousSample;

    // kEOSExpansion = 2 (expansion every-other-sample)
    constexpr int kEOSExpansion = 2;

    if (bufferIndex) {
        // Canal ODD (indices impairs: 1, 3, 5...)
        outBuffer = buffer + 1;
        inBuffer = buffer + 2;
        previousSample = sample = *buffer;
        --numSamples;
    } else {
        // Canal EVEN (indices pairs: 0, 2, 4...)
        outBuffer = buffer;
        inBuffer = buffer + 1;
        previousSample = sample = *inBuffer;
    }

    while (numSamples--) {
        // Interpolation linéaire: moyenne des deux échantillons voisins
        sample = (*inBuffer + previousSample) >> 1;
        previousSample = *inBuffer;
        *outBuffer = sample;
        inBuffer += kEOSExpansion;
        outBuffer += kEOSExpansion;
    }

    if (bufferIndex) {
        *outBuffer = sample;
    }
}

// ----------------------------------------------------------------------------
// Helper: write WAV file header
// ----------------------------------------------------------------------------
static void writeWavHeader(FILE *f, uint32_t sampleRate, uint16_t numChannels, uint32_t numSamples) {
    uint32_t byteRate = sampleRate * numChannels * 2; // 16-bit
    uint32_t dataSize = numSamples * numChannels * 2;
    uint32_t fileSize = 36 + dataSize;
    
    // RIFF header
    fwrite("RIFF", 1, 4, f);
    fwrite(&fileSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    
    // fmt chunk
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, f);
    uint16_t audioFormat = 1; // PCM
    fwrite(&audioFormat, 2, 1, f);
    fwrite(&numChannels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f);
    fwrite(&byteRate, 4, 1, f);
    uint16_t blockAlign = numChannels * 2;
    fwrite(&blockAlign, 2, 1, f);
    uint16_t bitsPerSample = 16;
    fwrite(&bitsPerSample, 2, 1, f);
    
    // data chunk
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);
}

// ----------------------------------------------------------------------------
// extractAudio - Extraction audio basique (DPCM16 → WAV 22050Hz mono)
// Basé sur la documentation de référence LZS_DECODER_DOCUMENTATION.md
// ----------------------------------------------------------------------------
void RbtParser::extractAudio(const char *outDir, size_t maxFrames) {
    if (!_hasAudio) {
        std::fprintf(stderr, "No audio in file\n");
        return;
    }

    // Si maxFrames == 0, extraire toutes les frames
    if (maxFrames == 0) maxFrames = _numFramesTotal;

    // Calcul de la taille du buffer audio final
    // Robot audio: 2 canaux (EVEN/ODD) à 11025 Hz chacun → 22050 Hz après entrelacement
    // Chaque frame @ 10fps = 0.1s → 2205 samples total (1102.5 par canal)
    const size_t samplesPerFrame = (_frameRate > 0) ? (22050 / _frameRate) : 2205;
    const size_t totalSamples = maxFrames * samplesPerFrame;
    std::vector<int16_t> audioBuffer(totalSamples, 0);

    // Positions d'écriture pour les canaux EVEN et ODD
    // EVEN: indices pairs (0, 2, 4, 6...) - ODD: indices impairs (1, 3, 5, 7...)
    size_t evenWritePos = 0;
    size_t oddWritePos = 1;

    // ========================================================================
    // ÉTAPE 1: Extraire les PRIMERS (si présents)
    // Les primers initialisent les buffers audio avant la lecture des frames
    // ========================================================================
    std::fprintf(stderr, "Audio extraction: evenPrimerSize=%d oddPrimerSize=%d\n",
                _evenPrimerSize, _oddPrimerSize);
    
    if (_evenPrimerSize > 0 && _oddPrimerSize > 0 && !_primerEvenRaw.empty() && !_primerOddRaw.empty()) {
        // Primer EVEN (canal pair)
        std::vector<int16_t> evenSamples(_evenPrimerSize);
        int16_t carry = 0;
        deDPCM16Mono(evenSamples.data(), _primerEvenRaw.data(), _evenPrimerSize, carry);
        
        // Écrire aux positions paires (0, 2, 4, 6...)
        for (size_t s = 0; s < evenSamples.size() && evenWritePos < totalSamples; ++s) {
            audioBuffer[evenWritePos] = evenSamples[s];
            evenWritePos += 2;
        }
        std::fprintf(stderr, "  Primer EVEN: %d samples written\n", _evenPrimerSize);
        
        // Primer ODD (canal impair)
        std::vector<int16_t> oddSamples(_oddPrimerSize);
        carry = 0;
        deDPCM16Mono(oddSamples.data(), _primerOddRaw.data(), _oddPrimerSize, carry);
        
        // Écrire aux positions impaires (1, 3, 5, 7...)
        for (size_t s = 0; s < oddSamples.size() && oddWritePos < totalSamples; ++s) {
            audioBuffer[oddWritePos] = oddSamples[s];
            oddWritePos += 2;
        }
        std::fprintf(stderr, "  Primer ODD: %d samples written\n", _oddPrimerSize);
    }

    // ========================================================================
    // ÉTAPE 2: Extraire les paquets audio des frames
    // Chaque paquet contient: [header 8 bytes][runway DPCM 8 bytes][données compressées]
    // IMPORTANT: Ne pas générer d'audio pour les frames skip (videoSize == 0)
    // ========================================================================
    size_t packetsProcessed = 0;
    
    for (size_t frameIdx = 0; frameIdx < maxFrames && frameIdx < _packetSizes.size(); ++frameIdx) {
        if (_packetSizes[frameIdx] == 0) continue;
        
        // Vérifier si c'est une frame skip (pas de vidéo)
        // Les frames skip ne doivent PAS générer d'audio pour maintenir la sync A/V
        if (frameIdx < _videoSizes.size() && _videoSizes[frameIdx] == 0) {
            std::fprintf(stderr, "  Frame %zu: skip (no video) - audio position not advanced\n", frameIdx);
            continue;
        }
        
        // Position de l'en-tête audio = position frame + taille vidéo
        uint64_t audioHeaderPos = (uint64_t)_recordPositions[frameIdx] + (uint64_t)_videoSizes[frameIdx];
        if (!seekSet((size_t)audioHeaderPos)) break;

        // Lire l'en-tête audio (8 bytes)
        int32_t audioAbsolutePosition = readSint32(_bigEndian);
        int32_t audioBlockSize = readSint32(_bigEndian);
        
        if (audioAbsolutePosition < 0 || audioBlockSize <= 0) continue;
        if (audioBlockSize > 10 * 1024 * 1024) continue;  // Sanity check
        
        const bool isEvenChannel = (audioAbsolutePosition % 2 == 0);
        
        // IMPORTANT: audioAbsolutePosition est la position dans le BUFFER FINAL entrelaçé
        size_t absoluteSamplePos = (size_t)audioAbsolutePosition;
        
        // Lire les données audio compressées
        // Note: audioBlockSize EXCLUT l'en-tête de 8 bytes (déjà lu)
        std::vector<uint8_t> compressedData(audioBlockSize);
        if (fread(compressedData.data(), 1, audioBlockSize, _f) != (size_t)audioBlockSize) {
            continue;
        }
        
        // Décompression DPCM16
        // Important: Chaque paquet commence avec sample = 0 (pas de carry entre paquets)
        std::vector<int16_t> decompressedSamples(audioBlockSize);
        int16_t sampleValue = 0;  // Valeur initiale = 0
        deDPCM16Mono(decompressedSamples.data(), compressedData.data(), audioBlockSize, sampleValue);
        
        // Selon la doc: "there is an 8-byte runway at the start of every audio block
        // that is never written to the output stream, which is used to move the signal
        // to the correct location by the 9th sample."
        const size_t kRunwaySamples = 8;
        
        // Écrire les samples directement à audioAbsolutePosition (déjà dans le buffer entrelaçé)
        for (size_t s = kRunwaySamples; s < decompressedSamples.size(); ++s) {
            size_t bufferPos = absoluteSamplePos + (s - kRunwaySamples) * 2;  // *2 car entrelacé
            if (bufferPos < totalSamples) {
                audioBuffer[bufferPos] = decompressedSamples[s];
            }
        }
        
        packetsProcessed++;
    }
    
    std::fprintf(stderr, "  Processed %zu audio packets from frames\n", packetsProcessed);

    // ========================================================================
    // ÉTAPE 3: Interpolation des canaux EVEN/ODD
    // L'interpolation lisse les transitions entre les deux canaux entrelacés
    // pour créer un flux audio continu sans discontinuités.
    // ========================================================================
    std::fprintf(stderr, "Interpolating missing samples...\n");
    interpolateChannel(audioBuffer.data(), totalSamples / 2, 0);
    interpolateChannel(audioBuffer.data(), totalSamples / 2, 1);

    // ========================================================================
    // ÉTAPE 4: Écrire le fichier WAV final (22050 Hz mono)
    // ========================================================================
    std::string audioPath = std::string(outDir) + "/audio.wav";
    FILE *audioFile = fopen(audioPath.c_str(), "wb");
    if (!audioFile) {
        std::fprintf(stderr, "Failed to create audio file: %s\n", audioPath.c_str());
        return;
    }
    
    writeWavHeader(audioFile, 22050, 1, audioBuffer.size());
    fwrite(audioBuffer.data(), sizeof(int16_t), audioBuffer.size(), audioFile);
    fclose(audioFile);
    
    std::fprintf(stderr, "\nWrote %s: %zu samples (%.2f seconds @ 22050Hz)\n",
                 audioPath.c_str(), audioBuffer.size(), (double)audioBuffer.size() / 22050.0);
}

// Surcharge acceptant un chemin complet pour le fichier WAV
void RbtParser::extractAudio(const std::string& outputWavPath, size_t maxFrames) {
    if (!_hasAudio) {
        std::fprintf(stderr, "No audio in file\n");
        return;
    }

    // Si maxFrames == 0, extraire toutes les frames
    if (maxFrames == 0) maxFrames = _numFramesTotal;

    const size_t samplesPerFrame = (_frameRate > 0) ? (22050 / _frameRate) : 2205;
    const size_t totalSamples = maxFrames * samplesPerFrame;
    std::vector<int16_t> audioBuffer(totalSamples, 0);

    size_t evenWritePos = 0;
    size_t oddWritePos = 1;

    std::fprintf(stderr, "Audio extraction: evenPrimerSize=%d oddPrimerSize=%d\n",
                _evenPrimerSize, _oddPrimerSize);
    
    if (_evenPrimerSize > 0 && _oddPrimerSize > 0 && !_primerEvenRaw.empty() && !_primerOddRaw.empty()) {
        std::vector<int16_t> evenSamples(_evenPrimerSize);
        int16_t carry = 0;
        deDPCM16Mono(evenSamples.data(), _primerEvenRaw.data(), _evenPrimerSize, carry);
        
        for (size_t s = 0; s < evenSamples.size() && evenWritePos < totalSamples; ++s) {
            audioBuffer[evenWritePos] = evenSamples[s];
            evenWritePos += 2;
        }
        std::fprintf(stderr, "  Primer EVEN: %d samples written\n", _evenPrimerSize);
        
        std::vector<int16_t> oddSamples(_oddPrimerSize);
        carry = 0;
        deDPCM16Mono(oddSamples.data(), _primerOddRaw.data(), _oddPrimerSize, carry);
        
        for (size_t s = 0; s < oddSamples.size() && oddWritePos < totalSamples; ++s) {
            audioBuffer[oddWritePos] = oddSamples[s];
            oddWritePos += 2;
        }
        std::fprintf(stderr, "  Primer ODD: %d samples written\n", _oddPrimerSize);
    }

    size_t packetsProcessed = 0;
    
    for (size_t frameIdx = 0; frameIdx < maxFrames && frameIdx < _packetSizes.size(); ++frameIdx) {
        if (_packetSizes[frameIdx] == 0) continue;
        
        // Vérifier si c'est une frame skip (pas de vidéo)
        // Les frames skip ne doivent PAS générer d'audio pour maintenir la sync A/V
        if (frameIdx < _videoSizes.size() && _videoSizes[frameIdx] == 0) {
            std::fprintf(stderr, "  Frame %zu: skip (no video) - audio position not advanced\n", frameIdx);
            continue;
        }
        
        uint64_t audioHeaderPos = (uint64_t)_recordPositions[frameIdx] + (uint64_t)_videoSizes[frameIdx];
        if (!seekSet((size_t)audioHeaderPos)) break;

        int32_t audioAbsolutePosition = readSint32(_bigEndian);
        int32_t audioBlockSize = readSint32(_bigEndian);
        
        if (audioAbsolutePosition < 0 || audioBlockSize <= 0) continue;
        if (audioBlockSize > 10 * 1024 * 1024) continue;
        
        const bool isEvenChannel = (audioAbsolutePosition % 2 == 0);
        
        // IMPORTANT: audioAbsolutePosition est la position dans le BUFFER FINAL entrelaçé
        // Pas la position dans le canal! Il faut donc l'utiliser directement.
        // Pour EVEN: audioAbsolutePosition sera 0, 2, 4, 6...
        // Pour ODD: audioAbsolutePosition sera 1, 3, 5, 7...
        size_t absoluteSamplePos = (size_t)audioAbsolutePosition;
        
        std::vector<uint8_t> compressedData(audioBlockSize);
        if (fread(compressedData.data(), 1, audioBlockSize, _f) != (size_t)audioBlockSize) {
            continue;
        }
        
        std::vector<int16_t> decompressedSamples(audioBlockSize);
        int16_t sampleValue = 0;
        deDPCM16Mono(decompressedSamples.data(), compressedData.data(), audioBlockSize, sampleValue);
        
        const size_t kRunwaySamples = 8;
        
        // Écrire les samples directement à audioAbsolutePosition (déjà dans le buffer entrelaçé)
        for (size_t s = kRunwaySamples; s < decompressedSamples.size(); ++s) {
            size_t bufferPos = absoluteSamplePos + (s - kRunwaySamples) * 2;  // *2 car entrelacé
            if (bufferPos < totalSamples) {
                audioBuffer[bufferPos] = decompressedSamples[s];
            }
        }
        
        packetsProcessed++;
    }
    
    std::fprintf(stderr, "  Processed %zu audio packets from frames\n", packetsProcessed);
    std::fprintf(stderr, "Interpolating missing samples...\n");
    interpolateChannel(audioBuffer.data(), totalSamples / 2, 0);
    interpolateChannel(audioBuffer.data(), totalSamples / 2, 1);

    FILE *audioFile = fopen(outputWavPath.c_str(), "wb");
    if (!audioFile) {
        std::fprintf(stderr, "Failed to create audio file: %s\n", outputWavPath.c_str());
        return;
    }
    
    writeWavHeader(audioFile, 22050, 1, audioBuffer.size());
    fwrite(audioBuffer.data(), sizeof(int16_t), audioBuffer.size(), audioFile);
    fclose(audioFile);
    
    std::fprintf(stderr, "\nWrote %s: %zu samples (%.2f seconds @ 22050Hz)\n",
                 outputWavPath.c_str(), audioBuffer.size(), (double)audioBuffer.size() / 22050.0);
}

// ============================================================================
// extractFramePixels - Extrait les pixels indexés d'une frame (sans conversion RGB)
// ============================================================================
bool RbtParser::extractFramePixels(size_t frameIndex, std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight) {
    if (frameIndex >= _recordPositions.size()) {
        return false;
    }
    
    // Position de la frame
    size_t startPos = _recordPositions[frameIndex];
    if (!seekSet(startPos)) return false;
    
    // Lire le nombre de cels
    uint16_t numCels = _bigEndian ? readUint16BE() : readUint16LE();
    if (numCels == 0 || numCels > 10) return false;
    
    // Pour Robot, résolution dépend des cels eux-mêmes
    // On scanne tous les cels pour trouver les dimensions maximales
    outWidth = 320;   // Minimum par défaut
    outHeight = 240;
    
    // Lire les données brutes de la frame pour trouver les dimensions réelles
    if (frameIndex < _videoSizes.size()) {
        std::vector<uint8_t> tempData(_videoSizes[frameIndex]);
        seekSet(startPos);
        if (fread(tempData.data(), 1, _videoSizes[frameIndex], _f) == _videoSizes[frameIndex]) {
            const uint8_t *tp = tempData.data() + 2;  // Skip numCels
            for (uint16_t c = 0; c < numCels; ++c) {
                if ((size_t)(tp - tempData.data()) + 22 > tempData.size()) break;
                
                uint16_t cw = SciHelpers::READ_SCI11ENDIAN_UINT16(tp + 2);
                uint16_t ch = SciHelpers::READ_SCI11ENDIAN_UINT16(tp + 4);
                uint16_t cx = SciHelpers::READ_SCI11ENDIAN_UINT16(tp + 10);
                uint16_t cy = SciHelpers::READ_SCI11ENDIAN_UINT16(tp + 12);
                uint16_t dsz = SciHelpers::READ_SCI11ENDIAN_UINT16(tp + 14);
                
                int maxX = cx + cw;
                int maxY = cy + ch;
                if (maxX > outWidth) outWidth = maxX;
                if (maxY > outHeight) outHeight = maxY;
                
                tp += 22 + dsz;
            }
        }
    }
    
    outPixels.assign(outWidth * outHeight, 255);  // Fond transparent (skip=255)
    
    // Lire les données brutes de la frame
    std::vector<uint8_t> rawVideoData(_videoSizes[frameIndex]);
    seekSet(startPos);
    if (fread(rawVideoData.data(), 1, _videoSizes[frameIndex], _f) != _videoSizes[frameIndex]) {
        return false;
    }
    
    const uint8_t *p = rawVideoData.data() + 2;  // Skip numCels
    
    // Traiter chaque cel et les composer dans le buffer final
    for (uint16_t celIdx = 0; celIdx < numCels; ++celIdx) {
        // Lire l'en-tête du cel (22 bytes) - MÊME ORDRE QUE createCel5
        const uint8_t horizontalScale = p[0];
        const uint8_t verticalScale = p[1];
        const uint16_t celWidth = SciHelpers::READ_SCI11ENDIAN_UINT16(p + 2);
        const uint16_t celHeight = SciHelpers::READ_SCI11ENDIAN_UINT16(p + 4);
        // Note: offset 6-9 sont d'autres champs (priorité, etc.)
        const uint16_t celX = SciHelpers::READ_SCI11ENDIAN_UINT16(p + 10);
        const uint16_t celY = SciHelpers::READ_SCI11ENDIAN_UINT16(p + 12);
        const uint16_t dataSize = SciHelpers::READ_SCI11ENDIAN_UINT16(p + 14);
        const int16_t numDataChunks = (int16_t)SciHelpers::READ_SCI11ENDIAN_UINT16(p + 16);
        
        p += 22;  // kCelHeaderSize
        
        if (celWidth == 0 || celHeight == 0 || numDataChunks <= 0) {
            return false;
        }
        
        // Calculer la taille de décompression
        const int verticalScaleFactor = verticalScale;
        int sourceHeight = (verticalScaleFactor == 100) ? celHeight : (celHeight * verticalScaleFactor) / 100;
        if (sourceHeight <= 0) sourceHeight = 1;
        const size_t decompressedArea = (size_t)celWidth * (size_t)sourceHeight;
        
        std::vector<uint8_t> decompressed;
        decompressed.reserve(decompressedArea);
        
        // Décompresser les chunks - UTILISER 10 BYTES HEADER COMME createCel5
        for (int16_t chunk = 0; chunk < numDataChunks; ++chunk) {
            const uint32_t compSize = SciHelpers::READ_SCI11ENDIAN_UINT32(p);
            const uint32_t decompSize = SciHelpers::READ_SCI11ENDIAN_UINT32(p + 4);
            const uint16_t compressionType = SciHelpers::READ_SCI11ENDIAN_UINT16(p + 8);
            p += 10;  // Header size: 4 + 4 + 2 = 10 bytes
            
            if (compressionType == 2) {  // kCompressionNone
                decompressed.insert(decompressed.end(), p, p + decompSize);
            } else if (compressionType == 0) {  // kCompressionLZS
                Common::MemoryReadStream mrs(p, compSize);
                DecompressorLZS dec;
                std::vector<uint8_t> out(decompSize);
                int rc = dec.unpack(&mrs, out.data(), compSize, decompSize);
                if (rc != 0) return false;
                decompressed.insert(decompressed.end(), out.begin(), out.end());
            } else {
                return false;
            }
            
            p += compSize;
        }
        
        // Expansion verticale - MÊME ALGORITHME QUE createCel5
        const size_t area = (size_t)celWidth * (size_t)celHeight;
        std::vector<uint8_t> finalCelPixels(area, 0);
        
        if (verticalScaleFactor == 100) {
            if (decompressed.size() >= area) {
                std::copy_n(decompressed.data(), area, finalCelPixels.data());
            }
        } else {
            // Algorithme d'expansion de createCel5 (de bas en haut)
            int numerator = celHeight;
            int denominator = sourceHeight;
            int remainder = 0;
            const uint8_t *srcPtr = decompressed.data();
            uint8_t *dstPtr = finalCelPixels.data();
            
            for (int y = sourceHeight - 1; y >= 0; --y) {
                remainder += numerator;
                int linesToDraw = remainder / denominator;
                remainder %= denominator;
                
                for (int l = 0; l < linesToDraw; ++l) {
                    if ((size_t)(srcPtr - decompressed.data()) + celWidth <= decompressed.size()) {
                        std::copy_n(srcPtr, celWidth, dstPtr);
                    } else {
                        std::fill_n(dstPtr, celWidth, 0);
                    }
                    dstPtr += celWidth;
                }
                srcPtr += celWidth;
            }
        }
        
        // Composer le cel dans le buffer final
        for (int y = 0; y < (int)celHeight && (celY + y) < (int)outHeight; ++y) {
            for (int x = 0; x < (int)celWidth && (celX + x) < (int)outWidth; ++x) {
                uint8_t pixelIdx = finalCelPixels[y * celWidth + x];
                // Écrire TOUS les pixels (y compris skip=255)
                outPixels[(celY + y) * outWidth + (celX + x)] = pixelIdx;
            }
        }
    }
    
    return true;
}
