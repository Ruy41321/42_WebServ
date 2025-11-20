#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <map>

// Structure to hold location-specific configuration
struct LocationConfig {
    std::string path;                           // Location path (e.g., "/", "/upload")
    std::string root;                           // Root directory for this location
    std::string alias;                          // Alternative to root (replaces location path)
    std::vector<std::string> allowMethods;      // Allowed HTTP methods (GET, POST, DELETE)
    std::string index;                          // Default file for directory (single file)
    bool autoindex;                             // Directory listing enabled
    bool hasAutoindex;                          // True if autoindex was explicitly set in config
    std::string uploadStore;                    // Upload storage directory (empty = disabled)
    std::vector<std::string> cgiPath;           // Paths to CGI interpreters
    std::vector<std::string> cgiExt;            // CGI file extensions
    std::string redirect;                       // HTTP redirect (e.g., "301 /new-page")
    size_t clientMaxBodySize;                   // Max body size for this location
    
    LocationConfig();
};

// Structure to hold server-specific configuration
struct ServerConfig {
    std::string host;                           // Server host/interface
    int port;                                   // Server port
    std::string root;                           // Root directory for this server
    std::string index;                          // Default index file (single file)
    bool autoindex;                             // Directory listing enabled by default
    size_t clientMaxBodySize;                   // Max client body size in bytes
    std::map<int, std::string> errorPages;      // Error code -> error page path
    std::vector<LocationConfig> locations;      // Location blocks for this server
    
    ServerConfig();
};

class Config {
private:
    std::vector<ServerConfig> servers;          // All server configurations
    std::string configFile;                     // Path to config file
    
    // Parsing helper methods
    bool parseServerBlock(std::ifstream& file, std::string& line);
    bool parseLocationBlock(std::ifstream& file, std::string& line, ServerConfig& server);
    std::string trim(const std::string& str);
    std::vector<std::string> split(const std::string& str, char delimiter);
    
public:
    Config();
    ~Config();
    
    // Load configuration from file
    bool loadFromFile(const std::string& filename);
    
    // Getters
    const std::vector<ServerConfig>& getServers() const;
    size_t getServerCount() const;
    const ServerConfig& getServer(size_t index) const;
    
    // Legacy getters for backward compatibility
    int getPort() const;
    std::string getHost() const;
    std::string getRoot() const;
    std::string getIndex() const;
};

#endif
