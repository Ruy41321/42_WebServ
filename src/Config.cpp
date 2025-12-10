#include "../include/Config.hpp"
#include "../include/StringUtils.hpp"

LocationConfig::LocationConfig() 
    : path("/"), root(""), alias(""), index(""), autoindex(false), hasAutoindex(false),
      uploadStore(""), redirect(""), clientMaxBodySize(0), hasClientMaxBodySize(false) {}

ServerConfig::ServerConfig() 
    : host("127.0.0.1"), port(8080), root("./www"),
      index("index.html"), autoindex(false), clientMaxBodySize(1048576) {}

Config::Config() : configFile("") {}

Config::~Config() {}

std::string Config::trim(const std::string& str) {
    return StringUtils::trim(str);
}

std::vector<std::string> Config::split(const std::string& str, char delimiter) {
    return StringUtils::split(str, delimiter);
}

std::string Config::removeInlineComment(const std::string& line) {
    size_t commentPos = line.find('#');
    if (commentPos != std::string::npos)
        return trim(line.substr(0, commentPos));
    return line;
}

std::string Config::removeSemicolon(const std::string& line) {
    if (!line.empty() && line[line.length() - 1] == ';')
        return line.substr(0, line.length() - 1);
    return line;
}

bool Config::parseLocationDirective(const std::string& directive, const std::vector<std::string>& tokens, 
                                    LocationConfig& location) {
    if (directive == "root" && tokens.size() >= 2) {
        location.root = tokens[1];
    } else if (directive == "alias" && tokens.size() >= 2) {
        location.alias = tokens[1];
    } else if (directive == "allow_methods" && tokens.size() >= 2) {
        for (size_t i = 1; i < tokens.size(); ++i)
            location.allowMethods.push_back(tokens[i]);
    } else if (directive == "index" && tokens.size() >= 2) {
        location.index = tokens[1];
    } else if (directive == "autoindex" && tokens.size() >= 2) {
        location.autoindex = (tokens[1] == "on");
        location.hasAutoindex = true;
    } else if (directive == "upload_store" && tokens.size() >= 2) {
        location.uploadStore = tokens[1];
    } else if (directive == "cgi_path" && tokens.size() >= 2) {
        for (size_t i = 1; i < tokens.size(); ++i)
            location.cgiPath.push_back(tokens[i]);
    } else if (directive == "cgi_ext" && tokens.size() >= 2) {
        for (size_t i = 1; i < tokens.size(); ++i)
            location.cgiExt.push_back(tokens[i]);
    } else if (directive == "return" && tokens.size() >= 2) {
        location.redirect = "";
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (i > 1) location.redirect += " ";
            location.redirect += tokens[i];
        }
    } else if (directive == "client_max_body_size" && tokens.size() >= 2) {
        long bodySize = std::atol(tokens[1].c_str());
        if (bodySize < 0) {
            std::cerr << "Error: Invalid client_max_body_size (must be non-negative)" << std::endl;
            return false;
        }
        location.clientMaxBodySize = static_cast<size_t>(bodySize);
        location.hasClientMaxBodySize = true;
    }
    return true;
}

bool Config::parseLocationBlock(std::ifstream& file, std::string& line, ServerConfig& server) {
    LocationConfig location;
    
    size_t pathStart = line.find_first_not_of(" \t", 8);
    size_t pathEnd = line.find('{');
    
    if (pathStart == std::string::npos || pathEnd == std::string::npos) {
        std::cerr << "Error: Invalid location syntax" << std::endl;
        return false;
    }
    
    location.path = trim(line.substr(pathStart, pathEnd - pathStart));
    
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        
        line = removeInlineComment(line);
        if (line.empty())
            continue;
        
        if (line[0] == '}')
            break;
        
        line = removeSemicolon(line);
        std::vector<std::string> tokens = split(line, ' ');
        if (tokens.empty())
            continue;
        
        if (!parseLocationDirective(tokens[0], tokens, location))
            return false;
    }
    
    server.locations.push_back(location);
    return true;
}

bool Config::parseListenDirective(const std::vector<std::string>& tokens, ServerConfig& server) {
    if (tokens.size() < 2)
        return true;
    
    std::string listenValue = tokens[1];
    size_t colonPos = listenValue.find(':');
    
    if (colonPos != std::string::npos) {
        server.host = listenValue.substr(0, colonPos);
        server.port = std::atoi(listenValue.substr(colonPos + 1).c_str());
    } else {
        server.port = std::atoi(listenValue.c_str());
    }
    
    if (server.port < 1 || server.port > 65535) {
        std::cerr << "Error: Invalid port number " << server.port << " (must be 1-65535)" << std::endl;
        return false;
    }
    return true;
}

bool Config::parseServerDirective(const std::string& directive, const std::vector<std::string>& tokens, 
                                  ServerConfig& server) {
    if (directive == "listen") {
        return parseListenDirective(tokens, server);
    } else if (directive == "root" && tokens.size() >= 2) {
        server.root = tokens[1];
    } else if (directive == "index" && tokens.size() >= 2) {
        server.index = tokens[1];
    } else if (directive == "autoindex" && tokens.size() >= 2) {
        server.autoindex = (tokens[1] == "on");
    } else if (directive == "client_max_body_size" && tokens.size() >= 2) {
        long bodySize = std::atol(tokens[1].c_str());
        if (bodySize < 0) {
            std::cerr << "Error: Invalid client_max_body_size (must be non-negative)" << std::endl;
            return false;
        }
        server.clientMaxBodySize = static_cast<size_t>(bodySize);
    } else if (directive == "error_page" && tokens.size() >= 3) {
        std::string errorPagePath = tokens[tokens.size() - 1];
        for (size_t i = 1; i < tokens.size() - 1; ++i) {
            int errorCode = std::atoi(tokens[i].c_str());
            server.errorPages[errorCode] = errorPagePath;
        }
    }
    return true;
}

bool Config::validateServerLine(const std::string& line) {
    if (line.find("location") != std::string::npos || 
        line.find('{') != std::string::npos || 
        line.find('}') != std::string::npos)
        return true;
    
    if (line[line.length() - 1] != ';') {
        std::cerr << "Error: Missing semicolon after directive: " << line << std::endl;
        return false;
    }
    return true;
}

bool Config::parseServerBlock(std::ifstream& file, std::string& line) {
    ServerConfig server;
    
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        
        line = removeInlineComment(line);
        if (line.empty())
            continue;
        
        if (line[0] == '}')
            break;
        
        if (!validateServerLine(line))
            return false;
        
        if (line.find("location") == 0) {
            if (!parseLocationBlock(file, line, server))
                return false;
            continue;
        }
        
        line = removeSemicolon(line);
        std::vector<std::string> tokens = split(line, ' ');
        if (tokens.empty())
            continue;
        
        if (!parseServerDirective(tokens[0], tokens, server))
            return false;
    }
    
    servers.push_back(server);
    return true;
}

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
        
        if (line.empty() || line[0] == '#')
            continue;
        
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

const std::vector<ServerConfig>& Config::getServers() const {
    return servers;
}

size_t Config::getServerCount() const {
    return servers.size();
}

const ServerConfig& Config::getServer(size_t index) const {
    return servers[index];
}

int Config::getPort() const {
    return servers.empty() ? 8080 : servers[0].port;
}

std::string Config::getHost() const {
    return servers.empty() ? "127.0.0.1" : servers[0].host;
}

std::string Config::getRoot() const {
    return servers.empty() ? "./www" : servers[0].root;
}

std::string Config::getIndex() const {
    return servers.empty() ? "index.html" : servers[0].index;
}
