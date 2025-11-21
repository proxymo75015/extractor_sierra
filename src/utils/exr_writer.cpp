#include "exr_writer.h"
#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfStringAttribute.h>
#include <OpenEXR/ImfIntAttribute.h>
#include <OpenEXR/ImfFloatAttribute.h>
#include <OpenEXR/ImfArray.h>
#include <OpenEXR/half.h>
#include <stdexcept>
#include <cstring>

using namespace Imf;
using namespace Imath;

namespace SierraExtractor {

// ============================================================================
// EXRWriter Implementation
// ============================================================================

EXRWriter::EXRWriter(const std::string& filename, int width, int height)
    : filename_(filename)
    , width_(width)
    , height_(height)
    , compression_(Compression::ZIP) {
    
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid image dimensions");
    }
}

EXRWriter::~EXRWriter() = default;

void EXRWriter::setCompression(Compression compression) {
    compression_ = compression;
}

void EXRWriter::addStringAttribute(const std::string& name, const std::string& value) {
    string_attributes_[name] = value;
}

void EXRWriter::addIntAttribute(const std::string& name, int value) {
    int_attributes_[name] = value;
}

void EXRWriter::addFloatAttribute(const std::string& name, float value) {
    float_attributes_[name] = value;
}

bool EXRWriter::validateChannelSize(size_t data_size) const {
    size_t expected = static_cast<size_t>(width_) * height_;
    return data_size == expected;
}

void EXRWriter::convertUint8ToHalf(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    size_t pixel_count = input.size();
    output.resize(pixel_count * sizeof(half));
    
    half* half_data = reinterpret_cast<half*>(output.data());
    
    for (size_t i = 0; i < pixel_count; ++i) {
        // Convertir uint8 (0-255) vers float (0.0-1.0) puis vers half
        float normalized = static_cast<float>(input[i]) / 255.0f;
        half_data[i] = half(normalized);
    }
}

void EXRWriter::addChannel(const std::string& name, const std::vector<uint8_t>& data) {
    if (!validateChannelSize(data.size())) {
        throw std::runtime_error("Channel data size mismatch: " + name);
    }
    
    ChannelData channel;
    channel.type = PixelType::HALF;  // uint8 converti en half pour EXR
    convertUint8ToHalf(data, channel.data_bytes);
    
    channels_[name] = std::move(channel);
}

void EXRWriter::addChannel(const std::string& name, const std::vector<float>& data) {
    if (!validateChannelSize(data.size())) {
        throw std::runtime_error("Channel data size mismatch: " + name);
    }
    
    ChannelData channel;
    channel.type = PixelType::FLOAT;
    channel.data_bytes.resize(data.size() * sizeof(float));
    std::memcpy(channel.data_bytes.data(), data.data(), channel.data_bytes.size());
    
    channels_[name] = std::move(channel);
}

void EXRWriter::addChannel(const std::string& name, const std::vector<uint32_t>& data) {
    if (!validateChannelSize(data.size())) {
        throw std::runtime_error("Channel data size mismatch: " + name);
    }
    
    ChannelData channel;
    channel.type = PixelType::UINT;
    channel.data_bytes.resize(data.size() * sizeof(uint32_t));
    std::memcpy(channel.data_bytes.data(), data.data(), channel.data_bytes.size());
    
    channels_[name] = std::move(channel);
}

bool EXRWriter::write() {
    try {
        // Créer le header EXR
        Header header(width_, height_);
        
        // Configurer la compression
        switch (compression_) {
            case Compression::NONE:
                header.compression() = NO_COMPRESSION;
                break;
            case Compression::RLE:
                header.compression() = RLE_COMPRESSION;
                break;
            case Compression::ZIPS:
                header.compression() = ZIPS_COMPRESSION;
                break;
            case Compression::ZIP:
                header.compression() = ZIP_COMPRESSION;
                break;
            case Compression::PIZ:
                header.compression() = PIZ_COMPRESSION;
                break;
            case Compression::PXR24:
                header.compression() = PXR24_COMPRESSION;
                break;
            case Compression::B44:
                header.compression() = B44_COMPRESSION;
                break;
            case Compression::B44A:
                header.compression() = B44A_COMPRESSION;
                break;
            case Compression::DWAA:
                header.compression() = DWAA_COMPRESSION;
                break;
            case Compression::DWAB:
                header.compression() = DWAB_COMPRESSION;
                break;
        }
        
        // Ajouter les métadonnées string
        for (const auto& attr : string_attributes_) {
            header.insert(attr.first, StringAttribute(attr.second));
        }
        
        // Ajouter les métadonnées int
        for (const auto& attr : int_attributes_) {
            header.insert(attr.first, IntAttribute(attr.second));
        }
        
        // Ajouter les métadonnées float
        for (const auto& attr : float_attributes_) {
            header.insert(attr.first, FloatAttribute(attr.second));
        }
        
        // Créer la liste des canaux
        ChannelList& channels = header.channels();
        
        for (const auto& ch : channels_) {
            PixelType pixel_type;
            switch (ch.second.type) {
                case PixelType::UINT:
                    pixel_type = UINT;
                    break;
                case PixelType::HALF:
                    pixel_type = HALF;
                    break;
                case PixelType::FLOAT:
                    pixel_type = FLOAT;
                    break;
            }
            
            channels.insert(ch.first, Channel(pixel_type));
        }
        
        // Créer le fichier de sortie
        OutputFile file(filename_.c_str(), header);
        
        // Créer le framebuffer
        FrameBuffer frameBuffer;
        
        for (const auto& ch : channels_) {
            size_t pixel_size = 0;
            switch (ch.second.type) {
                case PixelType::UINT:
                    pixel_size = sizeof(uint32_t);
                    break;
                case PixelType::HALF:
                    pixel_size = sizeof(half);
                    break;
                case PixelType::FLOAT:
                    pixel_size = sizeof(float);
                    break;
            }
            
            // L'image est stockée en row-major order
            char* base = reinterpret_cast<char*>(
                const_cast<uint8_t*>(ch.second.data_bytes.data())
            );
            
            frameBuffer.insert(
                ch.first,
                Slice(
                    ch.second.type == PixelType::UINT ? UINT :
                    ch.second.type == PixelType::HALF ? HALF : FLOAT,
                    base,
                    pixel_size,                    // xStride
                    pixel_size * width_            // yStride
                )
            );
        }
        
        // Écrire le framebuffer
        file.setFrameBuffer(frameBuffer);
        file.writePixels(height_);
        
        return true;
        
    } catch (const std::exception& e) {
        // Log l'erreur (à adapter selon votre système de logging)
        return false;
    }
}

} // namespace SierraExtractor
