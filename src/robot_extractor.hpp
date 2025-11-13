// API principale d'extraction du format Robot (SCI/ScummVM).
// 
// Ce fichier fournit la classe RobotExtractor qui permet de décoder et extraire
// les animations (cels/frames) et l'audio au format Robot utilisé par ScummVM.
// Il inclut également des helpers pour le test unitaire.
//
// Le format Robot stocke des animations compressées avec palette et audio DPCM-16.
// Les versions 4, 5 et 6 sont prises en charge.
#pragma once

#include "utilities.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace robot {

// Taille du tampon de décompression zéro (2 Ko) pour les blocs audio.
inline constexpr size_t kRobotZeroCompressSize = 2048;

// Nombre d'octets dans la "runway" (piste d'approche) audio.
// La runway contient les échantillons initiaux nécessaires au décodeur DPCM.
inline constexpr size_t kRobotRunwayBytes = 8;
constexpr size_t kRobotRunwaySamples = kRobotRunwayBytes / sizeof(int16_t);

// Taille de l'en-tête d'un bloc audio Robot.
inline constexpr size_t kRobotAudioHeaderSize = 8;

// Vérifie si deux plages de mémoire se chevauchent.
// Utilisé pour détecter les cas où source et cible partagent le même buffer.
inline bool rangesOverlap(const std::byte *aBegin, const std::byte *aEnd,
                          const std::byte *bBegin, const std::byte *bEnd) {
  return !(aEnd <= bBegin || bEnd <= aBegin);
}

// Étend verticalement un cel (cellule d'animation) en dupliquant des lignes.
// 
// Cette fonction reproduit l'algorithme d'expansion verticale de ScummVM.
// Elle permet d'adapter la hauteur d'une image compressée à sa résolution finale
// en fonction du facteur d'échelle vertical (scale).
//
// @param target   Buffer de destination (taille >= w * h)
// @param source   Buffer source contenant les lignes compressées
// @param w        Largeur du cel en pixels
// @param h        Hauteur finale du cel en pixels
// @param scale    Facteur d'échelle vertical (pourcentage, ex: 100 = pas d'agrandissement)
//
// @throws runtime_error si scale est zéro, si les buffers sont trop petits,
//                       ou si source et target se chevauchent
inline void expand_cel(std::span<std::byte> target,
                       std::span<const std::byte> source, 
                       uint16_t w, uint16_t h, uint8_t scale) {
  if (scale == 0) {
    throw std::runtime_error("Facteur d'échelle vertical invalide (zéro)");
  }

  // Calcule la hauteur source en fonction du facteur d'échelle.
  // Correspond au calcul expandCel de ScummVM.
  const int sourceHeight = (static_cast<int>(h) * static_cast<int>(scale)) / 100;
  
  // Dans ScummVM, ceci est une assertion (sourceHeight > 0).
  if (sourceHeight <= 0) {
    throw std::runtime_error("Facteur d'échelle vertical invalide (sourceHeight <= 0)");
  }
  
  const size_t wSize = static_cast<size_t>(w);
  const size_t source_h = static_cast<size_t>(sourceHeight);
  const size_t expected_source = wSize * source_h;
  const size_t expected_target = wSize * static_cast<size_t>(h);

  if (source.size() < expected_source) {
    throw std::runtime_error("Taille du tampon source insuffisante");
  }

  if (target.size() < expected_target) {
    throw std::runtime_error("Taille du tampon cible insuffisante");
  }

  // Vérifie que les buffers source et cible ne se chevauchent pas.
  if (rangesOverlap(source.data(), source.data() + expected_source,
                    target.data(), target.data() + expected_target)) {
    throw std::runtime_error("Les tampons source et cible ne doivent pas se chevaucher");
  }

  // Utilise des types int16 pour correspondre exactement à ScummVM.
  const int16_t numerator = static_cast<int16_t>(h);
  const int16_t denominator = static_cast<int16_t>(sourceHeight);
  int remainder = 0;

  const std::byte *sourcePtr = source.data();
  std::byte *targetPtr = target.data();

  // Parcourt les lignes source de bas en haut (sourceHeight - 1 à 0).
  // Pour chaque ligne source, duplique autant de fois que nécessaire
  // pour atteindre la hauteur finale h.
  for (int16_t y = sourceHeight - 1; y >= 0; --y) {
    remainder += numerator;
    int16_t linesToDraw = remainder / denominator;
    remainder %= denominator;

    while (linesToDraw--) {
      std::memcpy(targetPtr, sourcePtr, wSize);
      targetPtr += wSize;
    }

    sourcePtr += wSize;
  }
}

// Classe principale pour l'extraction de fichiers Robot.
//
// Cette classe gère la lecture et l'extraction complète d'un fichier Robot,
// incluant :
// - Le parsing de l'en-tête (version, résolution, palette, audio, etc.)
// - La lecture du primer audio (données d'initialisation DPCM)
// - Le décodage des frames et cels (images compressées LZS)
// - L'extraction de l'audio compressé DPCM-16
// - L'export en PNG et WAV
class RobotExtractor {
public:
  // Constructeur de l'extracteur.
  //
  // @param srcPath       Chemin du fichier Robot source (.rbt)
  // @param dstDir        Répertoire de destination pour les exports
  // @param extractAudio  Si true, extrait aussi l'audio en WAV
  // @param options       Options de comportement (quiet, force endian, etc.)
  RobotExtractor(const std::filesystem::path &srcPath,
                 const std::filesystem::path &dstDir, bool extractAudio,
                 ExtractorOptions options = {});
  
  // Lance l'extraction complète du fichier Robot.
  // Crée les fichiers PNG, WAV et metadata.json dans dstDir.
  void extract();

  // Représente une entrée de palette (couleur RGBA avec métadonnées).
  struct PaletteEntry {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    bool used = false;     // Marqueur d'utilisation SCI
    bool present = false;  // Indique si l'entrée a été lue depuis le fichier
  };

  // Résultat du parsing d'une palette SCI HunkPalette.
  struct ParsedPalette {
    bool valid = false;           // True si la palette a été parsée avec succès
    uint8_t startColor = 0;       // Index de la première couleur
    uint16_t colorCount = 0;      // Nombre de couleurs dans la palette
    bool sharedUsed = false;      // Flag "sharedUsed" SCI
    bool defaultUsed = false;     // Flag "defaultUsed" SCI
    uint32_t version = 0;         // Version de la palette
    std::array<PaletteEntry, 256> entries{};  // Table des 256 entrées
    std::vector<std::byte> remapData;         // Données de remapping (si présentes)
  };

private:
#ifdef ROBOT_EXTRACTOR_TESTING
  friend struct RobotExtractorTester;
#endif

  // Constantes du format Robot
  static constexpr uint16_t kRobotSig = 0x16;              // Signature du format
  static constexpr uint16_t kMaxFrames = 10000;            // Limite conseillée de frames
  static constexpr uint16_t kMaxAudioBlockSize = 65535;    // Taille max d'un bloc audio
  static constexpr size_t kMaxCuePoints = 256;             // Nombre max de points de repère
  static constexpr size_t kCelHeaderSize = 22;             // Taille de l'en-tête d'un cel
  static constexpr uint32_t kChannelSampleRate = 11025;    // Fréquence par canal (11.025 kHz)
  static constexpr uint32_t kSampleRate = 22050;           // Fréquence stéréo totale (22.05 kHz)
  static constexpr const char *kPaletteFallbackFilename = "palette.raw";  // Nom du fichier de secours

  // Méthodes privées d'extraction et de traitement
  void readHeader();                              // Lit l'en-tête du fichier Robot
  void parseHeaderFields(bool bigEndian);         // Parse les champs étendus de l'en-tête
  void readPrimer();                              // Lit le primer audio (données d'init DPCM)
  void readPalette();                             // Lit la palette de couleurs
  void ensurePrimerProcessed();                   // S'assure que le primer est traité
  void processPrimerChannel(std::vector<std::byte> &primer, bool isEven);  // Traite un canal du primer
  void process_audio_block(std::span<const std::byte> block, int32_t pos,
                           bool zeroCompressed = false);  // Traite un bloc audio
  void setAudioStartOffset(int64_t offset);       // Définit l'offset de début audio
  void readSizesAndCues(bool allowShortFile = false);  // Lit les tailles de frames et points de repère
  bool exportFrame(int frameNo, nlohmann::json &frameJson);  // Exporte une frame en PNG
  void writeWav(const std::vector<int16_t> &samples, uint32_t sampleRate,
                size_t blockIndex, bool isEvenChannel,
                uint16_t numChannels = 1,
                bool appendChannelSuffix = true);  // Écrit un fichier WAV
  void appendChannelSamples(
      bool isEven, int64_t halfPos, const std::vector<int16_t> &samples,
      size_t zeroCompressedPrefixSamples = 0,
      std::optional<int16_t> finalPredictor = std::nullopt);  // Ajoute des échantillons à un canal
  size_t celPixelLimit() const;    // Retourne la limite de pixels par cel
  size_t rgbaBufferLimit() const;  // Retourne la limite du buffer RGBA

  struct ChannelAudio;  // Déclaration anticipée
  
  // Plan d'ajout d'échantillons audio à un canal
  struct AppendPlan {
    size_t skipSamples = 0;          // Échantillons à ignorer au début
    size_t startSample = 0;          // Index de début dans le canal
    size_t availableSamples = 0;     // Échantillons disponibles
    size_t leadingOverlap = 0;       // Chevauchement au début
    size_t trimmedStart = 0;         // Début après trim
    size_t requiredSize = 0;         // Taille requise du buffer
    size_t zeroCompressedPrefix = 0; // Préfixe de compression zéro
    size_t inputOffset = 0;          // Offset dans l'entrée
    bool negativeAdjusted = false;   // Position négative ajustée
    bool negativeIgnored = false;    // Position négative ignorée
    bool posIsEven = true;           // Position sur canal pair
  };

  // Statut du plan d'ajout d'échantillons
  enum class AppendPlanStatus { 
    Skip,            // Bloc à ignorer
    Ok,              // Ajout OK
    Conflict,        // Conflit détecté
    ParityMismatch   // Erreur de parité (pair/impair)
  };

  // Méthodes de planification et finalisation de l'ajout audio
  AppendPlanStatus planChannelAppend(const ChannelAudio &channel, bool isEven,
                                     int64_t halfPos, int64_t originalHalfPos,
                                     const std::vector<int16_t> &samples,
                                     AppendPlan &plan,
                                     size_t inputOffset = 0) const;
  AppendPlanStatus prepareChannelAppend(ChannelAudio &channel, bool isEven,
                                        int64_t halfPos,
                                        const std::vector<int16_t> &samples,
                                        AppendPlan &plan,
                                        size_t zeroCompressedPrefixSamples = 0);
  void finalizeChannelAppend(ChannelAudio &channel, bool isEven,
                             int64_t halfPos,
                             const std::vector<int16_t> &samples,
                             const AppendPlan &plan,
                             AppendPlanStatus status);
  void finalizeAudio();                                      // Finalise l'extraction audio
  std::vector<int16_t> buildChannelStream(bool isEven) const;  // Construit le flux d'un canal

  // Parse une palette SCI HunkPalette depuis des données brutes
  static ParsedPalette parseHunkPalette(std::span<const std::byte> raw);

  // Membres de données : configuration et état
  std::filesystem::path m_srcPath;      // Chemin du fichier source
  std::filesystem::path m_dstDir;       // Répertoire de destination
  std::ifstream m_fp;                   // Flux du fichier
  std::uintmax_t m_fileSize;            // Taille du fichier
  bool m_bigEndian = false;             // Ordre des octets (big-endian)
  bool m_extractAudio;                  // Extraction audio activée
  ExtractorOptions m_options;           // Options de l'extracteur
  
  // Champs de l'en-tête Robot
  uint16_t m_version;                   // Version du format Robot
  uint16_t m_audioBlkSize;              // Taille des blocs audio
  int16_t m_primerZeroCompressFlag;     // Flag de compression zéro du primer
  uint16_t m_numFrames;                 // Nombre de frames
  uint16_t m_paletteSize;               // Taille de la palette
  uint16_t m_primerReservedSize;        // Taille réservée pour le primer
  int16_t m_xRes;                       // Résolution horizontale
  int16_t m_yRes;                       // Résolution verticale
  bool m_hasPalette;                    // Présence d'une palette
  bool m_hasAudio;                      // Présence d'audio
  int16_t m_frameRate;                  // Fréquence d'images (FPS)
  bool m_isHiRes;                       // Mode haute résolution
  int16_t m_maxSkippablePackets;        // Paquets ignorables max
  int16_t m_maxCelsPerFrame;            // Nombre max de cels par frame

  std::array<uint32_t, 4> m_fixedCelSizes{};        // Tailles fixes des cels
  std::array<uint32_t, 2> m_reservedHeaderSpace{}; // Espace réservé dans l'en-tête
  std::vector<uint32_t> m_frameSizes;               // Tailles de chaque frame
  std::vector<uint32_t> m_packetSizes;              // Tailles de chaque paquet
  std::array<int32_t, kMaxCuePoints> m_cueTimes;    // Temps des points de repère
  std::array<uint16_t, kMaxCuePoints> m_cueValues;  // Valeurs des points de repère
  std::vector<std::byte> m_palette;                 // Données de la palette

  // Gestion du flux et des positions dans le fichier
  std::streamoff m_fileOffset = 0;       // Offset courant dans le fichier
  std::streamoff m_postHeaderPos = 0;    // Position après l'en-tête
  std::streamoff m_postPrimerPos = 0;    // Position après le primer
  std::streamsize m_evenPrimerSize = 0;  // Taille du primer canal pair
  std::streamsize m_oddPrimerSize = 0;   // Taille du primer canal impair
  int32_t m_totalPrimerSize = 0;         // Taille totale du primer
  std::streamoff m_primerPosition = 0;   // Position du primer

  // Données du primer audio (initialisation DPCM)
  std::vector<std::byte> m_evenPrimer;  // Primer du canal pair
  std::vector<std::byte> m_oddPrimer;   // Primer du canal impair
  bool m_primerInvalid = false;         // Primer invalide
  bool m_primerProcessed = false;       // Primer traité

  // Buffers de travail pour le décodage
  std::vector<std::byte> m_frameBuffer;  // Buffer de frame
  std::vector<std::byte> m_celBuffer;    // Buffer de cel (image compressée)
  std::vector<uint32_t> m_rgbaBuffer;    // Buffer RGBA (image décodée, 0xAARRGGBB)

  // État de la palette
  bool m_paletteParseFailed = false;      // Échec du parsing de la palette
  bool m_paletteFallbackDumped = false;   // Palette de secours exportée

  // État audio d'un canal (pair ou impair)
  struct ChannelAudio {
    std::vector<int16_t> samples;        // Échantillons PCM décodés
    std::vector<uint8_t> occupied;       // Bits d'occupation
    std::vector<uint8_t> zeroCompressed; // Bits de compression zéro
    int64_t startHalfPos = 0;            // Position de début (demi-fréquence)
    bool startHalfPosInitialized = false;  // Position de début initialisée
    bool seenNonPrimerBlock = false;     // Bloc non-primer rencontré
    bool hasAcceptedPos = false;         // Position acceptée
    int32_t lastAcceptedPos = 0;         // Dernière position acceptée
    int16_t predictor = 0;               // Prédicteur DPCM
    bool predictorInitialized = false;   // Prédicteur initialisé
  };

  ChannelAudio m_evenChannelAudio;  // Audio du canal pair
  ChannelAudio m_oddChannelAudio;   // Audio du canal impair
  int64_t m_audioStartOffset = 0;   // Offset de début de l'audio
};

// Structure d'accès aux membres privés pour les tests unitaires.
// Permet aux tests d'inspecter et de modifier l'état interne de RobotExtractor
// sans exposer ces détails dans l'API publique.
#ifdef ROBOT_EXTRACTOR_TESTING
struct RobotExtractorTester {
  // Accesseurs pour les vecteurs de tailles
  static std::vector<uint32_t> &frameSizes(RobotExtractor &r) { return r.m_frameSizes; }
  static std::vector<uint32_t> &packetSizes(RobotExtractor &r) { return r.m_packetSizes; }
  
  // Accesseurs pour le flux et les positions
  static std::ifstream &file(RobotExtractor &r) { return r.m_fp; }
  static std::streamoff &primerPosition(RobotExtractor &r) { return r.m_primerPosition; }
  static std::streamoff &postHeaderPos(RobotExtractor &r) { return r.m_postHeaderPos; }
  static std::streamoff &postPrimerPos(RobotExtractor &r) { return r.m_postPrimerPos; }
  
  // Accesseurs pour les données du primer
  static std::vector<std::byte> &evenPrimer(RobotExtractor &r) { return r.m_evenPrimer; }
  static std::vector<std::byte> &oddPrimer(RobotExtractor &r) { return r.m_oddPrimer; }
  static std::streamsize &evenPrimerSize(RobotExtractor &r) { return r.m_evenPrimerSize; }
  static std::streamsize &oddPrimerSize(RobotExtractor &r) { return r.m_oddPrimerSize; }
  
  // Accesseurs pour les propriétés de configuration
  static bool &hasPalette(RobotExtractor &r) { return r.m_hasPalette; }
  static bool &bigEndian(RobotExtractor &r) { return r.m_bigEndian; }
  static int16_t &maxCelsPerFrame(RobotExtractor &r) { return r.m_maxCelsPerFrame; }
  static uint16_t &numFrames(RobotExtractor &r) { return r.m_numFrames; }
  static std::vector<std::byte> &palette(RobotExtractor &r) { return r.m_palette; }
  static int16_t &xRes(RobotExtractor &r) { return r.m_xRes; }
  static int16_t &yRes(RobotExtractor &r) { return r.m_yRes; }
  
  // Accesseurs pour les buffers de travail
  static std::vector<uint32_t> &rgbaBuffer(RobotExtractor &r) { return r.m_rgbaBuffer; }
  static std::array<uint32_t, 4> &fixedCelSizes(RobotExtractor &r) { return r.m_fixedCelSizes; }
  static std::vector<std::byte> &celBuffer(RobotExtractor &r) { return r.m_celBuffer; }
  
  // Accesseurs pour les limites
  static size_t celPixelLimit(const RobotExtractor &r) { return r.celPixelLimit(); }
  static size_t rgbaBufferLimit(const RobotExtractor &r) { return r.rgbaBufferLimit(); }
  static constexpr uint16_t maxAudioBlockSize() { return RobotExtractor::kMaxAudioBlockSize; }
  static constexpr uint16_t maxFrames() { return RobotExtractor::kMaxFrames; }
  
  // Méthodes de test pour forcer l'exécution étape par étape
  static void readHeader(RobotExtractor &r) { r.readHeader(); }
  static void readPrimer(RobotExtractor &r) { r.readPrimer(); }
  static void readPalette(RobotExtractor &r) { r.readPalette(); }
  static void readSizesAndCues(RobotExtractor &r) { r.readSizesAndCues(true); }
  static bool exportFrame(RobotExtractor &r, int frameNo, nlohmann::json &frameJson) {
    return r.exportFrame(frameNo, frameJson);
  }
  static void finalizeAudio(RobotExtractor &r) { r.finalizeAudio(); }
  static std::vector<int16_t> buildChannelStream(RobotExtractor &r, bool isEven) {
    return r.buildChannelStream(isEven);
  }
  static RobotExtractor::ParsedPalette parsePalette(const RobotExtractor &r) {
    return RobotExtractor::parseHunkPalette(r.m_palette);
  }
  static void processAudioBlock(RobotExtractor &r, std::span<const std::byte> block, int32_t pos) {
    r.process_audio_block(block, pos);
  }
  static void setAudioStartOffset(RobotExtractor &r, int64_t offset) {
    r.setAudioStartOffset(offset);
  }
  static int64_t audioStartOffset(const RobotExtractor &r) { return r.m_audioStartOffset; }
  static void writeWav(RobotExtractor &r, const std::vector<int16_t> &samples,
                       uint32_t sampleRate, size_t blockIndex, bool isEvenChannel,
                       uint16_t numChannels = 1, bool appendChannelSuffix = true) {
    r.writeWav(samples, sampleRate, blockIndex, isEvenChannel, numChannels, appendChannelSuffix);
  }
};
#endif

} // namespace robot
