#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

#include <string>
#include <sstream>
#include <vector>
#include "Config.hpp"

class HttpResponse {
public:
    // Success responses
    static std::string build200(const std::string& contentType, const std::string& body);
    static std::string build201(const std::string& body);
    static std::string build204();
    
    // Redirects
    static std::string build301(const std::string& location);
    static std::string build302(const std::string& location);
    
    // Client errors
    static std::string build400(const ServerConfig* serverConfig = NULL);
    static std::string build403(const std::string& message, const ServerConfig* serverConfig = NULL);
    static std::string build404(const ServerConfig* serverConfig = NULL);
    static std::string build405(const ServerConfig* serverConfig = NULL);
    static std::string build411(const ServerConfig* serverConfig = NULL);
    static std::string build413(const ServerConfig* serverConfig = NULL);
    
    // Server errors
    static std::string build500(const std::string& message, const ServerConfig* serverConfig = NULL);
    static std::string build501(const ServerConfig* serverConfig = NULL);
    static std::string build504(const ServerConfig* serverConfig = NULL);
    
    // Utility
    static std::string getStatusText(int statusCode);
    
    // File/Directory responses
    static std::string buildFileResponse(const std::string& fullPath, const ServerConfig* serverConfig = NULL);
    static std::string buildHeadResponse(const std::string& fullPath, const ServerConfig* serverConfig = NULL);
    static std::string buildDirectoryListing(const std::string& dirPath, const std::string& requestPath);
    
private:
    static std::string loadCustomErrorPage(int errorCode, const ServerConfig* serverConfig, const std::string& rootDir);
    static std::string buildErrorResponse(int errorCode, const std::string& statusText, const std::string& defaultBody, const ServerConfig* serverConfig, const std::string& rootDir);
    static void collectDirectoryEntries(const std::string& dirPath, std::vector<std::string>& files, std::vector<std::string>& directories);
    static std::string buildHtmlHeader(const std::string& requestPath);
    static std::string buildParentLink(const std::string& requestPath);
    static std::string buildEntriesTable(const std::vector<std::string>& directories, const std::vector<std::string>& files, const std::string& requestPath);
    static std::string getContentType(const std::string& path);
};

#endif
