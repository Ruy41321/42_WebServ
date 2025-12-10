#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <string>
#include "ClientConnection.hpp"
#include "Config.hpp"

class CgiHandler;

class HttpRequest {
private:
    Config& config;
    CgiHandler* cgiHandler;
    
    bool validateRequestLine(const std::string& method, const std::string& path, 
                            const std::string& version, ClientConnection* client);
    bool isMethodImplemented(const std::string& method);
    bool isMethodAllowed(const std::string& method, const std::string& path, size_t serverIndex);
    bool checkRedirect(const std::string& path, size_t serverIndex, 
                      std::string& redirectUrl, int& statusCode);
    bool checkHostHeader(const std::string& headers, const std::string& version);
    bool checkBodySizeLimit(ClientConnection* client, const std::string& method,
                           const std::string& path, const std::string& headers, size_t bodyStart);
    
    const LocationConfig* findBestLocation(const std::string& path, const ServerConfig& server);
    std::string getPathRelativeToLocation(const std::string& path, const LocationConfig* location);
    std::string buildFilePath(const std::string& path, const ServerConfig& server, 
                             const LocationConfig* location);
    bool findUploadLocation(const std::string& path, std::string& uploadDir, size_t serverIndex);
    
    bool getContentLength(const std::string& headers, size_t& contentLength);
    std::string getContentType(const std::string& headers);
    std::string getBoundary(const std::string& headers);
    bool isUploadRequest(const std::string& headers);
    bool isChunkedTransferEncoding(const std::string& headers);
    std::string unchunkBody(const std::string& chunkedBody);
    
    std::string extractFilename(const std::string& headers, const std::string& path);
    std::string sanitizeFilename(const std::string& filename);
    std::string generateUniqueFilename(const std::string& directory, const std::string& filename);
    std::string extractMultipartBody(const std::string& body, const std::string& headers, 
                                     std::string& extractedFilename);
    bool saveUploadedFile(const std::string& fullPath, const std::string& body);
    
    bool handleCgiRequest(ClientConnection* client, const std::string& method,
                         const std::string& path, const std::string& headers,
                         size_t bodyStart);
    std::string extractCgiBody(ClientConnection* client, const std::string& headers, size_t bodyStart);
    
    void handlePostUpload(ClientConnection* client, const std::string& path,
                         const std::string& headers, size_t bodyStart);
    
public:
    HttpRequest(Config& cfg);
    ~HttpRequest();
    
    void handleRequest(ClientConnection* client);
    
    void handleGet(ClientConnection* client, const std::string& path);
    void handleHead(ClientConnection* client, const std::string& path);
    void handlePost(ClientConnection* client, const std::string& path, 
                   const std::string& headers, size_t bodyStart);
    void handlePut(ClientConnection* client, const std::string& path,
                  const std::string& headers, size_t bodyStart);
    void handleDelete(ClientConnection* client, const std::string& path);
    
    CgiHandler* getCgiHandler() const;
    
    static bool isRequestComplete(const std::string& buffer);
    static std::string extractMethod(const std::string& headers);
    static std::string extractPath(const std::string& headers);
};

#endif
