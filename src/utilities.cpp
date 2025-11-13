// Implémentation des utilitaires (I/O, journalisation, PNG, LZS, DPCM16)
#include "utilities.hpp"

#include <iomanip>
#include <iostream>
#include <mutex>
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <system_error>

#include "stb_image_write.h"

namespace robot {

std::streamsize checked_streamsize(size_t size) {
    if (size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("Taille dépasse la limite de streamsize");
    }
    return static_cast<std::streamsize>(size);
}

void read_exact(std::ifstream &f, void *data, size_t size) {
    std::streamsize ss = checked_streamsize(size);
    auto old = f.exceptions();
    f.exceptions(std::ios::goodbit);
    auto start = f.tellg();
    f.read(static_cast<char *>(data), ss);
    std::streamsize got = f.gcount();
    f.clear();
    if (got != ss) {
        f.seekg(start);
    }
    f.exceptions(old);
    if (got != ss) {
        throw std::runtime_error("Lecture incomplète (" +
                                 std::to_string(got) + "/" +
                                 std::to_string(size) + " octets)");
    }
}

bool detect_endianness(std::ifstream &f) {
    StreamExceptionGuard guard(f);
    auto start = f.tellg();
    // La détection d'endianess s'effectue sur le champ de version situé à
    // l'offset 6. Le moteur original lit ce champ en big-endian et considère
    // que le flux est big-endian si la valeur obtenue est comprise entre
    // 0x0001 et 0x00ff. Nous reproduisons ce comportement et vérifions ensuite
    // l'interprétation little-endian comme garde-fou.
    f.seekg(start + static_cast<std::streamoff>(6));
    std::array<uint8_t, 2> verBytes{};
    f.read(reinterpret_cast<char *>(verBytes.data()), 2);
    uint16_t be = static_cast<uint16_t>(verBytes[0]) << 8 |
                  static_cast<uint16_t>(verBytes[1]);
    uint16_t le = static_cast<uint16_t>(verBytes[0]) |
                  (static_cast<uint16_t>(verBytes[1]) << 8);    
    f.seekg(start);
    // Si l'interprétation big-endian correspond à la plage utilisée par le
    // moteur historique, on considère le flux comme big-endian.
    if (be >= 0x0001 && be <= 0x00ff) {
        return true;
    }
    if (le >= 4 && le <= 6) {
        return false;
    }
    throw std::runtime_error("Version Robot invalide");
}

void append_le16(std::vector<std::byte> &out, uint16_t value) {
    for (int i = 0; i < 2; ++i) {
        out.push_back(std::byte((value >> (i * 8)) & 0xFF));
    }
}

void append_le32(std::vector<std::byte> &out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(std::byte((value >> (i * 8)) & 0xFF));
    }
}

void write_le16(char *dst, uint16_t value) {
    dst[0] = static_cast<char>(value & 0xFF);
    dst[1] = static_cast<char>((value >> 8) & 0xFF);
}

void write_le32(char *dst, uint32_t value) {
    dst[0] = static_cast<char>(value & 0xFF);
    dst[1] = static_cast<char>((value >> 8) & 0xFF);
    dst[2] = static_cast<char>((value >> 16) & 0xFF);
    dst[3] = static_cast<char>((value >> 24) & 0xFF);
}

namespace {

std::mutex g_logMutex;

void log(const std::filesystem::path &path, const std::string &msg,
         const char *prefix, const ExtractorOptions &opt) {
    if (opt.quiet) return;
    std::lock_guard lock(g_logMutex);
    std::cerr << prefix << path.string() << ": " << msg << '\n';
}

} // namespace

void log_info(const std::filesystem::path &path, const std::string &msg,
              const ExtractorOptions &opt) {
    log(path, msg, "", opt);
}

void log_warn(const std::filesystem::path &path, const std::string &msg,
              const ExtractorOptions &opt) {
    log(path, msg, "AVERTISSEMENT: ", opt);
}

void log_error(const std::filesystem::path &path, const std::string &msg,
               const ExtractorOptions &opt) {
    log(path, msg, "ERREUR: ", opt);
}

#ifdef _WIN32
std::wstring make_long_path(const std::wstring &path) {
    namespace fs = std::filesystem;
    fs::path absPath = fs::absolute(path);
    std::wstring abs = absPath.native();

    if (abs.rfind(L"\\\\?\\", 0) == 0) {
        return abs;
    }

    bool isUnc = abs.rfind(L"\\\\", 0) == 0;
    if (!isUnc && abs.length() >= MAX_PATH) {
        return L"\\\\?\\" + abs;
    }
    return abs;
}
#endif

#ifdef _WIN32
std::pair<std::filesystem::path, std::string>
to_long_path(const std::filesystem::path &path) {
    auto longPath = make_long_path(path.wstring());
    std::filesystem::path fsPath{longPath};
    auto pathUtf8 = fsPath.u8string();
    return {fsPath, std::string(pathUtf8.begin(), pathUtf8.end())};
}
#else
std::pair<std::filesystem::path, std::string>
to_long_path(const std::filesystem::path &path) {
    return {path, path.string()};
}
#endif

void write_png_cross_platform(const std::filesystem::path &path, int w, int h,
                              int comp, const void *data, int stride) {
    auto [fsPath, pathStr] = to_long_path(path);
    if (!stbi_write_png(pathStr.c_str(), w, h, comp, data, stride)) {
        std::error_code ec;
        std::filesystem::remove(fsPath, ec);
        throw std::runtime_error(std::string("Échec de l'écriture de ") + pathStr);
    }
}

namespace {

class BitReaderMSB {
public:
    explicit BitReaderMSB(std::span<const std::byte> data) : m_data(data) {}

    uint32_t getBits(int count) {
        if (count <= 0 || count > 32) {
            throw std::runtime_error("Lecture de bits LZS invalide");
        }
        ensureBits(count);
        uint32_t value = m_bits >> (32 - count);
        m_bits <<= count;
        m_bitCount -= count;
        return value;
    }

    uint8_t getByte() { return static_cast<uint8_t>(getBits(8)); }

private:
    void ensureBits(int count) {
        while (m_bitCount < count) {
            if (m_position >= m_data.size()) {
                throw std::runtime_error(
                    "Flux LZS malformé: fin de données");
            }
            uint8_t next = std::to_integer<uint8_t>(m_data[m_position++]);
            m_bits |= static_cast<uint32_t>(next) << (24 - m_bitCount);
            m_bitCount += 8;
        }
    }

    std::span<const std::byte> m_data;
    size_t m_position = 0;
    uint32_t m_bits = 0;
    int m_bitCount = 0;
};

constexpr size_t kMaxOffset = (1u << 11) - 1;

size_t getCompressedLength(BitReaderMSB &reader) {
    switch (reader.getBits(2)) {
    case 0:
        return 2;
    case 1:
        return 3;
    case 2:
        return 4;
    default:
        switch (reader.getBits(2)) {
        case 0:
            return 5;
        case 1:
            return 6;
        case 2:
            return 7;
        default: {
            size_t length = 8;
            uint32_t nibble;
            do {
                nibble = reader.getBits(4);
                length += nibble;
            } while (nibble == 0xF);
            return length;
        }
        }
    }
}

} // namespace

std::vector<std::byte> lzs_decompress(std::span<const std::byte> in,
                                      size_t expected_size,
                                      std::span<const std::byte> history) {
    if (expected_size > kMaxLzsOutput) {
        throw std::runtime_error("Taille décompressée trop grande: " +
                                 std::to_string(expected_size) +
                                 " > " +
                                 std::to_string(kMaxLzsOutput));
    }

    BitReaderMSB reader(in);

    size_t history_to_copy = history.size();
    if (history_to_copy > kMaxOffset) {
        history_to_copy = kMaxOffset;
    }

    std::vector<std::byte> dictionary;
    dictionary.reserve(history_to_copy + expected_size);
    if (history_to_copy > 0) {
        dictionary.insert(dictionary.end(), history.end() - history_to_copy,
                          history.end());
    }
    
    std::vector<std::byte> out(expected_size);
    size_t produced = 0;

    auto append_literal = [&](std::byte value) {
        if (produced >= expected_size) {
            throw std::runtime_error(
                "Taille décompressée dépasse la taille attendue");
        }
        dictionary.push_back(value);
        if (produced < out.size()) {
            out[produced] = value;
        }
        ++produced;
    };

    auto copy_match = [&](size_t offset, size_t length) {
        if (offset == 0 || offset > dictionary.size()) {
            throw std::runtime_error("Offset LZS invalide");
        }
        size_t src_index = dictionary.size() - offset;
        for (size_t i = 0; i < length; ++i) {
            if (src_index >= dictionary.size()) {
                throw std::runtime_error("Lecture hors limites dans LZS");
            }
            append_literal(dictionary[src_index++]);
        }
    };

    while (produced < expected_size) {
        if (reader.getBits(1) == 0) {
            append_literal(static_cast<std::byte>(reader.getByte()));
            continue;
        }
        
        bool short_offset = reader.getBits(1) != 0;
        size_t offset = short_offset ? reader.getBits(7) : reader.getBits(11);
        if (short_offset && offset == 0) {
            break;
        }
        if (offset == 0) {
            throw std::runtime_error("Offset LZS nul");
        }
        size_t length = getCompressedLength(reader);
        if (length == 0) {
            throw std::runtime_error("Longueur LZS invalide");
        }
        copy_match(offset, length);
    }

    if (produced != expected_size) {
        throw std::runtime_error("Taille décompressée (" +
                                 std::to_string(produced) +
                                 ") ne correspond pas à la taille attendue (" +
                                 std::to_string(expected_size) + ")");
    }
    
    return out;
}

namespace {

constexpr std::array<uint16_t, 128> kDpcm16StepTable = {
    0x0000, 0x0008, 0x0010, 0x0020, 0x0030, 0x0040, 0x0050, 0x0060,
    0x0070, 0x0080, 0x0090, 0x00A0, 0x00B0, 0x00C0, 0x00D0, 0x00E0,
    0x00F0, 0x0100, 0x0110, 0x0120, 0x0130, 0x0140, 0x0150, 0x0160,
    0x0170, 0x0180, 0x0190, 0x01A0, 0x01B0, 0x01C0, 0x01D0, 0x01E0,
    0x01F0, 0x0200, 0x0208, 0x0210, 0x0218, 0x0220, 0x0228, 0x0230,
    0x0238, 0x0240, 0x0248, 0x0250, 0x0258, 0x0260, 0x0268, 0x0270,
    0x0278, 0x0280, 0x0288, 0x0290, 0x0298, 0x02A0, 0x02A8, 0x02B0,
    0x02B8, 0x02C0, 0x02C8, 0x02D0, 0x02D8, 0x02E0, 0x02E8, 0x02F0,
    0x02F8, 0x0300, 0x0308, 0x0310, 0x0318, 0x0320, 0x0328, 0x0330,
    0x0338, 0x0340, 0x0348, 0x0350, 0x0358, 0x0360, 0x0368, 0x0370,
    0x0378, 0x0380, 0x0388, 0x0390, 0x0398, 0x03A0, 0x03A8, 0x03B0,
    0x03B8, 0x03C0, 0x03C8, 0x03D0, 0x03D8, 0x03E0, 0x03E8, 0x03F0,
    0x03F8, 0x0400, 0x0440, 0x0480, 0x04C0, 0x0500, 0x0540, 0x0580,
    0x05C0, 0x0600, 0x0640, 0x0680, 0x06C0, 0x0700, 0x0740, 0x0780,
    0x07C0, 0x0800, 0x0900, 0x0A00, 0x0B00, 0x0C00, 0x0D00, 0x0E00,
    0x0F00, 0x1000, 0x1400, 0x1800, 0x1C00, 0x2000, 0x3000, 0x4000};

int16_t apply_dpcm16_step(int16_t predictor, uint8_t control) {
    int32_t value = predictor;
    const int32_t delta = static_cast<int32_t>(kDpcm16StepTable[control & 0x7F]);
    if ((control & 0x80) != 0) {
        value -= delta;
    } else {
        value += delta;
    }
    // Emulating x86 16-bit signed register overflow (same as ScummVM)
    if (value > 32767) {
        value -= 65536;
    } else if (value < -32768) {
        value += 65536;
    }
    return static_cast<int16_t>(value);
}

} // namespace

std::vector<int16_t> dpcm16_decompress(std::span<const std::byte> in, int16_t &carry) {
    std::vector<int16_t> out;
    out.reserve(in.size());

    int16_t sample = carry;
    for (auto byte : in) {
        sample = apply_dpcm16_step(sample, std::to_integer<uint8_t>(byte));
        out.push_back(sample);
    }

    carry = sample;
    return out;
}

void dpcm16_decompress_last(std::span<const std::byte> in, int16_t &carry) {
    int16_t sample = carry;
    for (auto byte : in) {
        sample = apply_dpcm16_step(sample, std::to_integer<uint8_t>(byte));
    }
    carry = sample;
}

} // namespace robot
