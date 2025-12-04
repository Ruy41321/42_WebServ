#include "../include/HttpResponse.hpp"
#include <fstream>
#include <iostream>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>

// Helper function to load custom error page from filesystem
std::string HttpResponse::loadCustomErrorPage(int errorCode, const ServerConfig* serverConfig, const std::string& rootDir) {
    if (!serverConfig) {
        return "";
    }
    
    // Check if there's a custom error page configured for this error code
    std::map<int, std::string>::const_iterator it = serverConfig->errorPages.find(errorCode);
    if (it == serverConfig->errorPages.end()) {
        return "";
    }
    
    std::string errorPagePath = it->second;
    
    // Build full path to error page
    std::string fullPath;
    if (errorPagePath[0] == '/') {
        // Absolute path from root directory
        fullPath = rootDir + errorPagePath;
    } else {
        // Relative path
        fullPath = rootDir + "/" + errorPagePath;
    }
    
    // Try to open and read the error page file
    std::ifstream file(fullPath.c_str());
    if (!file.is_open()) {
        return ""; // File doesn't exist or can't be opened
    }
    
    // Read file content
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    return buffer.str();
}

// Generic function to build error response with custom or default page
std::string HttpResponse::buildErrorResponse(int errorCode, const std::string& statusText, 
                                              const std::string& defaultBody, 
                                              const ServerConfig* serverConfig,
                                              const std::string& rootDir) {
    std::string body;
    
    // Try to load custom error page
    if (serverConfig) {
        body = loadCustomErrorPage(errorCode, serverConfig, rootDir);
    }
    
    // If no custom page found or failed to load, use default
    if (body.empty()) {
        body = defaultBody;
    }
    
    std::ostringstream oss;
    oss << "HTTP/1.0 " << errorCode << " " << statusText << "\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.length() << "\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string HttpResponse::build200(const std::string& contentType, const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.0 200 OK\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.length() << "\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string HttpResponse::build201(const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.0 201 Created\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.length() << "\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string HttpResponse::build204() {
    std::ostringstream oss;
    oss << "HTTP/1.0 204 No Content\r\n"
        << "\r\n";
    return oss.str();
}

std::string HttpResponse::build301(const std::string& location) {
    std::string body = "<html><body><h1>301 Moved Permanently</h1><p>The document has moved <a href=\"" + location + "\">here</a>.</p></body></html>";
    std::ostringstream oss;
    oss << "HTTP/1.0 301 Moved Permanently\r\n"
        << "Location: " << location << "\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.length() << "\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string HttpResponse::build302(const std::string& location) {
    std::string body = "<html><body><h1>302 Found</h1><p>The document has moved <a href=\"" + location + "\">here</a>.</p></body></html>";
    std::ostringstream oss;
    oss << "HTTP/1.0 302 Found\r\n"
        << "Location: " << location << "\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.length() << "\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string HttpResponse::build400(const ServerConfig* serverConfig) {
    std::string defaultContent = "<html><body><h1>400 Bad Request</h1><p>The request could not be understood by the server.</p></body></html>";
    std::string rootDir = serverConfig ? serverConfig->root : "./www";
    return buildErrorResponse(400, "Bad Request", defaultContent, serverConfig, rootDir);
}

std::string HttpResponse::build403(const std::string& message, const ServerConfig* serverConfig) {
    std::ostringstream bodyStream;
    bodyStream << "<html><body><h1>403 Forbidden</h1><p>" << message << "</p></body></html>";
    std::string defaultBody = bodyStream.str();
    std::string rootDir = serverConfig ? serverConfig->root : "./www";
    return buildErrorResponse(403, "Forbidden", defaultBody, serverConfig, rootDir);
}

std::string HttpResponse::build404(const ServerConfig* serverConfig) {
    std::string defaultContent = "<html><body><h1>404 Not Found</h1><p>The requested resource was not found.</p></body></html>";
    std::string rootDir = serverConfig ? serverConfig->root : "./www";
    return buildErrorResponse(404, "Not Found", defaultContent, serverConfig, rootDir);
}

std::string HttpResponse::build405(const ServerConfig* serverConfig) {
    std::string defaultContent = "<html><body><h1>405 Method Not Allowed</h1><p>The method is not allowed for this resource.</p></body></html>";
    std::string rootDir = serverConfig ? serverConfig->root : "./www";
    return buildErrorResponse(405, "Method Not Allowed", defaultContent, serverConfig, rootDir);
}

std::string HttpResponse::build411(const ServerConfig* serverConfig) {
    std::string defaultContent = "<html><body><h1>411 Length Required</h1></body></html>";
    std::string rootDir = serverConfig ? serverConfig->root : "./www";
    return buildErrorResponse(411, "Length Required", defaultContent, serverConfig, rootDir);
}

std::string HttpResponse::build413(const ServerConfig* serverConfig) {
    std::string defaultBody = "<html><body><h1>413 Request Entity Too Large</h1></body></html>";
    std::string rootDir = serverConfig ? serverConfig->root : "./www";
    return buildErrorResponse(413, "Request Entity Too Large", defaultBody, serverConfig, rootDir);
}

std::string HttpResponse::build500(const std::string& message, const ServerConfig* serverConfig) {
    std::ostringstream bodyStream;
    bodyStream << "<html><body><h1>500 Internal Server Error</h1><p>" << message << "</p></body></html>";
    std::string defaultBody = bodyStream.str();
    std::string rootDir = serverConfig ? serverConfig->root : "./www";
    return buildErrorResponse(500, "Internal Server Error", defaultBody, serverConfig, rootDir);
}

std::string HttpResponse::build501(const ServerConfig* serverConfig) {
    std::string defaultContent = "<html><body><h1>501 Not Implemented</h1></body></html>";
    std::string rootDir = serverConfig ? serverConfig->root : "./www";
    return buildErrorResponse(501, "Not Implemented", defaultContent, serverConfig, rootDir);
}

std::string HttpResponse::build504(const ServerConfig* serverConfig) {
    std::string defaultContent = "<html><body><h1>504 Gateway Timeout</h1><p>CGI script timed out.</p></body></html>";
    std::string rootDir = serverConfig ? serverConfig->root : "./www";
    return buildErrorResponse(504, "Gateway Timeout", defaultContent, serverConfig, rootDir);
}

std::string HttpResponse::getStatusText(int statusCode) {
    switch (statusCode) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default: return "Unknown";
    }
}

std::string HttpResponse::buildFileResponse(const std::string& fullPath, const ServerConfig* serverConfig) {
    std::ifstream file(fullPath.c_str(), std::ios::binary);
    
    if (!file.is_open()) {
        return build404(serverConfig);
    }
    
    // Read file content
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    // Get content type
    std::string contentType = getContentType(fullPath);
    
    return build200(contentType, content);
}

std::string HttpResponse::buildHeadResponse(const std::string& fullPath, const ServerConfig* serverConfig) {
    std::ifstream file(fullPath.c_str(), std::ios::binary);
    
    if (!file.is_open()) {
        // Return 404 headers only (no body for HEAD)
        std::ostringstream oss;
        oss << "HTTP/1.0 404 Not Found\r\n"
            << "Content-Type: text/html\r\n"
            << "Content-Length: 0\r\n"
            << "\r\n";
        return oss.str();
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.close();
    
    // Get content type
    std::string contentType = getContentType(fullPath);
    
    // Return headers only (no body for HEAD)
    std::ostringstream oss;
    oss << "HTTP/1.0 200 OK\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << fileSize << "\r\n"
        << "\r\n";
    return oss.str();
    (void)serverConfig; // Unused for now, but kept for consistency
}

void HttpResponse::collectDirectoryEntries(const std::string& dirPath, std::vector<std::string>& files, std::vector<std::string>& directories) {
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        return;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        
        // Skip hidden files and current directory
        if (name[0] == '.') {
            continue;
        }
        
        // Build full path to check if it's a directory
        std::string fullPath = dirPath;
        if (fullPath[fullPath.length() - 1] != '/') {
            fullPath += "/";
        }
        fullPath += name;
        
        struct stat entryStat;
        if (stat(fullPath.c_str(), &entryStat) == 0) {
            if (S_ISDIR(entryStat.st_mode)) {
                directories.push_back(name);
            } else {
                files.push_back(name);
            }
        }
    }
    closedir(dir);
    
    // Sort entries
    std::sort(directories.begin(), directories.end());
    std::sort(files.begin(), files.end());
}

std::string HttpResponse::buildHtmlHeader(const std::string& requestPath) {
    std::ostringstream html;
    html << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head>\n"
         << "    <title>Index of " << requestPath << "</title>\n"
         << "    <style>\n"
         << "        body { font-family: Arial, sans-serif; margin: 40px; }\n"
         << "        h1 { border-bottom: 1px solid #ccc; padding-bottom: 10px; }\n"
         << "        table { border-collapse: collapse; width: 100%; }\n"
         << "        th, td { text-align: left; padding: 8px; border-bottom: 1px solid #ddd; }\n"
         << "        th { background-color: #f2f2f2; }\n"
         << "        a { text-decoration: none; color: #0066cc; }\n"
         << "        a:hover { text-decoration: underline; }\n"
         << "        .dir { font-weight: bold; }\n"
         << "    </style>\n"
         << "</head>\n"
         << "<body>\n"
         << "    <h1>Index of " << requestPath << "</h1>\n"
         << "    <table>\n"
         << "        <tr><th>Name</th><th>Type</th></tr>\n";
    return html.str();
}

std::string HttpResponse::buildParentLink(const std::string& requestPath) {
    if (requestPath == "/" || requestPath == "") {
        return "";
    }
    
    std::string parentPath = requestPath;
    if (parentPath[parentPath.length() - 1] == '/') {
        parentPath = parentPath.substr(0, parentPath.length() - 1);
    }
    
    size_t lastSlash = parentPath.find_last_of('/');
    if (lastSlash != std::string::npos) {
        parentPath = parentPath.substr(0, lastSlash + 1);
    } else {
        parentPath = "/";
    }
    
    std::ostringstream link;
    link << "        <tr><td><a href=\"" << parentPath << "\">../</a></td>"
         << "<td class=\"dir\">Directory</td></tr>\n";
    return link.str();
}

std::string HttpResponse::buildEntriesTable(const std::vector<std::string>& directories, const std::vector<std::string>& files, const std::string& requestPath) {
    std::ostringstream table;
    
    // List directories first
    for (size_t i = 0; i < directories.size(); ++i) {
        std::string href = requestPath;
        if (href[href.length() - 1] != '/') {
            href += "/";
        }
        href += directories[i];
        table << "        <tr><td><a href=\"" << href << "/\" class=\"dir\">" 
              << directories[i] << "/</a></td><td class=\"dir\">Directory</td></tr>\n";
    }
    
    // List files
    for (size_t i = 0; i < files.size(); ++i) {
        std::string href = requestPath;
        if (href[href.length() - 1] != '/') {
            href += "/";
        }
        href += files[i];
        table << "        <tr><td><a href=\"" << href << "\">" 
              << files[i] << "</a></td><td>File</td></tr>\n";
    }
    
    return table.str();
}

std::string HttpResponse::buildDirectoryListing(const std::string& dirPath, const std::string& requestPath) {
    // Collect files and directories
    std::vector<std::string> files;
    std::vector<std::string> directories;
    collectDirectoryEntries(dirPath, files, directories);
    
    if (files.empty() && directories.empty()) {
        DIR* testDir = opendir(dirPath.c_str());
        if (!testDir) {
            return build403("Cannot read directory.");
        }
        closedir(testDir);
    }
    
    // Build HTML response
    std::ostringstream body;
    body << buildHtmlHeader(requestPath)
         << buildParentLink(requestPath)
         << buildEntriesTable(directories, files, requestPath)
         << "    </table>\n"
         << "</body>\n"
         << "</html>";
    
    return build200("text/html", body.str());
}

std::string HttpResponse::getContentType(const std::string& path) {
    size_t dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos) {
        return "application/octet-stream";
    }
    
    std::string extension = path.substr(dotPos);
    
    if (extension == ".html" || extension == ".htm") {
        return "text/html";
    } else if (extension == ".css") {
        return "text/css";
    } else if (extension == ".js") {
        return "application/javascript";
    } else if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    } else if (extension == ".png") {
        return "image/png";
    } else if (extension == ".gif") {
        return "image/gif";
    } else if (extension == ".txt") {
        return "text/plain";
    } else if (extension == ".json") {
        return "application/json";
    } else if (extension == ".xml") {
        return "application/xml";
    }
    
    return "application/octet-stream";
}
