Robot Extractor v1.0.0
Extracts animations and audio from Atari Robot (.rbt) files, compatible with ScummVM's Robot file format (versions 4, 5, and 6).
The palette data in the input file must contain a multiple of 3 bytes (RGB triples).
Prerequisites

C++20 compiler (GCC 11+, Clang 14+, MSVC 19.30+)
Libraries:
nlohmann/json (version 3.10.0 or higher)
stb_image_write (included via vcpkg or as stb_image_write.h with STB_IMAGE_WRITE_IMPLEMENTATION defined)


Optional: vcpkg for dependency management

Installation

Install dependencies:

If using vcpkg:vcpkg install nlohmann-json stb


If not using vcpkg, ensure nlohmann/json.hpp and stb_image_write.h are in your include path.


Compile the project:
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build



Usage
./robot_extractor [--audio] [--quiet] [--force-be | --force-le] [--debug-index] <input.rbt> <output_dir>

Options

--audio: Extract audio tracks as WAV files␊
--quiet: Suppress console output␊
--force-be: Force big-endian byte order. Cannot be used with --force-le
--force-le: Force little-endian byte order. Cannot be used with --force-be
--debug-index: Log index and size inconsistencies when reading packets

Output

PNG files for each cel (frame_XXXXX_N.png)
WAV files for audio tracks (if --audio is specified):
frame_XXXXX_even.wav: Even channel (mono, 16-bit, 11.025 kHz)
frame_XXXXX_odd.wav: Odd channel (mono, 16-bit, 11.025 kHz)


metadata.json containing frame and cel metadata

Format Audio
The exported WAV files are in mono 16-bit 11.025 kHz format. For stereo audio in the original files, the channels are separated:

frame_XXXXX_even.wav: Even channel
frame_XXXXX_odd.wav: Odd channel

Compatibility
This tool is designed to be compatible with ScummVM's Robot file format (versions 4, 5, and 6). It handles LZS decompression, DPCM-16 audio decoding, and palette-based RGBA conversion.
History

Earlier revisions of this project bundled a legacy standalone extractor at `src/old/robot_extractor.old` (version 1.2.8). The file has been removed for clarity, but it can still be accessed through the repository's Git history if needed.
License

BSD 3-Clause License



