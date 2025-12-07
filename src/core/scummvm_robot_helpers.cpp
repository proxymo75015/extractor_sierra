#include "scummvm_robot_helpers.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace ScummVMRobot {

std::vector<RobotPosition> loadRobotPositions(const std::string& filename) {
    std::vector<RobotPosition> positions;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open " << filename << std::endl;
        std::cerr << "         CANVAS mode will not be available (all robots will use CROP mode)" << std::endl;
        return positions;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Ignorer commentaires et lignes vides
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        std::istringstream iss(line);
        int robotId;
        int16_t x, y;
        
        if (iss >> robotId >> x >> y) {
            positions.push_back(RobotPosition(robotId, x, y));
        }
    }
    
    if (!positions.empty()) {
        std::cout << "Loaded " << positions.size() << " robot positions from " << filename << std::endl;
    }
    
    return positions;
}

RobotPosition findRobotPosition(const std::vector<RobotPosition>& positions, int robotId) {
    for (const auto& pos : positions) {
        if (pos.robotId == robotId) {
            return pos;
        }
    }
    
    // Non trouvé
    return RobotPosition(-1, 0, 0);
}

} // namespace ScummVMRobot
