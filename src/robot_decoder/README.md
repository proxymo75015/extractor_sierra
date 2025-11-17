robot_decoder
=================

Minimal standalone extractor for Sierra `.rbt` Robot files.

Build (inside dev container):

```
cd src/robot_decoder
mkdir build && cd build
cmake ..
make -j
```

Run:

```
./robot_decoder <path/to/file.rbt> <out_dir>
```

Output:
- `<out_dir>/metadata.txt` basic metadata
- `<out_dir>/palette.bin` raw palette if present
- `<out_dir>/frames/frame_XXXX_cel_YY.pgm` per-cel PGM files containing palette indices
- `<out_dir>/audio.raw.pcm` (raw PCM 16-bit mono 22050Hz) if audio exists

Notes & Limitations:
- This is a minimal tool to extract frame cels as indexed PGM files and raw decoded DPCM audio.
- Palette format is preserved as `palette.bin` (not parsed to PNG). Use ScummVM or tools to remap PGM with palette.
- LZS decompressor implemented minimally and may not handle all edge cases; intended for quick testing.
