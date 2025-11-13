cmake_minimum_required(VERSION 3.20)
project(robot_extractor LANGUAGES CXX)

# ═══════════════════════════════════════════════════════════════════════
# Configuration du standard C++
# ═══════════════════════════════════════════════════════════════════════

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # Désactive les extensions GNU pour la portabilité

# ═══════════════════════════════════════════════════════════════════════
# Intégration vcpkg
# ═══════════════════════════════════════════════════════════════════════

# Détecte automatiquement vcpkg si la variable d'environnement est définie
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Fichier toolchain vcpkg")
endif()

# ═══════════════════════════════════════════════════════════════════════
# Définitions spécifiques à la plateforme
# ═══════════════════════════════════════════════════════════════════════

if(WIN32)
    # NOMINMAX : Évite les conflits entre min/max de Windows.h et std::min/max
    # _CRT_SECURE_NO_WARNINGS : Désactive les warnings sur les fonctions POSIX
    add_compile_definitions(
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# ═══════════════════════════════════════════════════════════════════════
# Gestion des dépendances
# ═══════════════════════════════════════════════════════════════════════

include(FetchContent)

# Recherche nlohmann_json via find_package (vcpkg ou système)
# Si introuvable, télécharge depuis GitHub via FetchContent
find_package(nlohmann_json 3.11.2 CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json non trouvé, utilisation de FetchContent")
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ═══════════════════════════════════════════════════════════════════════
# Exécutable principal : robot_extractor
# ═══════════════════════════════════════════════════════════════════════

add_executable(robot_extractor
    src/robot_extractor.cpp
    src/utilities.cpp
)

# Chemins d'inclusion pour les headers
target_include_directories(robot_extractor PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Liaison avec nlohmann_json
target_link_libraries(robot_extractor PRIVATE 
    nlohmann_json::nlohmann_json
)

# ═══════════════════════════════════════════════════════════════════════
# Options de compilation et optimisations
# ═══════════════════════════════════════════════════════════════════════
# ═══════════════════════════════════════════════════════════════════════
# Options de compilation et optimisations
# ═══════════════════════════════════════════════════════════════════════

if(MSVC)
    # Configuration MSVC (Visual Studio)
    target_compile_options(robot_extractor PRIVATE
        /W4                           # Niveau de warnings élevé
        /WX                           # Traiter les warnings comme des erreurs
        $<$<CONFIG:Release>:/O2>      # Optimisation maximale en Release
        $<$<CONFIG:Debug>:/Od>        # Désactive l'optimisation en Debug
    )
    
    # Sanitizers MSVC (disponible depuis Visual Studio 2022 17.9+)
    # Active AddressSanitizer en mode Debug pour détecter les erreurs mémoire
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(robot_extractor PRIVATE /fsanitize=address)
        target_link_options(robot_extractor PRIVATE /fsanitize=address)
    endif()
else()
    # Configuration GCC/Clang
    target_compile_options(robot_extractor PRIVATE
        -Wall                         # Tous les warnings standards
        -Wextra                       # Warnings supplémentaires
        -Wpedantic                    # Respect strict du standard
        -Wconversion                  # Warnings sur conversions implicites
        -Wshadow                      # Warnings sur variables masquées
        $<$<CONFIG:Release>:-O3>      # Optimisation maximale en Release
        $<$<CONFIG:Debug>:-O0 -g -fsanitize=address,undefined>  # Debug + sanitizers
    )
    
    # Liaison des sanitizers en mode Debug
    target_link_options(robot_extractor PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
endif()

# ═══════════════════════════════════════════════════════════════════════
# Configuration des tests unitaires
# ═══════════════════════════════════════════════════════════════════════

option(BUILD_TESTS "Construire les tests unitaires" OFF)

if(BUILD_TESTS)
    enable_testing()
    
    # Télécharge Catch2 (framework de test)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)
    
    # Découvre automatiquement tous les fichiers de test
    file(GLOB TEST_SOURCES tests/*.cpp)
    
    # Crée l'exécutable de test avec les mêmes sources que l'exécutable principal
    add_executable(tests ${TEST_SOURCES}
        src/utilities.cpp
        src/robot_extractor.cpp)

    # Définitions pour le mode test
    # ROBOT_EXTRACTOR_NO_MAIN : Désactive la fonction main() de robot_extractor.cpp
    # ROBOT_EXTRACTOR_TESTING : Active les accesseurs de test (RobotExtractorTester)
    target_compile_definitions(tests PRIVATE 
        ROBOT_EXTRACTOR_NO_MAIN 
        ROBOT_EXTRACTOR_TESTING
    )
    
    target_include_directories(tests PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(tests PRIVATE
        Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json
    )
    
    # Enregistre la suite de tests auprès de CTest
    add_test(NAME ExtractionTests COMMAND tests)
endif()

# ═══════════════════════════════════════════════════════════════════════
# Installation
# ═══════════════════════════════════════════════════════════════════════

# Installe l'exécutable dans bin/ (uniquement en Release)
install(TARGETS robot_extractor
    RUNTIME DESTINATION bin
    CONFIGURATIONS Release
)

# Installe la documentation
install(FILES
    readme.md
    DESTINATION .
    CONFIGURATIONS Release
)

# Installe les presets CMake (pour référence)
install(FILES
    CMakePresets.json
    DESTINATION share/${PROJECT_NAME}
    CONFIGURATIONS Release
)





