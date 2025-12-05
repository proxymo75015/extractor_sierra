// Example: Loading and using Robot positions in the extractor
// Add this to src/main.cpp or create src/robot_positions.cpp

#include <fstream>
#include <sstream>
#include <map>
#include <iostream>
#include <cstdint>

struct RobotPosition {
    int16_t x;
    int16_t y;
    
    RobotPosition() : x(0), y(0) {}
    RobotPosition(int16_t _x, int16_t _y) : x(_x), y(_y) {}
};

class RobotPositionManager {
private:
    std::map<uint16_t, RobotPosition> positions;
    bool loaded;
    
public:
    RobotPositionManager() : loaded(false) {}
    
    bool loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open " << filename << std::endl;
            std::cerr << "Robot videos will use default centered positions." << std::endl;
            return false;
        }
        
        std::string line;
        int lineNum = 0;
        
        while (std::getline(file, line)) {
            lineNum++;
            
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            std::istringstream iss(line);
            uint16_t robotId;
            int16_t x, y;
            
            if (!(iss >> robotId >> x >> y)) {
                std::cerr << "Warning: Invalid format at line " << lineNum 
                          << ": " << line << std::endl;
                continue;
            }
            
            positions[robotId] = RobotPosition(x, y);
            std::cout << "Loaded Robot " << robotId << ": X=" << x << " Y=" << y << std::endl;
        }
        
        file.close();
        loaded = true;
        
        std::cout << "Loaded " << positions.size() << " Robot positions from " 
                  << filename << std::endl;
        
        return true;
    }
    
    RobotPosition getPosition(uint16_t robotId, int videoWidth, int videoHeight, 
                              int gameWidth = 630, int gameHeight = 450) {
        // Try to get from loaded positions
        if (positions.count(robotId) > 0) {
            return positions[robotId];
        }
        
        // Fallback: calculate default centered position
        std::cout << "Warning: No position found for Robot " << robotId 
                  << ", using centered default" << std::endl;
        
        int16_t x = (gameWidth - videoWidth) / 2;
        int16_t y = gameHeight / 3;  // Upper third of screen
        
        return RobotPosition(x, y);
    }
    
    bool hasPosition(uint16_t robotId) const {
        return positions.count(robotId) > 0;
    }
    
    bool isLoaded() const {
        return loaded;
    }
    
    size_t count() const {
        return positions.size();
    }
};

// Global instance (or pass as parameter)
static RobotPositionManager g_robotPositions;

// Usage example in extractRobotVideo():
/*

void extractRobotVideo(const std::string& rbtPath, uint16_t robotId) {
    // Load positions once at startup
    static bool positionsLoaded = false;
    if (!positionsLoaded) {
        g_robotPositions.loadFromFile("robot_positions.txt");
        positionsLoaded = true;
    }
    
    // ... decode RBT header to get video dimensions ...
    int videoWidth = 330;   // Example from RBT header
    int videoHeight = 242;  // Example from RBT header
    
    // Get position for this Robot
    RobotPosition pos = g_robotPositions.getPosition(robotId, videoWidth, videoHeight);
    
    std::cout << "Positioning Robot " << robotId << " at X=" << pos.x 
              << " Y=" << pos.y << std::endl;
    
    // Create canvas (game resolution)
    const int canvasWidth = 630;
    const int canvasHeight = 450;
    
    std::vector<uint8_t> canvas(canvasWidth * canvasHeight * 3, 0);  // RGB
    
    // For each decoded frame:
    for (int frameNum = 0; frameNum < numFrames; frameNum++) {
        // Clear canvas (or use background)
        std::fill(canvas.begin(), canvas.end(), 0);
        
        // Decode Robot frame into temporary buffer
        std::vector<uint8_t> robotFrame = decodeRobotFrame(frameNum);
        
        // Composite Robot frame onto canvas at position (pos.x, pos.y)
        for (int y = 0; y < videoHeight; y++) {
            for (int x = 0; x < videoWidth; x++) {
                int canvasX = pos.x + x;
                int canvasY = pos.y + y;
                
                // Bounds check
                if (canvasX >= 0 && canvasX < canvasWidth && 
                    canvasY >= 0 && canvasY < canvasHeight) {
                    
                    int srcIdx = (y * videoWidth + x) * 3;
                    int dstIdx = (canvasY * canvasWidth + canvasX) * 3;
                    
                    canvas[dstIdx + 0] = robotFrame[srcIdx + 0];  // R
                    canvas[dstIdx + 1] = robotFrame[srcIdx + 1];  // G
                    canvas[dstIdx + 2] = robotFrame[srcIdx + 2];  // B
                }
            }
        }
        
        // Write canvas to video encoder (FFmpeg, libav, etc.)
        writeFrameToVideo(canvas.data(), canvasWidth, canvasHeight);
    }
}

*/

// Example main() integration:
/*

int main(int argc, char* argv[]) {
    // Load Robot positions at startup
    if (!g_robotPositions.loadFromFile("robot_positions.txt")) {
        std::cout << "Warning: robot_positions.txt not found." << std::endl;
        std::cout << "Videos will be centered by default." << std::endl;
        std::cout << std::endl;
        std::cout << "To extract proper positions:" << std::endl;
        std::cout << "  1. See README_ROBOT_POSITIONS.md" << std::endl;
        std::cout << "  2. Run extract_robot_positions.sh" << std::endl;
        std::cout << "  3. Or manually create robot_positions.txt" << std::endl;
        std::cout << std::endl;
    }
    
    // ... rest of extraction code ...
    
    return 0;
}

*/
