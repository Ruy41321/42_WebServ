#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <string>
#include "ClientConnection.hpp"
#include "Config.hpp"

class HttpRequest {
private:
    Config& config;
    
    // Helper methods for POST handling
    bool getContentLength(const std::string& headers, size_t& contentLength);
    bool isUploadRequest(const std::string& headers);
    bool findUploadLocation(const std::string& path, std::string& uploadDir);
    std::string extractFilename(const std::string& headers);
    bool saveUploadedFile(const std::string& fullPath, const std::string& body);
    
    void handlePostUpload(ClientConnection* client, const std::string& path,
                         const std::string& headers, size_t bodyStart);
    void handlePostData(ClientConnection* client, const std::string& path,
                       const std::string& headers, size_t bodyStart);
    
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
