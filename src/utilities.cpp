#include "utilities.hpp"

#include <iomanip>
#include <iostream>
#include <mutex>
#include <algorithm>
#include <array>

namespace robot {

bool g_quiet = false;
bool g_force_be = false;
bool g_force_le = false;

void read_exact(std::ifstream &f, void *data, size_t size) {
    auto old = f.exceptions();
    f.exceptions(std::ios::goodbit);
    f.read(static_cast<char *>(data), static_cast<std::streamsize>(size));
    std::streamsize got = f.gcount();
    f.exceptions(old);
    if (got != static_cast<std::streamsize>(size)) {
        throw std::runtime_error("Lecture incomplète (" +
                                 std::to_string(got) + "/" +
                                 std::to_string(size) + " octets)");
    }
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

template <Integral T>
T read_scalar(std::span<const std::byte> data, bool bigEndian) {
    constexpr size_t size = sizeof(T);
    if (data.size() < size) {
        throw std::runtime_error("Échec de la lecture de " +
                                 std::to_string(size) +
                                 " octets");
    }
    T value;
    if constexpr (size == 1) {
        value = static_cast<T>(std::to_integer<uint8_t>(data[0]));
    } else {
        if (bigEndian != (std::endian::native == std::endian::big)) {
#if defined(__cpp_lib_byteswap) || (__cplusplus >= 202302L)
            std::memcpy(&value, data.data(), size);
            value = std::byteswap(value);
#else
            std::array<std::byte, size> swapped_bytes;
            std::reverse_copy(data.begin(), data.begin() + size, swapped_bytes.begin());
            std::memcpy(&value, swapped_bytes.data(), size);
#endif
        } else {
            std::memcpy(&value, data.data(), size);
        }
    }
    return value;
}


namespace {

std::mutex g_logMutex;

void log(const std::filesystem::path &path, const std::string &msg, const char *prefix) {
    if (g_quiet) return;
    std::lock_guard lock(g_logMutex);
    std::cerr << prefix << path.string() << ": " << msg << '\n';
}

} // namespace

void log_info(const std::filesystem::path &path, const std::string &msg) {
    log(path, msg, "");
}

void log_warn(const std::filesystem::path &path, const std::string &msg) {
    log(path, msg, "AVERTISSEMENT: ");
}

void log_error(const std::filesystem::path &path, const std::string &msg) {
    log(path, msg, "ERREUR: ");
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
        uint8_t control = static_cast<uint8_t>(in[in_pos++]);
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
                uint8_t byte1 = static_cast<uint8_t>(in[in_pos++]);
                uint8_t byte2 = static_cast<uint8_t>(in[in_pos++]);
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

    if (out_pos != expected_size) {
        throw std::runtime_error("Taille décompressée (" + std::to_string(out_pos) +
                                 ") ne correspond pas à la taille attendue (" +
                                 std::to_string(expected_size) + ")");
    }
    return out;
}

std::vector<int16_t> dpcm16_decompress(std::span<const std::byte> in, int16_t &carry) {
    std::vector<int16_t> out;
    if (in.size() > SIZE_MAX / 2) {
        throw std::runtime_error("Entrée trop volumineuse");
    }
    out.reserve(in.size() * 2);

    static constexpr std::array<int16_t, 16> DPCM_TABLE = {
        -0x0c0, -0x080, -0x040, -0x020,
        -0x010, -0x008, -0x004, -0x002,
         0x002,  0x004,  0x008,  0x010,
         0x020,  0x040,  0x080,  0x0c0,
    };

    int32_t predictor = carry;
    for (auto byte : in) {
        uint8_t b = static_cast<uint8_t>(byte);
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

template uint8_t read_scalar<uint8_t>(std::span<const std::byte>, bool);
template int8_t read_scalar<int8_t>(std::span<const std::byte>, bool);
template uint16_t read_scalar<uint16_t>(std::span<const std::byte>, bool);
template int16_t read_scalar<int16_t>(std::span<const std::byte>, bool);
template uint32_t read_scalar<uint32_t>(std::span<const std::byte>, bool);
template int32_t read_scalar<int32_t>(std::span<const std::byte>, bool);

} // namespace robot
