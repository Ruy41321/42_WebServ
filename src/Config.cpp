#include "../include/Config.hpp"
#include <sstream>

// LocationConfig constructor with default values
LocationConfig::LocationConfig() 
    : path("/"), root(""), alias(""), index(""), autoindex(false),
      uploadStore(""), redirect(""), clientMaxBodySize(0) {
}

// ServerConfig constructor with default values
ServerConfig::ServerConfig() 
    : host("127.0.0.1"), port(8080), root("./www"),
      index("index.html"), autoindex(false), clientMaxBodySize(1048576) {
}

Config::Config() : configFile("") {
}

Config::~Config() {
}

// Trim whitespace from string
std::string Config::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    size_t end = str.find_last_not_of(" \t\r\n");
    
    if (start == std::string::npos || end == std::string::npos) {
        return "";
    }
    return str.substr(start, end - start + 1);
}

// Split string by delimiter
std::vector<std::string> Config::split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    
    while (std::getline(tokenStream, token, delimiter)) {
        std::string trimmed = trim(token);
        if (!trimmed.empty()) {
            tokens.push_back(trimmed);
        }
    }
    return tokens;
}

// Parse a location block
bool Config::parseLocationBlock(std::ifstream& file, std::string& line, ServerConfig& server) {
    LocationConfig location;
    
    // Parse location line: "location /path {"
    size_t pathStart = line.find_first_not_of(" \t", 8); // Skip "location"
    size_t pathEnd = line.find('{');
    
    if (pathStart == std::string::npos || pathEnd == std::string::npos) {
        std::cerr << "Error: Invalid location syntax" << std::endl;
        return false;
    }
    
    location.path = trim(line.substr(pathStart, pathEnd - pathStart));
    
    // Parse location body
    while (std::getline(file, line)) {
        line = trim(line);
        
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Remove inline comments
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = trim(line.substr(0, commentPos));
        }
        
        if (line.empty()) {
            continue;
        }
        
        if (line[0] == '}') {
            break;
        }
        
        // Remove semicolon at end
        if (!line.empty() && line[line.length() - 1] == ';') {
            line = line.substr(0, line.length() - 1);
        }
        
        std::vector<std::string> tokens = split(line, ' ');
        if (tokens.empty()) {
            continue;
        }
        
        std::string directive = tokens[0];
        
        if (directive == "root" && tokens.size() >= 2) {
            location.root = tokens[1];
        }
        else if (directive == "alias" && tokens.size() >= 2) {
            location.alias = tokens[1];
        }
        else if (directive == "allow_methods" && tokens.size() >= 2) {
            for (size_t i = 1; i < tokens.size(); ++i) {
                location.allowMethods.push_back(tokens[i]);
            }
        }
        else if (directive == "index" && tokens.size() >= 2) {
            location.index = tokens[1];
        }
        else if (directive == "autoindex" && tokens.size() >= 2) {
            location.autoindex = (tokens[1] == "on");
            location.hasAutoindex = true;
        }
        else if (directive == "upload_store" && tokens.size() >= 2) {
            location.uploadStore = tokens[1];
        }
        else if (directive == "cgi_path" && tokens.size() >= 2) {
            for (size_t i = 1; i < tokens.size(); ++i) {
                location.cgiPath.push_back(tokens[i]);
            }
        }
        else if (directive == "cgi_ext" && tokens.size() >= 2) {
            for (size_t i = 1; i < tokens.size(); ++i) {
                location.cgiExt.push_back(tokens[i]);
            }
        }
        else if (directive == "return" && tokens.size() >= 2) {
            // Combine all tokens after "return" into redirect string
            location.redirect = "";
            for (size_t i = 1; i < tokens.size(); ++i) {
                if (i > 1) location.redirect += " ";
                location.redirect += tokens[i];
            }
        }
        else if (directive == "client_max_body_size" && tokens.size() >= 2) {
            int bodySize = std::atoi(tokens[1].c_str());
            if (bodySize <= 0) {
                std::cerr << "Error: Invalid client_max_body_size " << bodySize << " (must be positive)" << std::endl;
                return false;
            }
            location.clientMaxBodySize = bodySize;
        }
    }
    
    server.locations.push_back(location);
    return true;
}

// Parse a server block
bool Config::parseServerBlock(std::ifstream& file, std::string& line) {
    ServerConfig server;
    
    // Parse server body
    while (std::getline(file, line)) {
        line = trim(line);
        
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Remove inline comments
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = trim(line.substr(0, commentPos));
        }
        
        if (line.empty()) {
            continue;
        }
        
        if (line[0] == '}') {
            break;
        }
        
        // Check for syntax: directives should end with semicolon (except blocks)
        if (line.find("location") == std::string::npos && 
            line.find('{') == std::string::npos && 
            line.find('}') == std::string::npos) {
            if (line[line.length() - 1] != ';') {
                std::cerr << "Error: Missing semicolon after directive: " << line << std::endl;
                return false;
            }
        }
        
        // Check for location block
        if (line.find("location") == 0) {
            if (!parseLocationBlock(file, line, server)) {
                return false;
            }
            continue;
        }
        
        // Remove semicolon at end
        if (!line.empty() && line[line.length() - 1] == ';') {
            line = line.substr(0, line.length() - 1);
        }
        
        std::vector<std::string> tokens = split(line, ' ');
        if (tokens.empty()) {
            continue;
        }
        
        std::string directive = tokens[0];
        
        if (directive == "listen" && tokens.size() >= 2) {
            // Parse listen directive: "127.0.0.1:8080" or "8080"
            std::string listenValue = tokens[1];
            size_t colonPos = listenValue.find(':');
            
            if (colonPos != std::string::npos) {
                server.host = listenValue.substr(0, colonPos);
                server.port = std::atoi(listenValue.substr(colonPos + 1).c_str());
            } else {
                server.port = std::atoi(listenValue.c_str());
            }
            
            // Validate port range (1-65535)
            if (server.port < 1 || server.port > 65535) {
                std::cerr << "Error: Invalid port number " << server.port << " (must be 1-65535)" << std::endl;
                return false;
            }
        }
        else if (directive == "root" && tokens.size() >= 2) {
            server.root = tokens[1];
        }
        else if (directive == "index" && tokens.size() >= 2) {
            server.index = tokens[1];
        }
        else if (directive == "autoindex" && tokens.size() >= 2) {
            server.autoindex = (tokens[1] == "on");
        }
        else if (directive == "client_max_body_size" && tokens.size() >= 2) {
            int bodySize = std::atoi(tokens[1].c_str());
            if (bodySize <= 0) {
                std::cerr << "Error: Invalid client_max_body_size " << bodySize << " (must be positive)" << std::endl;
                return false;
            }
            server.clientMaxBodySize = bodySize;
        }
        else if (directive == "error_page" && tokens.size() >= 3) {
            // Parse error_page: "error_page 404 /errors/404.html"
            // Can have multiple error codes: "error_page 500 502 503 /error.html"
            std::string errorPagePath = tokens[tokens.size() - 1];
            for (size_t i = 1; i < tokens.size() - 1; ++i) {
                int errorCode = std::atoi(tokens[i].c_str());
                server.errorPages[errorCode] = errorPagePath;
            }
        }
    }
    
    servers.push_back(server);
    return true;
}

// Load configuration from file
bool Config::loadFromFile(const std::string& filename) {
    configFile = filename;
    servers.clear();
    
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open configuration file: " << filename << std::endl;
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Check for server block
        if (line.find("server") == 0 && line.find('{') != std::string::npos) {
            if (!parseServerBlock(file, line)) {
                file.close();
                return false;
            }
        }
    }
    
    file.close();
    
    if (servers.empty()) {
        std::cerr << "Error: No server blocks found in configuration file" << std::endl;
        return false;
    }
    
    return true;
}

// Getters
const std::vector<ServerConfig>& Config::getServers() const {
    return servers;
}

size_t Config::getServerCount() const {
    return servers.size();
}

const ServerConfig& Config::getServer(size_t index) const {
    return servers[index];
}

// Legacy getters for backward compatibility (return first server's config)
int Config::getPort() const {
    if (servers.empty()) {
        return 8080;
    }
    return servers[0].port;
}

std::string Config::getHost() const {
    if (servers.empty()) {
        return "127.0.0.1";
    }
    return servers[0].host;
}

std::string Config::getRoot() const {
    if (servers.empty()) {
        return "./www";
    }
    return servers[0].root;
}

std::string Config::getIndex() const {
    if (servers.empty()) {
        return "index.html";
    }
    return servers[0].index;
}
