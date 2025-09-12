#include "robot_extractor.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstddef>
#include <vector>

using robot::expand_cel;

TEST_CASE("expand_cel recopie les lignes comprimées dans la cible finale") {
  const uint16_t w = 3;
  const uint16_t h = 4;
  const uint8_t scale = 50; // source à moitié de la hauteur

  std::vector<std::byte> source{
      std::byte{1}, std::byte{2}, std::byte{3},
      std::byte{4}, std::byte{5}, std::byte{6}}; // hauteur source = 2 lignes

  std::vector<std::byte> expected{std::byte{1}, std::byte{2}, std::byte{3},
                                  std::byte{1}, std::byte{2}, std::byte{3},
                                  std::byte{4}, std::byte{5}, std::byte{6},
                                  std::byte{4}, std::byte{5}, std::byte{6}};

  std::vector<std::byte> target(static_cast<size_t>(w) * h);
  expand_cel(target, source, w, h, scale);

  REQUIRE(target == expected);
}

TEST_CASE("expand_cel gère les ratios non entiers") {
  const uint16_t w = 2;
  const uint16_t h = 5;
  const uint8_t scale = 60; // hauteur source = 3 lignes

  std::vector<std::byte> source{std::byte{1}, std::byte{2}, std::byte{3},
                                std::byte{4}, std::byte{5}, std::byte{6}};

  std::vector<std::byte> expected{
      std::byte{1}, std::byte{2}, std::byte{1}, std::byte{2}, std::byte{3},
      std::byte{4}, std::byte{3}, std::byte{4}, std::byte{5}, std::byte{6}};

  std::vector<std::byte> target(static_cast<size_t>(w) * h);
  expand_cel(target, source, w, h, scale);

  REQUIRE(target == expected);
}
TEST_CASE("expand_cel réduit les lignes lorsque le scale est supérieur à 100") {
  const uint16_t w = 2;
  const uint16_t h = 2;
  const uint8_t scale = 200; // source deux fois plus haute

  std::vector<std::byte> source{std::byte{1}, std::byte{2}, std::byte{3},
                                std::byte{4}, std::byte{5}, std::byte{6},
                                std::byte{7}, std::byte{8}}; // hauteur source 4

  std::vector<std::byte> expected{std::byte{1}, std::byte{2}, std::byte{5},
                                  std::byte{6}}; // lignes 0 et 2 conservées

  std::vector<std::byte> target(static_cast<size_t>(w) * h);
  expand_cel(target, source, w, h, scale);

  REQUIRE(target == expected);
}

TEST_CASE("expand_cel vérifie les tailles de tampons") {
  const uint16_t w = 2;
  const uint16_t h = 2;
  const uint8_t scale = 50; // hauteur source = 1

  // Taille incorrecte de la source
  std::vector<std::byte> badSource(3);
  std::vector<std::byte> goodTarget(static_cast<size_t>(w) * h);
  REQUIRE_THROWS(expand_cel(goodTarget, badSource, w, h, scale));

  // Taille incorrecte de la cible
  std::vector<std::byte> goodSource(static_cast<size_t>(w) *
                                    ((static_cast<int>(h) * scale) / 100));
  std::vector<std::byte> badTarget(goodTarget.size() - 1);
  REQUIRE_THROWS(expand_cel(badTarget, goodSource, w, h, scale));
}

TEST_CASE("expand_cel rejette un scale nul") {
  const uint16_t w = 1;
  const uint16_t h = 1;
  std::vector<std::byte> target(1);
  std::vector<std::byte> source(1);

  REQUIRE_THROWS(expand_cel(target, source, w, h, 0));
}
TEST_CASE("expand_cel rejette un scale trop grand") {
  const uint16_t w = 1;
  const uint16_t h = 1;
  const uint8_t scale = 201;
  std::vector<std::byte> target(1);
  std::vector<std::byte> source(1);

  REQUIRE_THROWS(expand_cel(target, source, w, h, scale));
}

TEST_CASE("expand_cel rejette des dimensions nulles") {
  const uint8_t scale = 100;
  std::vector<std::byte> target;
  std::vector<std::byte> source;

  REQUIRE_THROWS(expand_cel(target, source, 0, 1, scale));
  REQUIRE_THROWS(expand_cel(target, source, 1, 0, scale));
}

TEST_CASE("expand_cel détecte un facteur de réduction incohérent") {
  const uint16_t w = 2;
  const uint16_t h = 4;
  const uint8_t badScale = 25; // attend 1 ligne mais en fournit 2

  std::vector<std::byte> source(static_cast<size_t>(w) * 2);
  std::vector<std::byte> target(static_cast<size_t>(w) * h);

  REQUIRE_THROWS(expand_cel(target, source, w, h, badScale));
}

TEST_CASE("expand_cel traite de grands cels à l'agrandissement") {
  const size_t maxPixels = robot::RobotExtractor::kMaxCelPixels;
  const uint16_t w = 1024;
  const uint16_t h = static_cast<uint16_t>(maxPixels / w);
  REQUIRE(static_cast<size_t>(w) * h == maxPixels);
  const uint8_t scale = 50;

  const int sourceHeight = static_cast<int>(h) * scale / 100;
  std::vector<std::byte> source(static_cast<size_t>(w) * sourceHeight);
  for (int y = 0; y < sourceHeight; ++y) {
    const auto offset = static_cast<size_t>(y) * w;
    auto row = source.begin() + static_cast<std::ptrdiff_t>(offset);
    std::fill_n(row, static_cast<std::size_t>(w),
                std::byte(y & 0xFF));
  }

  std::vector<std::byte> target(static_cast<size_t>(w) * h);
  expand_cel(target, source, w, h, scale);

  for (int y = 0; y < static_cast<int>(h); ++y) {
    std::byte expected = std::byte((y / 2) & 0xFF);
    const auto offset = static_cast<size_t>(y) * w;
    auto row = target.begin() + static_cast<std::ptrdiff_t>(offset);
    bool ok = std::all_of(row, row + static_cast<std::ptrdiff_t>(w),
                          [expected](std::byte b) { return b == expected; });
    REQUIRE(ok);
  }
}

TEST_CASE("expand_cel traite de grands cels à la réduction") {
  const size_t maxPixels = robot::RobotExtractor::kMaxCelPixels;
  const uint16_t w = 1024;
  const uint16_t h = static_cast<uint16_t>(maxPixels / w);
  REQUIRE(static_cast<size_t>(w) * h == maxPixels);
  const uint8_t scale = 150;

  const int sourceHeight = static_cast<int>(h) * scale / 100;
  std::vector<std::byte> source(static_cast<size_t>(w) * sourceHeight);
  for (int y = 0; y < sourceHeight; ++y) {
    const auto offset = static_cast<size_t>(y) * w;
    auto row = source.begin() + static_cast<std::ptrdiff_t>(offset);
    std::fill_n(row, static_cast<std::size_t>(w),
                std::byte(y & 0xFF));
  }

  std::vector<std::byte> target(static_cast<size_t>(w) * h);
  expand_cel(target, source, w, h, scale);

  int srcY = sourceHeight;
  int remainder = 0;
  for (int destY = static_cast<int>(h) - 1; destY >= 0; --destY) {
    remainder += sourceHeight;
    int step = remainder / static_cast<int>(h);
    remainder %= static_cast<int>(h);
    srcY -= step;
    REQUIRE(srcY >= 0);
    std::byte expected = std::byte(srcY & 0xFF);
    const auto offset = static_cast<size_t>(destY) * w;
    auto row = target.begin() + static_cast<std::ptrdiff_t>(offset);
    bool ok = std::all_of(row, row + static_cast<std::ptrdiff_t>(w),
                          [expected](std::byte b) { return b == expected; });
    REQUIRE(ok);
  }
  REQUIRE(srcY == 0);
}

TEST_CASE("expand_cel réduit correctement un cel de hauteur minimale") {
  const uint16_t w = 2;
  const uint16_t h = 1;
  const uint8_t scale = 200; // source deux fois plus haute que la cible

  std::vector<std::byte> source{std::byte{1}, std::byte{2},
                                std::byte{3}, std::byte{4}};

  std::vector<std::byte> expected{std::byte{1}, std::byte{2}};

  std::vector<std::byte> target(static_cast<size_t>(w) * h);
  expand_cel(target, source, w, h, scale);

  REQUIRE(target == expected);
}
