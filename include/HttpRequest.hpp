#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <string>
#include "ClientConnection.hpp"
#include "Config.hpp"

class HttpRequest {
private:
    Config& config;
    
    // ==================== Validation & Routing ====================
    bool validateRequestLine(const std::string& method, const std::string& path, 
                            const std::string& version, ClientConnection* client);
    bool isMethodImplemented(const std::string& method);
    bool isMethodAllowed(const std::string& method, const std::string& path, size_t serverIndex);
    bool checkRedirect(const std::string& path, size_t serverIndex, 
                      std::string& redirectUrl, int& statusCode);
    
    // ==================== Path & Location Helpers ====================
    const LocationConfig* findBestLocation(const std::string& path, const ServerConfig& server);
    std::string getPathRelativeToLocation(const std::string& path, const LocationConfig* location);
    std::string buildFilePath(const std::string& path, const ServerConfig& server, 
                             const LocationConfig* location);
    bool findUploadLocation(const std::string& path, std::string& uploadDir, size_t serverIndex);
    
    // ==================== Header Parsing ====================
    bool getContentLength(const std::string& headers, size_t& contentLength);
    std::string getContentType(const std::string& headers);
    std::string getBoundary(const std::string& headers);
    bool isUploadRequest(const std::string& headers);
    
    // ==================== File Upload Helpers ====================
    std::string extractFilename(const std::string& headers, const std::string& path);
    std::string sanitizeFilename(const std::string& filename);
    std::string generateUniqueFilename(const std::string& directory, const std::string& filename);
    std::string extractMultipartBody(const std::string& body, const std::string& headers, 
                                     std::string& extractedFilename);
    bool saveUploadedFile(const std::string& fullPath, const std::string& body);
    
    // ==================== Method Handlers ====================
    void handlePostUpload(ClientConnection* client, const std::string& path,
                         const std::string& headers, size_t bodyStart);
    void handlePostData(ClientConnection* client, const std::string& path,
                       const std::string& headers, size_t bodyStart);
    
public:
    HttpRequest(Config& cfg);
    
    // Main request dispatcher
    void handleRequest(ClientConnection* client);
    
    // HTTP Method handlers (public for potential future extensions)
    void handleGet(ClientConnection* client, const std::string& path);
    void handleHead(ClientConnection* client, const std::string& path);
    void handlePost(ClientConnection* client, const std::string& path, 
                   const std::string& headers, size_t bodyStart);
    void handlePut(ClientConnection* client, const std::string& path,
                  const std::string& headers, size_t bodyStart);
    void handleDelete(ClientConnection* client, const std::string& path);
    
    // Static utility methods
    static bool isRequestComplete(const std::string& buffer);
    static std::string extractMethod(const std::string& headers);
    static std::string extractPath(const std::string& headers);
};

#endif
