// Programme de décodage des headers audio dans un fichier Robot (RBT)
// Basé sur la spécification ScummVM - Programme indépendant
// Affiche les headers audio décodés pour chaque frame

#include <cstdio>
#include <cstdint>
#include <cstring>

// Lecture little-endian 16-bit
static uint16_t readUint16LE(FILE* f) {
    uint8_t buf[2];
    fread(buf, 1, 2, f);
    return buf[0] | (buf[1] << 8);
}

// Lecture little-endian 32-bit
static uint32_t readUint32LE(FILE* f) {
    uint8_t buf[4];
    fread(buf, 1, 4, f);
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

// Lecture int32 signé little-endian
static int32_t readSint32LE(FILE* f) {
    return (int32_t)readUint32LE(f);
}

// Lecture SCI1.1 16-bit
static uint16_t readSCI11_16(FILE* f) {
    uint8_t buf[2];
    fread(buf, 1, 2, f);
    return buf[0] | (buf[1] << 8);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <robot.rbt>\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file %s\n", argv[1]);
        return 1;
    }

    printf("=== Robot Audio Header Decoder ===\n");
    printf("File: %s\n\n", argv[1]);

    // Lire et vérifier le header Robot
    uint8_t header[60];
    if (fread(header, 1, 60, f) != 60) {
        fprintf(stderr, "Error: Cannot read header\n");
        fclose(f);
        return 1;
    }

    // Vérifier signature 0x16 + 'SOL\0'
    if (header[0] != 0x16 || header[2] != 'S' || header[3] != 'O' || header[4] != 'L' || header[5] != 0x00) {
        fprintf(stderr, "Error: Not a valid Robot file\n");
        fclose(f);
        return 1;
    }

    // Parser le header
    fseek(f, 6, SEEK_SET);
    uint16_t version = readUint16LE(f);
    uint16_t audioBlockSize = readUint16LE(f);
    fseek(f, 2, SEEK_CUR); // primerCompressed
    fseek(f, 2, SEEK_CUR); // unused
    uint16_t totalFrames = readUint16LE(f);
    uint16_t paletteSize = readUint16LE(f);
    uint16_t primerReservedSpace = readUint16LE(f);
    fseek(f, 4, SEEK_CUR); // resolution
    uint8_t hasPalette = fgetc(f);
    uint8_t hasAudio = fgetc(f);

    printf("Version: %u\n", version);
    printf("Total frames: %u\n", totalFrames);
    printf("Audio block size: %u bytes\n", audioBlockSize);
    printf("Has audio: %s\n\n", hasAudio ? "yes" : "no");

    if (!hasAudio) {
        printf("No audio in this file.\n");
        fclose(f);
        return 0;
    }

    // Lire l'en-tête du primer audio
    fseek(f, 60, SEEK_SET);
    
    if (primerReservedSpace > 0) {
        long primerStart = ftell(f);
        
        printf("=== AUDIO PRIMER HEADER ===\n");
        int32_t totalPrimerSize = readSint32LE(f);
        int16_t compressionFormat = (int16_t)readUint16LE(f);
        int32_t evenPrimerSize = readSint32LE(f);
        int32_t oddPrimerSize = readSint32LE(f);
        
        printf("Offset 0-3   | int32 | Taille totale: %d\n", totalPrimerSize);
        printf("Offset 4-5   | int16 | Format compression: %d\n", compressionFormat);
        printf("Offset 6-9   | int32 | Taille even: %d\n", evenPrimerSize);
        printf("Offset 10-13 | int32 | Taille odd: %d\n\n", oddPrimerSize);
        
        fseek(f, primerStart + primerReservedSpace, SEEK_SET);
    } else {
        fseek(f, primerReservedSpace, SEEK_CUR);
    }

    // Sauter la palette
    fseek(f, paletteSize, SEEK_CUR);

    // Lire les tables
    uint32_t videoSizes[totalFrames];
    uint32_t packetSizes[totalFrames];

    for (int i = 0; i < totalFrames; i++) {
        videoSizes[i] = readSCI11_16(f);
    }
    for (int i = 0; i < totalFrames; i++) {
        packetSizes[i] = readSCI11_16(f);
    }

    // Lire et sauter les cues (256 × 32-bit + 256 × 16-bit)
    for (int i = 0; i < 256; i++) {
        readUint32LE(f);
    }
    for (int i = 0; i < 256; i++) {
        readUint16LE(f);
    }

    // Aligner sur 2048
    long currentPos = ftell(f);
    long alignedPos = ((currentPos + 2047) / 2048) * 2048;
    fseek(f, alignedPos, SEEK_SET);

    long firstRecordPos = ftell(f);

    // Décoder chaque header audio
    for (int i = 0; i < totalFrames; i++) {
        // Calculer la position du record
        long recordPos = firstRecordPos;
        for (int j = 0; j < i; j++) {
            recordPos += packetSizes[j];
        }

        // Position du header audio = après la vidéo
        long audioHeaderPos = recordPos + videoSizes[i];
        
        // Vérifier qu'on ne dépasse pas la fin du fichier
        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        if (audioHeaderPos + 8 > fileSize) {
            break;
        }

        fseek(f, audioHeaderPos, SEEK_SET);

        // Lire le header audio (8 bytes)
        int32_t audioAbsolutePosition = readSint32LE(f);
        int32_t audioSize = readSint32LE(f);

        // Vérifier validité
        if (audioAbsolutePosition < 0 || audioSize <= 0 || audioSize > 100000) {
            continue;
        }

        // Afficher le header décodé pour cette frame
        printf("=== FRAME %d AUDIO HEADER (8 bytes) ===\n", i);
        printf("Offset 0-3 | int32 | Position absolue: %d\n", audioAbsolutePosition);
        printf("Offset 4-7 | int32 | Taille du bloc: %d\n\n", audioSize);
    }

    printf("\n");
    fclose(f);
    return 0;
}
