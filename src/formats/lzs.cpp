#include "lzs.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>

// More faithful, bounds-checked memory port of ScummVM's DecompressorLZS
class BitReader {
public:
    const uint8_t *_data;
    size_t _size;
    size_t _pos;
    uint32_t _dwBits;
    int _nBits;

    BitReader(const uint8_t *data, size_t size) : _data(data), _size(size), _pos(0), _dwBits(0), _nBits(0) {}

    bool eof() const { return _pos >= _size && _nBits == 0; }

    uint8_t readByte() { return (_pos < _size) ? _data[_pos++] : 0; }

    void fetchBitsMSB() {
        while (_nBits <= 24 && _pos < _size) {
            _dwBits |= (uint32_t)readByte() << (24 - _nBits);
            _nBits += 8;
        }
    }

    uint32_t getBitsMSB(int n) {
        if (_nBits < n) fetchBitsMSB();
        if (_nBits < n) { // not enough bits, pad with zeros
            uint32_t ret = _dwBits >> (32 - n);
            _dwBits = 0;
            _nBits = 0;
            return ret;
        }
        uint32_t ret = _dwBits >> (32 - n);
        _dwBits <<= n;
        _nBits -= n;
        return ret;
    }

    uint8_t getByteMSB() { return static_cast<uint8_t>(getBitsMSB(8)); }
};

int LZSDecompress(const uint8_t *in, uint32_t inSize, uint8_t *out, uint32_t outSize) {
    BitReader r(in, inSize);
    uint32_t wrote = 0;

    auto putByte = [&](uint8_t b) {
        if (wrote < outSize) out[wrote++] = b;
        else /* overflow */ wrote++;
    };

    auto getCompLen = [&]() -> uint32_t {
        uint32_t v = r.getBitsMSB(2);
        if (v == 0) return 2;
        if (v == 1) return 3;
        if (v == 2) return 4;
        v = r.getBitsMSB(2);
        if (v == 0) return 5;
        if (v == 1) return 6;
        if (v == 2) return 7;
        uint32_t clen = 8;
        while (true) {
            uint32_t nib = r.getBitsMSB(4);
            clen += nib;
            if (nib != 0xF) break;
        }
        return clen;
    };

    while (!r.eof()) {
        uint32_t flag = r.getBitsMSB(1);
        if (flag) {
            uint32_t type = r.getBitsMSB(1);
            uint16_t offs = static_cast<uint16_t>(type ? r.getBitsMSB(7) : r.getBitsMSB(11));
            if (offs == 0) break; // end marker
            uint32_t clen = getCompLen();
            if (offs == 0 || offs > wrote) return 1; // invalid offset
            uint32_t srcPos = wrote - offs;
            for (uint32_t i = 0; i < clen; ++i) {
                if (srcPos + i >= wrote) return 1; // invalid reference
                putByte(out[srcPos + i]);
            }
        } else {
            uint8_t b = r.getByteMSB();
            putByte(b);
        }
        if (wrote > outSize) return 1; // overflow
        if (wrote == outSize) break;
    }

    return (wrote == outSize) ? 0 : 1;
}
