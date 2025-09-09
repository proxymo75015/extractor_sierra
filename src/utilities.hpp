#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <bit>
#include <cstring>
#include <fstream>
#include <span>
#include <vector>
#include <algorithm>
#include <type_traits>

#ifdef _WIN32
#include <windows.h>
#endif

namespace robot {

constexpr int kMaxXRes = 7680;
constexpr int kMaxYRes = 4320;

struct ExtractorOptions {
    bool quiet = false;
    bool force_be = false;
    bool force_le = false;
    int max_x_res = kMaxXRes;
    int max_y_res = kMaxYRes;
};

template <typename T>
concept Integral = std::is_integral_v<T>;

template <Integral T>
T read_scalar(std::ifstream &f, bool bigEndian) {
    constexpr size_t size = sizeof(T);
    std::array<uint8_t, size> bytes;
    f.read(reinterpret_cast<char *>(bytes.data()), size);
    if (f.gcount() != static_cast<std::streamsize>(size)) {
        throw std::runtime_error("Échec de la lecture de " + std::to_string(size) + " octets");
    }
    T value;
    if constexpr (size == 1) {
        value = bytes[0];
    } else {
        if (bigEndian != (std::endian::native == std::endian::big)) {
#if defined(__cpp_lib_byteswap) || (__cplusplus >= 202302L)
            std::memcpy(&value, bytes.data(), size);
            value = std::byteswap(value);
#else
            std::array<uint8_t, size> swapped_bytes;
            std::reverse_copy(bytes.begin(), bytes.end(), swapped_bytes.begin());
            std::memcpy(&value, swapped_bytes.data(), size);
#endif
        } else {
            std::memcpy(&value, bytes.data(), size);
        }
    }
    return value;
}

template <Integral T>
T read_scalar(std::span<const std::byte> data, bool bigEndian);

class StreamExceptionGuard {
public:
    explicit StreamExceptionGuard(std::ifstream &stream)
        : m_stream(stream), m_oldMask(stream.exceptions()) {
        m_stream.exceptions(std::ios::failbit | std::ios::badbit);
    }
    ~StreamExceptionGuard() noexcept {
        try {
            m_stream.exceptions(m_oldMask);
        } catch (...) {
            // ignore exceptions to maintain noexcept
        }
    }
private:
    std::ifstream &m_stream;
    std::ios::iostate m_oldMask;
};

// Lit exactement `size` octets depuis `f` dans `data`.
// Lève std::runtime_error si la lecture est incomplète.
void read_exact(std::ifstream &f, void *data, size_t size);

// Ajoute un entier 16 bits en little-endian dans un vecteur de bytes
void append_le16(std::vector<std::byte> &out, uint16_t value);

// Ajoute un entier 32 bits en little-endian dans un vecteur de bytes
void append_le32(std::vector<std::byte> &out, uint32_t value);

void log_info(const std::filesystem::path &path, const std::string &msg,
              const ExtractorOptions &opt);
void log_warn(const std::filesystem::path &path, const std::string &msg,
              const ExtractorOptions &opt);
void log_error(const std::filesystem::path &path, const std::string &msg,
               const ExtractorOptions &opt);

#ifdef _WIN32
std::wstring make_long_path(const std::wstring &path);
#endif

constexpr size_t kMaxLzsOutput = 10'000'000;

std::vector<std::byte> lzs_decompress(std::span<const std::byte> in, size_t expected_size);
std::vector<int16_t> dpcm16_decompress(std::span<const std::byte> in, int16_t &carry);

} // namespace robot
