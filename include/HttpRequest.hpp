#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <string>
#include "ClientConnection.hpp"
#include "Config.hpp"

class HttpRequest {
private:
    Config& config;
    
public:
    HttpRequest(Config& cfg);
    
    void handleRequest(ClientConnection* client);
    void handleGet(ClientConnection* client, const std::string& path);
    void handlePost(ClientConnection* client, const std::string& path, 
                   const std::string& headers, size_t bodyStart);
    void handleDelete(ClientConnection* client, const std::string& path);
    
    static bool isRequestComplete(const std::string& buffer);
    static std::string extractMethod(const std::string& headers);
    static std::string extractPath(const std::string& headers);
};

#endif
