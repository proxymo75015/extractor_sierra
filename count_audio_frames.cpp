// Programme de comptage des frames audio dans un fichier Robot (RBT)
// Basé sur la spécification ScummVM et la documentation de référence
// Ne dépend pas du code du projet - programme indépendant

#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>

// Lecture little-endian 16-bit
static uint16_t readUint16LE(FILE* f) {
    uint8_t buf[2];
    fread(buf, 1, 2, f);
    return buf[0] | (buf[1] << 8);
}

// Lecture big-endian 16-bit
static uint16_t readUint16BE(FILE* f) {
    uint8_t buf[2];
    fread(buf, 1, 2, f);
    return (buf[0] << 8) | buf[1];
}

// Lecture little-endian 32-bit
static uint32_t readUint32LE(FILE* f) {
    uint8_t buf[4];
    fread(buf, 1, 4, f);
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

// Lecture big-endian 32-bit
static uint32_t readUint32BE(FILE* f) {
    uint8_t buf[4];
    fread(buf, 1, 4, f);
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

// Lecture int32 signé
static int32_t readSint32(FILE* f, bool bigEndian) {
    return bigEndian ? (int32_t)readUint32BE(f) : (int32_t)readUint32LE(f);
}

// Lecture SCI1.1 16-bit (format spécial Robot)
static uint16_t readSCI11_16(FILE* f) {
    uint8_t buf[2];
    fread(buf, 1, 2, f);
    return buf[0] | (buf[1] << 8);
}

// Lecture SCI1.1 32-bit (format spécial Robot)
static uint32_t readSCI11_32(FILE* f) {
    uint8_t buf[4];
    fread(buf, 1, 4, f);
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
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

    printf("=== Robot RBT Audio Frame Counter ===\n");
    printf("File: %s\n\n", argv[1]);

    // Lire les premiers bytes pour détecter le format
    uint8_t header[60];
    if (fread(header, 1, 60, f) != 60) {
        fprintf(stderr, "Error: Cannot read header\n");
        fclose(f);
        return 1;
    }
    
    // Détecter le format Robot
    // Signature: 0x16 [unused byte] 'SOL\0'
    if (header[0] != 0x16 || header[2] != 'S' || header[3] != 'O' || header[4] != 'L' || header[5] != 0x00) {
        fprintf(stderr, "Error: Not a valid Robot file\n");
        fprintf(stderr, "Expected: 16 XX 53 4F 4C 00 (0x16 + 'SOL\\0')\n");
        fprintf(stderr, "Got:      ");
        for (int i = 0; i < 6; i++) {
            fprintf(stderr, "%02X ", header[i]);
        }
        fprintf(stderr, "\n");
        fclose(f);
        return 1;
    }
    
    printf("Format: Robot video (signature 0x16 + 'SOL\\0' found)\n");

    // Robot header parsing (offset 6+)
    fseek(f, 6, SEEK_SET);
    
    // Offset 6-7: version
    uint16_t version = readUint16LE(f);  // Robot uses little-endian on x86
    printf("Version: %u\n", version);
    
    // Offset 8-9: audio block size
    uint16_t audioBlockSize = readUint16LE(f);
    printf("Audio block size: %u bytes\n", audioBlockSize);
    
    // Offset 10-11: primer compressed flag
    uint16_t primerCompressed = readUint16LE(f);
    
    // Offset 12-13: unused
    fseek(f, 2, SEEK_CUR);
    
    // Offset 14-15: total frames
    uint16_t totalFrames = readUint16LE(f);
    printf("Total frames: %u\n", totalFrames);
    
    // Offset 16-17: palette size
    uint16_t paletteSize = readUint16LE(f);
    printf("Palette size: %u bytes\n", paletteSize);
    
    // Offset 18-19: primer reserved space
    uint16_t primerReservedSpace = readUint16LE(f);
    printf("Primer reserved: %u bytes\n", primerReservedSpace);
    
    // Offset 20-23: coordinate resolution (skip for now)
    fseek(f, 4, SEEK_CUR);
    
    // Offset 24: has palette
    uint8_t hasPalette = fgetc(f);
    
    // Offset 25: has audio
    uint8_t hasAudio = fgetc(f);
    printf("Has audio: %s\n", hasAudio ? "yes" : "no");
    
    // Offset 26-27: unused
    fseek(f, 2, SEEK_CUR);
    
    // Offset 28-29: frame rate
    int16_t frameRate = (int16_t)readUint16LE(f);
    printf("Frame rate: %d fps\n", frameRate);
    
    // Skip to primer (offset 60)
    fseek(f, 60, SEEK_SET);

    printf("\n--- Primer Information ---\n");
    
    int32_t evenPrimerSize = 0;
    int32_t oddPrimerSize = 0;
    
    if (hasAudio && primerReservedSpace > 0) {
        long primerStart = ftell(f);
        
        // Lire le header du primer (Robot utilise little-endian)
        int32_t totalPrimerSize = readSint32(f, false);
        int16_t compressionType = (int16_t)readUint16LE(f);
        evenPrimerSize = readSint32(f, false);
        oddPrimerSize = readSint32(f, false);
        
        printf("Total primer size: %d bytes\n", totalPrimerSize);
        printf("Compression type: %d\n", compressionType);
        printf("Even primer size: %d bytes\n", evenPrimerSize);
        printf("Odd primer size: %d bytes\n", oddPrimerSize);
        
        // Sauter le primer pour aller à la palette
        fseek(f, primerStart + primerReservedSpace, SEEK_SET);
    } else if (hasAudio && primerCompressed) {
        // Valeurs par défaut selon ScummVM
        evenPrimerSize = 19922;
        oddPrimerSize = 21024;
        printf("Using default primer sizes (zero-compressed)\n");
        printf("Even primer size: %d bytes\n", evenPrimerSize);
        printf("Odd primer size: %d bytes\n", oddPrimerSize);
    } else {
        printf("No primer data\n");
    }

    // Sauter la palette
    fseek(f, paletteSize, SEEK_CUR);

    printf("\n--- Reading Frame Tables ---\n");

    // Lire les deux tables (videoSizes et packetSizes)
    std::vector<uint32_t> videoSizes(totalFrames);
    std::vector<uint32_t> packetSizes(totalFrames);

    if (version == 5) {
        for (int i = 0; i < totalFrames; i++) {
            videoSizes[i] = readSCI11_16(f);
        }
        for (int i = 0; i < totalFrames; i++) {
            packetSizes[i] = readSCI11_16(f);
        }
    } else {
        for (int i = 0; i < totalFrames; i++) {
            videoSizes[i] = readSCI11_32(f);
        }
        for (int i = 0; i < totalFrames; i++) {
            packetSizes[i] = readSCI11_32(f);
        }
    }

    printf("Read %u video sizes\n", totalFrames);
    printf("Read %u packet sizes\n", totalFrames);

    // Lire les cue times (256 × 32-bit)
    for (int i = 0; i < 256; i++) {
        readUint32LE(f);
    }
    
    // Lire les cue values (256 × 16-bit)
    for (int i = 0; i < 256; i++) {
        readUint16LE(f);
    }

    // Aligner sur 2048 bytes
    long currentPos = ftell(f);
    long alignedPos = ((currentPos + 2047) / 2048) * 2048;
    fseek(f, alignedPos, SEEK_SET);
    
    long firstRecordPos = ftell(f);
    printf("First record position: %ld (aligned to 2048)\n", firstRecordPos);

    printf("\n--- Counting Audio Frames ---\n");

    int audioFrameCount = 0;
    std::vector<long> audioFramePositions;
    std::vector<int32_t> audioAbsolutePositions;

    for (int i = 0; i < totalFrames; i++) {
        // Calculer la position de la frame
        long recordPos = firstRecordPos;
        for (int j = 0; j < i; j++) {
            recordPos += packetSizes[j];
        }

        // Aller à la position audio (après la vidéo)
        long audioHeaderPos = recordPos + videoSizes[i];
        fseek(f, audioHeaderPos, SEEK_SET);

        // Vérifier qu'on a assez de données pour le header audio
        long currentPos = ftell(f);
        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        
        if (audioHeaderPos + 8 > fileSize) {
            break; // Pas assez de données
        }
        
        fseek(f, audioHeaderPos, SEEK_SET);

        // Lire le header audio (8 bytes, little-endian)
        int32_t audioAbsolutePosition = readSint32(f, false);
        int32_t audioBlockSize = readSint32(f, false);

        audioFrameCount++;

        if (audioBlockSize > 0 && audioAbsolutePosition >= 0 && audioBlockSize < 100000) {  // Sanity check
            // Frame audio valide trouvée
            audioFramePositions.push_back(audioHeaderPos);
            audioAbsolutePositions.push_back(audioAbsolutePosition);
        } else if (audioBlockSize < 0 || audioBlockSize >= 100000) {
            // Frame audio invalide
        }
    }

    printf("\n=== RESULT ===\n");
    printf("Audio frames: %d\n", audioFrameCount);
    
    printf("\n=== AUDIO FRAME POSITIONS ===\n");
    for (size_t i = 0; i < audioFramePositions.size(); i++) {
        printf("Frame %3zu: filePos=%7ld audioAbsPos=%6d (channel: %s)\n", 
               i, 
               audioFramePositions[i],
               audioAbsolutePositions[i],
               (audioAbsolutePositions[i] % 4 == 0) ? "EVEN" : "ODD");
    }

    fclose(f);
    return 0;
}
