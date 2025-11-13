#include "include/Config.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }
    
    Config config;
    
    if (!config.loadFromFile(argv[1])) {
        std::cerr << "Failed to load configuration" << std::endl;
        return 1;
    }
    
    std::cout << "\n=== Configuration Loaded Successfully ===\n" << std::endl;
    std::cout << "Number of servers: " << config.getServerCount() << "\n" << std::endl;
    
    for (size_t i = 0; i < config.getServerCount(); ++i) {
        const ServerConfig& server = config.getServer(i);
        
        std::cout << "--- Server " << (i + 1) << " ---" << std::endl;
        std::cout << "  Listen: " << server.host << ":" << server.port << std::endl;
        std::cout << "  Root: " << server.root << std::endl;
        std::cout << "  Index: " << server.index << std::endl;
        std::cout << "  Autoindex: " << (server.autoindex ? "on" : "off") << std::endl;
        std::cout << "  Client Max Body Size: " << server.clientMaxBodySize << " bytes" << std::endl;
        
        if (!server.errorPages.empty()) {
            std::cout << "  Error Pages:" << std::endl;
            for (std::map<int, std::string>::const_iterator it = server.errorPages.begin();
                 it != server.errorPages.end(); ++it) {
                std::cout << "    " << it->first << " -> " << it->second << std::endl;
            }
        }
        
        std::cout << "  Locations (" << server.locations.size() << "):" << std::endl;
        for (size_t j = 0; j < server.locations.size(); ++j) {
            const LocationConfig& loc = server.locations[j];
            std::cout << "\n    Location: " << loc.path << std::endl;
            
            if (!loc.root.empty()) {
                std::cout << "      Root: " << loc.root << std::endl;
            }
            
            if (!loc.alias.empty()) {
                std::cout << "      Alias: " << loc.alias << std::endl;
            }
            
            if (!loc.allowMethods.empty()) {
                std::cout << "      Allow Methods: ";
                for (size_t k = 0; k < loc.allowMethods.size(); ++k) {
                    std::cout << loc.allowMethods[k];
                    if (k < loc.allowMethods.size() - 1) std::cout << ", ";
                }
                std::cout << std::endl;
            }
            
            if (!loc.index.empty()) {
                std::cout << "      Index: " << loc.index << std::endl;
            }
            
            std::cout << "      Autoindex: " << (loc.autoindex ? "on" : "off") << std::endl;
            
            if (!loc.uploadStore.empty()) {
                std::cout << "      Upload Store: " << loc.uploadStore << std::endl;
            }
            
            if (!loc.cgiPath.empty()) {
                std::cout << "      CGI Paths: ";
                for (size_t k = 0; k < loc.cgiPath.size(); ++k) {
                    std::cout << loc.cgiPath[k];
                    if (k < loc.cgiPath.size() - 1) std::cout << ", ";
                }
                std::cout << std::endl;
            }
            
            if (!loc.cgiExt.empty()) {
                std::cout << "      CGI Extensions: ";
                for (size_t k = 0; k < loc.cgiExt.size(); ++k) {
                    std::cout << loc.cgiExt[k];
                    if (k < loc.cgiExt.size() - 1) std::cout << ", ";
                }
                std::cout << std::endl;
            }
            
            if (!loc.redirect.empty()) {
                std::cout << "      Redirect: " << loc.redirect << std::endl;
            }
            
            if (loc.clientMaxBodySize > 0) {
                std::cout << "      Client Max Body Size: " << loc.clientMaxBodySize << " bytes" << std::endl;
            }
        }
        
        std::cout << std::endl;
    }
    
    return 0;
}
