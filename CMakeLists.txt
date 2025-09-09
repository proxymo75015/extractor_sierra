cmake_minimum_required(VERSION 3.20)
project(robot_extractor LANGUAGES CXX)

# Configuration standard améliorée
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # Désactive les extensions GNU

# Intégration vcpkg portable
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Fichier toolchain vcpkg")
endif()

# Définitions spécifiques Windows
if(WIN32)
    add_compile_definitions(
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# Gestion des dépendances avec fallback
include(FetchContent)

# nlohmann_json - d'abord tentative avec find_package
find_package(nlohmann_json 3.11.2 CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json non trouvé, utilisation de FetchContent")
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Exécutable principal
add_executable(robot_extractor
    src/robot_extractor.cpp
    src/utilities.cpp
    src/stb_image_write_impl.cpp
)

# Chemins d'inclusion
target_include_directories(robot_extractor PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Liaison des bibliothèques
target_link_libraries(robot_extractor PRIVATE 
    nlohmann_json::nlohmann_json
)

# Options de compilation optimisées
if(MSVC)
    target_compile_options(robot_extractor PRIVATE
        /W4 /WX  # Tous les warnings + traités comme erreurs
        $<$<CONFIG:Release>:/O2>  # Optimisation en Release
        $<$<CONFIG:Debug>:/Od>    # Pas d'optimisation en Debug
    )
    # Sanitizers MSVC (VS 2022 17.9+)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(robot_extractor PRIVATE /fsanitize=address)
        target_link_options(robot_extractor PRIVATE /fsanitize=address)
    endif()
else()
    target_compile_options(robot_extractor PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-O0 -g -fsanitize=address,undefined>
    )
    # Lien des sanitizers en Debug
    target_link_options(robot_extractor PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
endif()

# Configuration des tests
option(BUILD_TESTS "Construire les tests unitaires" OFF)

if(BUILD_TESTS)
    enable_testing()
    # Catch2 via FetchContent
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(Catch2)
    
    # Découverte automatique des sources de test
    file(GLOB TEST_SOURCES tests/*.cpp)
    add_executable(tests ${TEST_SOURCES}
        src/utilities.cpp
        src/robot_extractor.cpp
        src/stb_image_write_impl.cpp)

    target_compile_definitions(tests PRIVATE ROBOT_EXTRACTOR_NO_MAIN ROBOT_EXTRACTOR_TESTING)
    target_include_directories(tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(tests PRIVATE
        Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json
    )
    
    add_test(NAME ExtractionTests COMMAND tests)
endif()

# Cible d'installation complète
install(TARGETS robot_extractor
    RUNTIME DESTINATION bin
    CONFIGURATIONS Release
)

install(FILES
    readme.md
    DESTINATION .
    CONFIGURATIONS Release
)

install(FILES
    CMakePresets.json
    DESTINATION share/${PROJECT_NAME}
    CONFIGURATIONS Release

)




