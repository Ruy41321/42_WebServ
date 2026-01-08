#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <map>

struct LocationConfig {
    std::string path;
    std::string root;
    std::string alias;
    std::vector<std::string> allowMethods;
    std::string index;
    bool autoindex;
    bool hasAutoindex;
    std::string uploadStore;
    std::vector<std::string> cgiPath;
    std::vector<std::string> cgiExt;
    std::string redirect;
    size_t clientMaxBodySize;
    bool hasClientMaxBodySize;
    
    LocationConfig();
};

struct ServerConfig {
    std::string host;
    int port;
    std::string root;
    std::string index;
    bool autoindex;
    size_t clientMaxBodySize;
    std::map<int, std::string> errorPages;
    std::vector<LocationConfig> locations;
    
    ServerConfig();
};

class Config {
private:
    std::vector<ServerConfig> servers;
    std::string configFile;
    
    bool parseServerBlock(std::ifstream& file, std::string& line);
    bool parseLocationBlock(std::ifstream& file, std::string& line, ServerConfig& server);
    bool parseServerDirective(const std::string& directive, const std::vector<std::string>& tokens, 
                              ServerConfig& server);
    bool parseLocationDirective(const std::string& directive, const std::vector<std::string>& tokens, 
                                LocationConfig& location);
    bool parseListenDirective(const std::vector<std::string>& tokens, ServerConfig& server);
    bool validateServerLine(const std::string& line);
    
    std::string trim(const std::string& str);
    std::vector<std::string> split(const std::string& str, char delimiter);
    std::string removeInlineComment(const std::string& line);
    std::string removeSemicolon(const std::string& line);
    
public:
    Config();
    ~Config();
    
    bool loadFromFile(const std::string& filename);
    
    const std::vector<ServerConfig>& getServers() const;
    size_t getServerCount() const;
    const ServerConfig& getServer(size_t index) const;
    
    int getPort() const;
    std::string getHost() const;
    std::string getRoot() const;
    std::string getIndex() const;
};

#endif
