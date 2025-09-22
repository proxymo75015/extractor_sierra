#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace test_palette {

constexpr size_t kHunkPaletteHeaderSize = 13;
constexpr size_t kNumPaletteEntriesOffset = 10;
constexpr size_t kEntryHeaderSize = 22;
constexpr size_t kEntryStartColorOffset = 10;
constexpr size_t kEntryNumColorsOffset = 14;
constexpr size_t kEntryUsedOffset = 16;
constexpr size_t kEntrySharedUsedOffset = 17;
constexpr size_t kEntryVersionOffset = 18;

struct Color {
  bool used = true;
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

inline void write_u16(std::vector<std::byte> &out, size_t offset, uint16_t value,
                      bool bigEndian) {
  if (offset + 1 >= out.size()) {
    return;
  }
  if (bigEndian) {
    out[offset] = std::byte{static_cast<uint8_t>(value >> 8)};
    out[offset + 1] = std::byte{static_cast<uint8_t>(value & 0xFF)};
  } else {
    out[offset] = std::byte{static_cast<uint8_t>(value & 0xFF)};
    out[offset + 1] = std::byte{static_cast<uint8_t>(value >> 8)};
  }
}

inline void write_u32(std::vector<std::byte> &out, size_t offset, uint32_t value,
                      bool bigEndian) {
  if (offset + 3 >= out.size()) {
    return;
  }
  if (bigEndian) {
    out[offset + 0] = std::byte{static_cast<uint8_t>((value >> 24) & 0xFF)};
    out[offset + 1] = std::byte{static_cast<uint8_t>((value >> 16) & 0xFF)};
    out[offset + 2] = std::byte{static_cast<uint8_t>((value >> 8) & 0xFF)};
    out[offset + 3] = std::byte{static_cast<uint8_t>(value & 0xFF)};
  } else {
    out[offset + 0] = std::byte{static_cast<uint8_t>(value & 0xFF)};
    out[offset + 1] = std::byte{static_cast<uint8_t>((value >> 8) & 0xFF)};
    out[offset + 2] = std::byte{static_cast<uint8_t>((value >> 16) & 0xFF)};
    out[offset + 3] = std::byte{static_cast<uint8_t>((value >> 24) & 0xFF)};
  }
}

inline std::vector<std::byte>
build_hunk_palette(const std::vector<Color> &colors, uint8_t startColor = 0,
                   bool sharedUsed = false, bool defaultUsed = true,
                   std::vector<std::byte> remapData = {},
                   bool bigEndian = false) {
  const uint8_t numPalettes = colors.empty() ? 0 : 1;
  size_t totalSize = kHunkPaletteHeaderSize + static_cast<size_t>(2 * numPalettes);
  const size_t perColorBytes = 3 + (sharedUsed ? 0 : 1);
  if (numPalettes) {
    totalSize += kEntryHeaderSize + colors.size() * perColorBytes;
  }
  totalSize += remapData.size();

  std::vector<std::byte> raw(totalSize, std::byte{0});
  raw[kNumPaletteEntriesOffset] = std::byte{numPalettes};
  if (!numPalettes) {
    if (!remapData.empty()) {
      std::copy(remapData.begin(), remapData.end(),
                raw.begin() + kHunkPaletteHeaderSize);
    }
    return raw;
  }

  const uint16_t paletteOffset = static_cast<uint16_t>(kHunkPaletteHeaderSize +
                                                       2 * numPalettes);
  write_u16(raw, kHunkPaletteHeaderSize, paletteOffset, bigEndian);

  const size_t entryPos = paletteOffset;
  raw[entryPos + kEntryStartColorOffset] = std::byte{startColor};
  write_u16(raw, entryPos + kEntryNumColorsOffset,
            static_cast<uint16_t>(colors.size()), bigEndian);
  raw[entryPos + kEntryUsedOffset] =
      static_cast<std::byte>(defaultUsed ? 1 : 0);
  raw[entryPos + kEntrySharedUsedOffset] =
      static_cast<std::byte>(sharedUsed ? 1 : 0);
  write_u32(raw, entryPos + kEntryVersionOffset, 1, bigEndian);

  size_t cursor = entryPos + kEntryHeaderSize;
  for (const auto &color : colors) {
    if (!sharedUsed) {
      raw[cursor++] = static_cast<std::byte>(color.used ? 1 : 0);
    }
    raw[cursor++] = std::byte{color.r};
    raw[cursor++] = std::byte{color.g};
    raw[cursor++] = std::byte{color.b};
  }

  if (!remapData.empty() && cursor < raw.size()) {
    std::copy(remapData.begin(), remapData.end(), raw.begin() + cursor);
  }

  return raw;
}

inline std::vector<std::byte> build_flat_palette(uint8_t r, uint8_t g, uint8_t b,
                                                 bool used = true,
                                                 size_t count = 256) {
  std::vector<Color> colors(count);
  for (auto &color : colors) {
    color.used = used;
    color.r = r;
    color.g = g;
    color.b = b;
  }
  return build_hunk_palette(colors);
}

} // namespace test_palette
