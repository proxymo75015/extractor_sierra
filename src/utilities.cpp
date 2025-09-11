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
    f.read(static_cast<char *>(data), ss);
    std::streamsize got = f.gcount();
    f.clear();
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
    std::array<uint8_t, 2> sigBytes{};
    f.read(reinterpret_cast<char *>(sigBytes.data()), 2);
    if (f.gcount() != 2) {
        throw std::runtime_error("Signature Robot invalide");
    }
    uint16_t le = static_cast<uint16_t>(sigBytes[0]) |
                   (static_cast<uint16_t>(sigBytes[1]) << 8);
    uint16_t be = static_cast<uint16_t>(sigBytes[0]) << 8 |
                   static_cast<uint16_t>(sigBytes[1]);
    f.seekg(start);
    if (le == 0x16 || le == 0x3d) {
        return false;
    }
    if (be == 0x16 || be == 0x3d) {
        return true;
    }
    throw std::runtime_error("Signature Robot invalide");
}

void append_le16(std::vector<std::byte> &out, uint16_t value) {
    for (int i = 0; i < 2; ++i) {
        out.push_back(std::byte(value >> (i * 8)));
    }
}

void append_le32(std::vector<std::byte> &out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(std::byte(value >> (i * 8)));
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

void write_png_cross_platform(const std::filesystem::path &path, int w, int h,
                              int comp, const void *data, int stride) {
#ifdef _WIN32
    auto longPath = make_long_path(path.wstring());
    auto pathUtf8 = std::filesystem::path{longPath}.u8string();
    std::string pathUtf8Str(pathUtf8.begin(), pathUtf8.end());
    if (!stbi_write_png(pathUtf8Str.c_str(), w, h, comp, data, stride)) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(pathUtf8Str), ec);
        throw std::runtime_error(std::string("Échec de l'écriture de ") + pathUtf8Str);
    }
#else
    auto pathStr = path.string();
    if (!stbi_write_png(pathStr.c_str(), w, h, comp, data, stride)) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        throw std::runtime_error(std::string("Échec de l'écriture de ") + pathStr);
    }
#endif
}

std::vector<std::byte> lzs_decompress(std::span<const std::byte> in, size_t expected_size) {
    if (expected_size > kMaxLzsOutput) {
        throw std::runtime_error("Taille décompressée trop grande: " + 
                                 std::to_string(expected_size) + 
                                 " > " + 
                                 std::to_string(kMaxLzsOutput));
    }
    std::vector<std::byte> out(expected_size);
    size_t out_pos = 0;
    size_t in_pos = 0;

    while (in_pos < in.size()) {
        uint8_t control = std::to_integer<uint8_t>(in[in_pos++]);
        for (int i = 0; i < 8 && in_pos < in.size(); ++i) {
            if (control & (1 << i)) {
                if (out_pos >= out.size()) {
                    throw std::runtime_error("Dépassement du tampon de sortie");
                }
                out[out_pos++] = in[in_pos++];
            } else {
                if (in_pos + 1 >= in.size()) {
                    throw std::runtime_error("Données d'entrée insuffisantes pour LZS");
                }
                uint8_t byte1 = std::to_integer<uint8_t>(in[in_pos++]);
                uint8_t byte2 = std::to_integer<uint8_t>(in[in_pos++]);
                uint16_t offset = ((byte1 & 0xF0) << 4) | byte2;
                uint8_t length = (byte1 & 0x0F) + 3;
                if (offset == 0) {
                    throw std::runtime_error("Offset zéro interdit en LZS");
                }
                for (uint8_t j = 0; j < length; ++j) {
                    if (out_pos >= out.size()) {
                        throw std::runtime_error("Dépassement du tampon de sortie");
                    }
                    size_t src_pos = out_pos - offset;
                    if (src_pos >= out_pos) {
                        throw std::runtime_error("Offset LZS invalide");
                    }
                    out[out_pos++] = out[src_pos];
                }
            }
        }
    }

    if (in_pos != in.size()) {
        throw std::runtime_error("Flux LZS malformé: octets non traités");
    }
    
    if (out_pos != expected_size) {
        throw std::runtime_error("Taille décompressée (" + std::to_string(out_pos) +
                                 ") ne correspond pas à la taille attendue (" +
                                 std::to_string(expected_size) + ")");
    }
    return out;
}

constexpr std::array<int16_t, 16> DPCM_TABLE = {
    -0x0c0, -0x080, -0x040, -0x020,
    -0x010, -0x008, -0x004, -0x002,
     0x002,  0x004,  0x008,  0x010,
     0x020,  0x040,  0x080,  0x0c0,
};

std::vector<int16_t> dpcm16_decompress(std::span<const std::byte> in, int16_t &carry) {
    std::vector<int16_t> out;
    if (in.size() > SIZE_MAX / 2) {
        throw std::runtime_error("Entrée trop volumineuse");
    }
    out.reserve(in.size() * 2);

    int32_t predictor = carry;
    for (auto byte : in) {
        uint8_t b = std::to_integer<uint8_t>(byte);
        uint8_t hi = b >> 4;
        uint8_t lo = b & 0x0F;
        for (uint8_t nib : {hi, lo}) {
            predictor += DPCM_TABLE[nib];
            predictor = std::clamp(predictor, -32768, 32767);
            carry = static_cast<int16_t>(predictor);
            out.push_back(carry);
        }
    }
    
    return out;
}

void dpcm16_decompress_last(std::span<const std::byte> in, int16_t &carry) {
    int32_t predictor = carry;
    for (auto byte : in) {
        uint8_t b = std::to_integer<uint8_t>(byte);
        uint8_t hi = b >> 4;
        predictor += DPCM_TABLE[hi];
        predictor = std::clamp(predictor, -32768, 32767);
        carry = static_cast<int16_t>(predictor);
        uint8_t lo = b & 0x0F;
        predictor += DPCM_TABLE[lo];
        predictor = std::clamp(predictor, -32768, 32767);
        carry = static_cast<int16_t>(predictor);
    }
}

} // namespace robot
