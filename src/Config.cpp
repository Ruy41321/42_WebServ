#include "../include/WebServer.hpp"

Config::Config() : port(8080), host("127.0.0.1"), root("./www"), index("index.html") {
}

Config::~Config() {
}

bool Config::loadFromFile(const std::string& filename) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open configuration file: " << filename << std::endl;
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Simple key-value parsing
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            
            // Trim whitespace
            size_t start = key.find_first_not_of(" \t\r\n");
            size_t end = key.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                key = key.substr(start, end - start + 1);
            }
            
            start = value.find_first_not_of(" \t\r\n");
            end = value.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                value = value.substr(start, end - start + 1);
            }
            
            if (key == "port") {
                port = std::atoi(value.c_str());
            } else if (key == "host") {
                host = value;
            } else if (key == "root") {
                root = value;
            } else if (key == "index") {
                index = value;
            }
        }
    }
    
    file.close();
    return true;
}

int Config::getPort() const {
    return port;
}

std::string Config::getHost() const {
    return host;
}

std::string Config::getRoot() const {
    return root;
}

std::string Config::getIndex() const {
    return index;
}
