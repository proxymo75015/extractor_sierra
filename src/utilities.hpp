// Utilitaires généraux pour la lecture/écriture, la journalisation
// et les décodeurs (LZS, DPCM16) utilisés par le format Robot.
//
// Ce fichier fournit :
// - Lecture/écriture d'entiers avec gestion de l'endianness
// - Décodeurs LZS (compression Lempel-Ziv) et DPCM-16 (audio)
// - Fonctions de journalisation (info, warn, error)
// - Gestion des chemins longs sous Windows
// - Export PNG multi-plateforme
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

// Résolutions maximales autorisées (4K UHD par défaut).
constexpr int kMaxXRes = 7680;
constexpr int kMaxYRes = 4320;

// Options de comportement de l'extracteur.
// Permet de contrôler la journalisation, l'endianness forcée,
// et les limites de résolution.
struct ExtractorOptions {
    bool quiet = false;        // Désactive les messages de log
    bool force_be = false;     // Force l'interprétation big-endian
    bool force_le = false;     // Force l'interprétation little-endian
    bool debug_index = false;  // Active le log des incohérences d'index
    int max_x_res = kMaxXRes;  // Résolution max en largeur
    int max_y_res = kMaxYRes;  // Résolution max en hauteur
};

// Concept C++20 : restreint un type template aux types entiers uniquement.
template <typename T>
concept Integral = std::is_integral_v<T>;

namespace detail {

// Implémentation interne de lecture scalaire avec gestion de l'endianness.
// Utilise std::byteswap (C++23) si disponible, sinon reverse_copy.
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

// Lit un scalaire de type T depuis un flux d'entrée.
//
// @param f          Flux d'entrée (ifstream)
// @param bigEndian  Si true, interprète en big-endian, sinon little-endian
// @return           La valeur lue de type T
// @throws runtime_error si la lecture échoue
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

// Variante tampon : lit un scalaire T depuis un bloc mémoire (span).
//
// @param data       Bloc de données à lire
// @param bigEndian  Si true, interprète en big-endian
// @return           La valeur lue de type T
// @throws runtime_error si le tampon est trop petit
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

// Raccourci : lit un scalaire en little-endian depuis un flux.
template <Integral T>
T read_scalar_le(std::ifstream &f) {
    return read_scalar<T>(f, false);
}

// Raccourci : lit un scalaire en little-endian depuis un tampon mémoire.
template <Integral T>
inline T read_scalar_le(std::span<const std::byte> data) {
    return read_scalar<T>(data, false);
}

// Garde RAII pour restaurer le masque d'exceptions d'un std::ifstream.
// 
// Active automatiquement failbit et badbit à la construction,
// puis restaure le masque original à la destruction.
// Utilisé pour garantir que les erreurs de lecture lèvent des exceptions.
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
            // Ignore les exceptions pour maintenir noexcept
        }
    }
private:
    std::ifstream &m_stream;
    std::ios::iostate m_oldMask;
};

// Convertit une taille (size_t) en streamsize de manière sécurisée.
// @throws runtime_error si la taille dépasse la limite de streamsize
std::streamsize checked_streamsize(size_t size);

// Lit exactement `size` octets depuis le flux `f` dans `data`.
// 
// En cas de lecture incomplète, le flux est repositionné à sa position
// initiale et une exception est levée.
//
// @param f     Flux d'entrée
// @param data  Buffer de destination
// @param size  Nombre d'octets à lire
// @throws runtime_error si la lecture est incomplète
void read_exact(std::ifstream &f, void *data, size_t size);

// Détecte l'endianness d'un fichier Robot à partir de son en-tête.
//
// Lit le champ de version à l'offset 6 et détermine si le fichier
// est en big-endian ou little-endian selon l'algorithme de ScummVM.
// Le flux est repositionné à sa position initiale après lecture.
//
// @param f  Flux d'entrée positionné au début du fichier
// @return   true si le fichier est big-endian, false sinon
// @throws runtime_error si la version est invalide
bool detect_endianness(std::ifstream &f);

// Ajoute un entier 16 bits en little-endian à la fin d'un vecteur.
void append_le16(std::vector<std::byte> &out, uint16_t value);

// Ajoute un entier 32 bits en little-endian à la fin d'un vecteur.
void append_le32(std::vector<std::byte> &out, uint32_t value);

// Écrit un entier 16 bits en little-endian dans un buffer brut.
void write_le16(char *dst, uint16_t value);

// Écrit un entier 32 bits en little-endian dans un buffer brut.
void write_le32(char *dst, uint32_t value);

// Fonctions de journalisation (info, avertissement, erreur).
// Toutes sont désactivées si ExtractorOptions::quiet est true.
void log_info(const std::filesystem::path &path, const std::string &msg,
              const ExtractorOptions &opt);
void log_warn(const std::filesystem::path &path, const std::string &msg,
              const ExtractorOptions &opt);
void log_error(const std::filesystem::path &path, const std::string &msg,
               const ExtractorOptions &opt);

#ifdef _WIN32
// Convertit un chemin Windows en format long (\\?\...) pour dépasser
// la limite de 260 caractères.
std::wstring make_long_path(const std::wstring &path);
#endif

// Convertit un chemin en version compatible longs chemins (Windows uniquement).
// Retourne une paire : (chemin compatible, version UTF-8).
std::pair<std::filesystem::path, std::string>
to_long_path(const std::filesystem::path &path);

// Écrit une image PNG avec support des chemins longs.
//
// Utilise stb_image_write pour l'export, avec gestion spéciale
// des chemins longs sous Windows.
//
// @param path    Chemin du fichier PNG à créer
// @param w       Largeur de l'image
// @param h       Hauteur de l'image
// @param comp    Nombre de composantes (3=RGB, 4=RGBA)
// @param data    Buffer de pixels
// @param stride  Nombre d'octets par ligne (0 = calculé automatiquement)
void write_png_cross_platform(const std::filesystem::path &path, int w, int h,
                              int comp, const void *data, int stride);

// Taille maximale de sortie pour le décodeur LZS (10 Mo).
constexpr size_t kMaxLzsOutput = 10'000'000;

// Décompresse un flux LZS (compression Lempel-Ziv Stac).
//
// @param in             Données compressées
// @param expected_size  Taille attendue après décompression
// @param history        Fenêtre glissante initiale (optionnelle)
// @return               Données décompressées
// @throws runtime_error en cas d'erreur de décompression
std::vector<std::byte> lzs_decompress(std::span<const std::byte> in,
                                      size_t expected_size,
                                      std::span<const std::byte> history = {});

// Décompresse un flux DPCM-16 (audio différentiel).
//
// @param in     Données DPCM compressées
// @param carry  Prédicteur DPCM (en entrée/sortie)
// @return       Échantillons PCM 16 bits décodés
std::vector<int16_t> dpcm16_decompress(std::span<const std::byte> in, int16_t &carry);

// Décompresse un flux DPCM-16 sans stocker les échantillons.
// Met uniquement à jour le prédicteur `carry` pour un usage ultérieur.
//
// @param in     Données DPCM compressées
// @param carry  Prédicteur DPCM (en sortie)
void dpcm16_decompress_last(std::span<const std::byte> in, int16_t &carry);

} // namespace robot
