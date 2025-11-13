// Utilitaires généraux pour la lecture/écriture, la journalisation
// et les décodeurs (LZS, DPCM16) utilisés par le format Robot.
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
#include <limits>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace robot {

constexpr int kMaxXRes = 7680;
constexpr int kMaxYRes = 4320;

// Options de comportement de l'extracteur (tests/journalisation).
struct ExtractorOptions {
    bool quiet = false;
    bool force_be = false;
    bool force_le = false;
    bool debug_index = false;
    int max_x_res = kMaxXRes;
    int max_y_res = kMaxYRes;
};

// Restreint certaines fonctions aux types entiers uniquement.
template <typename T>
concept Integral = std::is_integral_v<T>;

namespace detail {

template <Integral T>
T read_scalar_impl(const uint8_t *bytes, bool bigEndian) {
    T value;
    if constexpr (sizeof(T) == 1) {
        value = bytes[0];
    } else {
        if (bigEndian != (std::endian::native == std::endian::big)) {
#if defined(__cpp_lib_byteswap) || (__cplusplus >= 202302L)
            std::memcpy(&value, bytes, sizeof(T));
            value = std::byteswap(value);
#else
            std::array<uint8_t, sizeof(T)> swapped_bytes;
            std::reverse_copy(bytes, bytes + sizeof(T), swapped_bytes.begin());
            std::memcpy(&value, swapped_bytes.data(), sizeof(T));
#endif
        } else {
            std::memcpy(&value, bytes, sizeof(T));
        }
    }
    return value;
}

} // namespace detail

// Lit un scalaire de type T depuis un flux (endian configurable).
template <Integral T>
T read_scalar(std::ifstream &f, bool bigEndian) {
    constexpr size_t size = sizeof(T);
    std::array<uint8_t, size> bytes;
    f.read(reinterpret_cast<char *>(bytes.data()), size);
    if (f.gcount() != static_cast<std::streamsize>(size)) {
        throw std::runtime_error("Échec de la lecture de " + std::to_string(size) + " octets");
    }
    return detail::read_scalar_impl<T>(bytes.data(), bigEndian);
}

// Variante tampon: lit un scalaire T depuis un bloc mémoire.
template <Integral T>
inline T read_scalar(std::span<const std::byte> data, bool bigEndian) {
    constexpr size_t size = sizeof(T);
    if (data.size() < size) {
        throw std::runtime_error("Échec de la lecture de " +
                                 std::to_string(size) +
                                 " octets");
    }
    return detail::read_scalar_impl<T>(
        reinterpret_cast<const uint8_t *>(data.data()), bigEndian);
}

// Raccourci: lit un scalaire en little-endian depuis un flux.
template <Integral T>
T read_scalar_le(std::ifstream &f) {
    return read_scalar<T>(f, false);
}

// Raccourci: lit un scalaire en little-endian depuis un tampon.
template <Integral T>
inline T read_scalar_le(std::span<const std::byte> data) {
    return read_scalar<T>(data, false);
}

// Restaure le masque d'exceptions d'un std::ifstream à la destruction.
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

// Vérifie que `size` tient dans un std::streamsize et retourne la conversion.
std::streamsize checked_streamsize(size_t size);

// Lit exactement `size` octets depuis `f` dans `data`.
// Lève std::runtime_error si la lecture est incomplète.
void read_exact(std::ifstream &f, void *data, size_t size);

// Détecte l'endianness à partir de la signature en tête de fichier.
// Retourne true si la signature indique un format big-endian.
// Le flux est repositionné à sa position initiale après lecture.
bool detect_endianness(std::ifstream &f);

// Ajoute un entier 16 bits en little-endian dans un vecteur de bytes.
void append_le16(std::vector<std::byte> &out, uint16_t value);

// Ajoute un entier 32 bits en little-endian dans un vecteur de bytes.
void append_le32(std::vector<std::byte> &out, uint32_t value);

// Écrit un entier 16 bits en little-endian dans un tampon fixé.
void write_le16(char *dst, uint16_t value);

// Écrit un entier 32 bits en little-endian dans un tampon fixé.
void write_le32(char *dst, uint32_t value);

// Journalisation (désactivée si ExtractorOptions::quiet est vrai).
void log_info(const std::filesystem::path &path, const std::string &msg,
              const ExtractorOptions &opt);
void log_warn(const std::filesystem::path &path, const std::string &msg,
              const ExtractorOptions &opt);
void log_error(const std::filesystem::path &path, const std::string &msg,
               const ExtractorOptions &opt);

#ifdef _WIN32
std::wstring make_long_path(const std::wstring &path);
#endif

// Renvoie un chemin compatible longs chemins (Windows) et sa version UTF-8.
std::pair<std::filesystem::path, std::string>
to_long_path(const std::filesystem::path &path);

// Écrit une image PNG (via stb_image_write) avec prise en charge des chemins longs.
void write_png_cross_platform(const std::filesystem::path &path, int w, int h,
                              int comp, const void *data, int stride);

constexpr size_t kMaxLzsOutput = 10'000'000;

// Décompresse un flux LZS dans un tampon de taille attendue.
// 'history' fournit une fenêtre glissante initiale optionnelle.
std::vector<std::byte> lzs_decompress(std::span<const std::byte> in,
                                      size_t expected_size,
                                      std::span<const std::byte> history = {});
std::vector<int16_t> dpcm16_decompress(std::span<const std::byte> in, int16_t &carry);
// Décompresse sans stocker les échantillons, ne met à jour que `carry`.
void dpcm16_decompress_last(std::span<const std::byte> in, int16_t &carry);

} // namespace robot
