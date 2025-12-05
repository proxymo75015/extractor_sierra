/*
 * RESSCI Extractor - Extract scripts and heap data from SCI resource files
 * Based on ScummVM's resource manager implementation
 * 
 * For SCI2.1 (Phantasmagoria):
 * - Scripts stored in RESSCI.00X files
 * - RESMAP.00X contains index (5-byte entries)
 * - Each script has embedded HEAP section (Local Variables)
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <iomanip>
#include <map>
#include <cstdint>
#include <algorithm>

// Resource types (from ScummVM engines/sci/resource/resource.cpp)
enum ResourceType {
    kResourceTypeView = 0,
    kResourceTypePic,
    kResourceTypeScript,
    kResourceTypeText,
    kResourceTypeSound,
    kResourceTypeMemory,
    kResourceTypeVocab,
    kResourceTypeFont,
    kResourceTypeCursor,
    kResourceTypePatch,
    kResourceTypeBitmap,
    kResourceTypePalette,
    kResourceTypeCdAudio,
    kResourceTypeAudio,
    kResourceTypeSync,
    kResourceTypeMessage,
    kResourceTypeMap,
    kResourceTypeHeap,
    kResourceTypeAudio36,
    kResourceTypeSync36,
    kResourceTypeTranslation,
    kResourceTypeRave,
    kResourceTypeInvalid
};

const char* resourceTypeNames[] = {
    "View", "Pic", "Script", "Text", "Sound", "Memory", "Vocab", "Font",
    "Cursor", "Patch", "Bitmap", "Palette", "CdAudio", "Audio", "Sync", "Message",
    "Map", "Heap", "Audio36", "Sync36", "Translation", "Rave"
};

// SCI2.1 resource map entry (5 bytes)
struct ResMapEntry {
    uint16_t resourceId;  // resource number
    uint32_t offset;      // offset in RESSCI file (24-bit)
    uint8_t volume;       // which RESSCI.00X file
    
    void read(std::ifstream& file) {
        uint8_t buf[5];
        file.read(reinterpret_cast<char*>(buf), 5);
        
        resourceId = buf[0] | (buf[1] << 8);
        offset = buf[2] | (buf[3] << 8) | (buf[4] << 16);
        volume = (offset >> 18) & 0x3F;  // Volume in upper bits
        offset &= 0x3FFFF;  // Actual offset in lower 18 bits
    }
};

// Resource header in RESSCI file
struct ResourceHeader {
    ResourceType type;
    uint16_t number;
    uint8_t compressionMethod;
    uint32_t compressedSize;
    uint32_t decompressedSize;
    
    bool read(std::ifstream& file) {
        uint8_t typeAndMethod;
        file.read(reinterpret_cast<char*>(&typeAndMethod), 1);
        
        type = static_cast<ResourceType>(typeAndMethod & 0x7F);
        compressionMethod = (typeAndMethod & 0x80) ? 1 : 0;
        
        uint8_t numBuf[2];
        file.read(reinterpret_cast<char*>(numBuf), 2);
        number = numBuf[0] | (numBuf[1] << 8);
        
        uint8_t sizeBuf[4];
        file.read(reinterpret_cast<char*>(sizeBuf), 4);
        compressedSize = sizeBuf[0] | (sizeBuf[1] << 8) | (sizeBuf[2] << 16);
        decompressedSize = sizeBuf[0] | (sizeBuf[1] << 8) | (sizeBuf[2] << 16);
        
        if (compressionMethod) {
            file.read(reinterpret_cast<char*>(sizeBuf), 4);
            decompressedSize = sizeBuf[0] | (sizeBuf[1] << 8) | (sizeBuf[2] << 16);
        }
        
        return file.good();
    }
};

// Script structure for SCI1.1 - SCI2.1
struct ScriptInfo {
    uint16_t scriptNumber;
    uint32_t scriptSize;
    uint32_t heapOffset;
    uint16_t localsCount;
    uint32_t localsOffset;
    uint16_t numExports;
    std::vector<uint8_t> data;
    
    void analyze() {
        if (data.size() < 8) return;
        
        // For SCI1.1-2.1: exports at offset 6
        numExports = data[6] | (data[7] << 8);
        
        // Script section ends where heap begins
        scriptSize = data.size();  // Will be adjusted when heap is found
        
        // Find heap offset (after script code)
        // In SCI2.1, heap is appended to script
        heapOffset = 0;  // Will be determined by searching for heap marker
        
        std::cout << "  Exports: " << numExports << "\n";
    }
    
    void findHeapSection() {
        // Heap section starts after script code
        // Look for local variables count marker
        if (data.size() < 100) return;
        
        // Try different offsets to find heap
        for (size_t offset = 50; offset < data.size() - 4; offset++) {
            uint16_t possibleLocalsCount = data[offset] | (data[offset + 1] << 8);
            
            // Reasonable check: locals count usually < 1000
            if (possibleLocalsCount > 0 && possibleLocalsCount < 1000) {
                // Check if there's enough space for locals
                if (offset + 2 + possibleLocalsCount * 2 <= data.size()) {
                    heapOffset = offset - 2;  // -2 because count is at offset+2
                    localsOffset = offset;
                    localsCount = possibleLocalsCount;
                    scriptSize = heapOffset;
                    
                    std::cout << "  Heap found at offset: 0x" << std::hex << heapOffset << std::dec << "\n";
                    std::cout << "  Script size: " << scriptSize << " bytes\n";
                    std::cout << "  Heap size: " << (data.size() - heapOffset) << " bytes\n";
                    std::cout << "  Locals count: " << localsCount << "\n";
                    break;
                }
            }
        }
    }
    
    void extractHeapData(const std::string& outputDir) {
        if (heapOffset == 0 || heapOffset >= data.size()) {
            std::cout << "  No heap section found\n";
            return;
        }
        
        // Save heap section
        std::string heapFile = outputDir + "/script_" + std::to_string(scriptNumber) + "_heap.bin";
        std::ofstream out(heapFile, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data() + heapOffset), data.size() - heapOffset);
        out.close();
        
        std::cout << "  Heap saved to: " << heapFile << "\n";
        
        // Dump local variables
        if (localsCount > 0 && localsOffset + localsCount * 2 <= data.size()) {
            std::cout << "  Local Variables (first 20):\n";
            for (int i = 0; i < std::min(20, static_cast<int>(localsCount)); i++) {
                uint16_t value = data[localsOffset + 2 + i * 2] | (data[localsOffset + 2 + i * 2 + 1] << 8);
                std::cout << "    Local[" << std::setw(3) << i << "] = " 
                         << std::setw(5) << value << " (0x" << std::hex << std::setw(4) 
                         << std::setfill('0') << value << std::dec << std::setfill(' ') << ")\n";
            }
        }
    }
};

class RESMAPReader {
private:
    std::map<uint16_t, std::vector<ResMapEntry>> resourceMap;  // type -> entries
    
public:
    bool load(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open: " << filename << "\n";
            return false;
        }
        
        std::cout << "Reading RESMAP: " << filename << "\n";
        
        uint8_t currentType = 0xFF;
        int entryCount = 0;
        
        // SCI1+ format: 5-byte entries with type markers (type | 0x80)
        while (file.good()) {
            uint8_t buf[5];
            file.read(reinterpret_cast<char*>(buf), 5);
            if (!file.good() || file.gcount() < 5) break;
            
            // Check if this is a type marker (high bit set)
            if (buf[0] & 0x80) {
                currentType = buf[0] & 0x7F;
                if (currentType < 22) {
                    std::cout << "  Found type: " << (int)currentType 
                             << " (" << resourceTypeNames[currentType] << ")\n";
                }
                // The rest of this 5-byte block is padding
                continue;
            }
            
            // It's a resource entry (5 bytes)
            ResMapEntry entry;
            entry.resourceId = buf[0] | (buf[1] << 8);
            entry.offset = buf[2] | (buf[3] << 8) | ((buf[4] & 0x3F) << 16);
            entry.volume = (buf[4] >> 6);
            
            resourceMap[currentType].push_back(entry);
            entryCount++;
            
            if (currentType == kResourceTypeScript) {
                std::cout << "    Script " << entry.resourceId 
                         << " at offset 0x" << std::hex << entry.offset 
                         << " in RESSCI.00" << std::dec << (int)entry.volume << "\n";
            }
        }
        
        std::cout << "Total entries read: " << entryCount << "\n";
        
        // Summary
        std::cout << "\nResource summary:\n";
        for (const auto& pair : resourceMap) {
            std::cout << "  Type " << pair.first;
            if (pair.first < 22) {
                std::cout << " (" << resourceTypeNames[pair.first] << ")";
            }
            std::cout << ": " << pair.second.size() << " resources\n";
        }
        
        return true;
    }
    
    const std::vector<ResMapEntry>* getResourcesOfType(ResourceType type) const {
        auto it = resourceMap.find(static_cast<uint16_t>(type));
        if (it != resourceMap.end()) {
            return &it->second;
        }
        return nullptr;
    }
};

class RESSCIReader {
private:
    std::string basePath;
    
public:
    RESSCIReader(const std::string& path) : basePath(path) {}
    
    bool extractScript(uint16_t scriptNum, uint32_t offset, uint8_t volume, const std::string& outputDir) {
        std::string volumeFile = basePath + "/RESSCI.00" + std::to_string(volume);
        std::ifstream file(volumeFile, std::ios::binary);
        
        if (!file) {
            std::cerr << "Failed to open: " << volumeFile << "\n";
            return false;
        }
        
        std::cout << "\nExtracting script " << scriptNum << " from " << volumeFile 
                 << " at offset 0x" << std::hex << offset << std::dec << "\n";
        
        file.seekg(offset);
        
        ResourceHeader header;
        if (!header.read(file)) {
            std::cerr << "Failed to read resource header\n";
            return false;
        }
        
        std::cout << "  Type: " << (int)header.type << " (" << resourceTypeNames[header.type] << ")\n";
        std::cout << "  Number: " << header.number << "\n";
        std::cout << "  Compressed: " << (header.compressionMethod ? "Yes" : "No") << "\n";
        std::cout << "  Size: " << header.compressedSize << " bytes\n";
        
        if (header.type != kResourceTypeScript) {
            std::cerr << "Warning: Expected script, got type " << (int)header.type << "\n";
        }
        
        // Read script data
        ScriptInfo script;
        script.scriptNumber = scriptNum;
        script.data.resize(header.compressedSize);
        
        file.read(reinterpret_cast<char*>(script.data.data()), header.compressedSize);
        
        // Analyze script structure
        script.analyze();
        script.findHeapSection();
        
        // Save complete script
        std::string scriptFile = outputDir + "/script_" + std::to_string(scriptNum) + "_complete.bin";
        std::ofstream out(scriptFile, std::ios::binary);
        out.write(reinterpret_cast<const char*>(script.data.data()), script.data.size());
        out.close();
        std::cout << "  Complete script saved to: " << scriptFile << "\n";
        
        // Extract and analyze heap
        script.extractHeapData(outputDir);
        
        return true;
    }
};

int main(int argc, char* argv[]) {
    std::string resourceDir = "Resource";
    std::string outputDir = "scripts_extracted";
    
    if (argc > 1) {
        resourceDir = argv[1];
    }
    if (argc > 2) {
        outputDir = argv[2];
    }
    
    std::cout << "RESSCI Extractor for SCI2.1 (Phantasmagoria)\n";
    std::cout << "=============================================\n\n";
    
    // Create output directory
    system(("mkdir -p " + outputDir).c_str());
    
    // Load RESMAP
    RESMAPReader resmap;
    if (!resmap.load(resourceDir + "/RESMAP.001")) {
        std::cerr << "Failed to load RESMAP.001\n";
        return 1;
    }
    
    // Get scripts
    const auto* scripts = resmap.getResourcesOfType(kResourceTypeScript);
    if (!scripts) {
        std::cerr << "No scripts found in RESMAP\n";
        return 1;
    }
    
    std::cout << "\nFound " << scripts->size() << " scripts\n";
    
    // Extract specific scripts of interest
    std::vector<uint16_t> scriptsToExtract = {902, 23, 13400, 0};  // Script 0 contains game object
    
    RESSCIReader reader(resourceDir);
    
    for (uint16_t scriptNum : scriptsToExtract) {
        // Find this script in the map
        for (const auto& entry : *scripts) {
            if (entry.resourceId == scriptNum) {
                reader.extractScript(scriptNum, entry.offset, entry.volume, outputDir);
                break;
            }
        }
    }
    
    std::cout << "\n\nExtraction complete! Check " << outputDir << "/ for output files.\n";
    std::cout << "\nNext steps:\n";
    std::cout << "1. Analyze heap sections for object properties\n";
    std::cout << "2. Look for Robot coordinate values (e.g., 315, 200 for positioning)\n";
    std::cout << "3. Parse object structures in heap to find _position.x/y properties\n";
    
    return 0;
}
