#include "robot_extractor.hpp"
#include <catch2/catch_test_macros.hpp>
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
                                    ((h * scale) / 100));
  std::vector<std::byte> badTarget(goodTarget.size() - 1);
  REQUIRE_THROWS(expand_cel(badTarget, goodSource, w, h, scale));
}

TEST_CASE("expand_cel rejette un scale invalide") {
  const uint16_t w = 1;
  const uint16_t h = 1;
  std::vector<std::byte> target(1);
  std::vector<std::byte> source(1);

  REQUIRE_THROWS(expand_cel(target, source, w, h, 0));
  REQUIRE_THROWS(expand_cel(target, source, w, h, 150));
}

TEST_CASE("expand_cel détecte un facteur de réduction incohérent") {
  const uint16_t w = 2;
  const uint16_t h = 4;
  const uint8_t badScale = 40; // attend 1 ligne mais en fournit 2

  std::vector<std::byte> source(static_cast<size_t>(w) * 2);
  std::vector<std::byte> target(static_cast<size_t>(w) * h);

  REQUIRE_THROWS(expand_cel(target, source, w, h, badScale));
}
