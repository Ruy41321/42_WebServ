#include "../include/HttpRequest.hpp"
#include "../include/HttpResponse.hpp"
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

HttpRequest::HttpRequest(Config& cfg) : config(cfg) {
}

bool HttpRequest::isRequestComplete(const std::string& buffer) {
    return buffer.find("\r\n\r\n") != std::string::npos;
}

std::string HttpRequest::extractMethod(const std::string& headers) {
    std::istringstream iss(headers);
    std::string method;
    iss >> method;
    return method;
}

std::string HttpRequest::extractPath(const std::string& headers) {
    std::istringstream iss(headers);
    std::string method, path;
    iss >> method >> path;
    return path;
}

void HttpRequest::handleRequest(ClientConnection* client) {
    // Check if headers are complete
    size_t headerEnd = client->requestBuffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return;  // Headers not complete yet
    }
    
    // Extract headers
    std::string headers = client->requestBuffer.substr(0, headerEnd);
    size_t bodyStart = headerEnd + 4;
    
    // Parse request line
    std::istringstream iss(headers);
    std::string method, path, version;
    iss >> method >> path >> version;
    
    std::cout << "Request: " << method << " " << path << " " << version << std::endl;
    
    // Validate request line format (must have method, path, and HTTP version)
    if (method.empty() || path.empty() || version.empty()) {
        const ServerConfig& server = config.getServer(client->serverIndex);
        client->responseBuffer = HttpResponse::build400(&server);
        return;
    }
    
    // Validate HTTP version format
    if (version.find("HTTP/") != 0) {
        const ServerConfig& server = config.getServer(client->serverIndex);
        client->responseBuffer = HttpResponse::build400(&server);
        return;
    }
    
    // Check for redirect first
    std::string redirectUrl;
    int statusCode;
    if (checkRedirect(path, client->serverIndex, redirectUrl, statusCode)) {
        if (statusCode == 301) {
            client->responseBuffer = HttpResponse::build301(redirectUrl);
        } else if (statusCode == 302) {
            client->responseBuffer = HttpResponse::build302(redirectUrl);
        }
        return;
    }
    
    // Check if method is implemented
    if (method != "GET" && method != "HEAD" && method != "POST" && method != "PUT" && method != "DELETE") {
        const ServerConfig& server = config.getServer(client->serverIndex);
        client->responseBuffer = HttpResponse::build501(&server);
        return;
    }
    
    // Check if method is allowed for this location
    if (!isMethodAllowed(method, path, client->serverIndex)) {
        const ServerConfig& server = config.getServer(client->serverIndex);
        client->responseBuffer = HttpResponse::build405(&server);
        return;
    }
    
    // For POST/PUT requests, check Content-Length against max body size EARLY
    if (method == "POST" || method == "PUT") {
        size_t contentLength;
        if (getContentLength(headers, contentLength)) {
            size_t maxBodySize = getMaxBodySize(path, client->serverIndex);
            if (contentLength > maxBodySize) {
                std::cout << "Body size " << contentLength << " exceeds limit " 
                          << maxBodySize << std::endl;
                const ServerConfig& server = config.getServer(client->serverIndex);
                client->responseBuffer = HttpResponse::build413(&server);
                return;
            }
        }
    }
    
    // Route to appropriate handler
    if (method == "GET" || method == "HEAD") {
        // HEAD is treated like GET, but response body is stripped later
        handleGet(client, path);
        
        // For HEAD requests, keep headers but remove body
        if (method == "HEAD") {
            size_t bodyStart = client->responseBuffer.find("\r\n\r\n");
            if (bodyStart != std::string::npos) {
                client->responseBuffer = client->responseBuffer.substr(0, bodyStart + 4);
            }
        }
    } else if (method == "POST") {
        handlePost(client, path, headers, bodyStart);
    } else if (method == "PUT") {
        handlePut(client, path, headers, bodyStart);
    } else if (method == "DELETE") {
        handleDelete(client, path);
    }
}

void HttpRequest::handleGet(ClientConnection* client, const std::string& path) {
    // Get the server config for this client
    const ServerConfig& server = config.getServer(client->serverIndex);
    
    // Find the best matching location for this path
    const LocationConfig* bestMatch = NULL;
    size_t bestMatchLength = 0;
    
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        if (path.find(loc.path) == 0) {
            size_t matchLength = loc.path.length();
            if (matchLength > bestMatchLength) {
                bestMatchLength = matchLength;
                bestMatch = &loc;
            }
        }
    }
    
    // Determine root and autoindex settings
    std::string root = server.root;
    bool autoindex = server.autoindex;
    std::string indexFile = server.index;
    
    if (bestMatch) {
        if (!bestMatch->root.empty()) {
            root = bestMatch->root;
        }
        if (bestMatch->hasAutoindex) {
            autoindex = bestMatch->autoindex;
        }
        if (!bestMatch->index.empty()) {
            indexFile = bestMatch->index;
        }
    }
    
    // Build full file path
    std::string fullPath = root + path;
    
    // Check if path is a directory
    struct stat pathStat;
    if (stat(fullPath.c_str(), &pathStat) == 0) {
        if (S_ISDIR(pathStat.st_mode)) {
            // It's a directory - try index file first
            std::string indexPath = fullPath;
            if (indexPath[indexPath.length() - 1] != '/') {
                indexPath += "/";
            }
            indexPath += indexFile;
            
            // Check if index file exists
            struct stat indexStat;
            if (stat(indexPath.c_str(), &indexStat) == 0 && S_ISREG(indexStat.st_mode)) {
                // Index file exists, serve it
                client->responseBuffer = HttpResponse::buildFileResponse(indexPath, &server);
                return;
            }
            
            // No index file - check autoindex
            if (autoindex) {
                // Generate directory listing
                client->responseBuffer = HttpResponse::buildDirectoryListing(fullPath, path);
                return;
            } else {
                // Autoindex disabled, return 403
                client->responseBuffer = HttpResponse::build403("Directory listing is disabled.", &server);
                return;
            }
        }
    }
    
    // It's a file or doesn't exist - try to serve it
    client->responseBuffer = HttpResponse::buildFileResponse(fullPath, &server);
}

// Helper: Extract Content-Length from headers
bool HttpRequest::getContentLength(const std::string& headers, size_t& contentLength) {
    size_t pos = headers.find("Content-Length:");
    if (pos == std::string::npos) {
        pos = headers.find("content-length:");
    }
    
    if (pos == std::string::npos) {
        return false;
    }
    
    size_t valueStart = headers.find_first_not_of(" \t", pos + 15);
    size_t valueEnd = headers.find("\r\n", valueStart);
    std::string lengthStr = headers.substr(valueStart, valueEnd - valueStart);
    contentLength = std::atoi(lengthStr.c_str());
    return true;
}

// Helper: Check if method is allowed for this path/location
bool HttpRequest::isMethodAllowed(const std::string& method, const std::string& path, size_t serverIndex) {
    const ServerConfig& server = config.getServer(serverIndex);
    const LocationConfig* bestMatch = NULL;
    size_t bestMatchLength = 0;
    
    // Find the most specific location match
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        
        if (path.find(loc.path) == 0) {
            size_t matchLength = loc.path.length();
            if (matchLength > bestMatchLength) {
                bestMatchLength = matchLength;
                bestMatch = &loc;
            }
        }
    }
    
    // If no location match, allow by default
    if (!bestMatch) {
        return true;
    }
    
    // Check if method is in allowed list
    for (size_t i = 0; i < bestMatch->allowMethods.size(); ++i) {
        if (bestMatch->allowMethods[i] == method) {
            return true;
        }
        // HEAD is allowed wherever GET is allowed
        if (method == "HEAD" && bestMatch->allowMethods[i] == "GET") {
            return true;
        }
    }
    
    return false;
}

// Helper: Check for redirect configuration
bool HttpRequest::checkRedirect(const std::string& path, size_t serverIndex, std::string& redirectUrl, int& statusCode) {
    const ServerConfig& server = config.getServer(serverIndex);
    
    // Check each location for redirect
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        
        // Exact path match for redirect
        if (loc.path == path && !loc.redirect.empty()) {
            // Parse redirect string (format: "301 /new-page" or "302 /")
            std::istringstream iss(loc.redirect);
            iss >> statusCode >> redirectUrl;
            return true;
        }
    }
    
    return false;
}

// Helper: Check if request is a file upload based on Content-Type or Content-Disposition
bool HttpRequest::isUploadRequest(const std::string& headers) {
    // Check for Content-Disposition header (strongest indicator)
    if (headers.find("Content-Disposition:") != std::string::npos) {
        return true;
    }
    
    // Check Content-Type
    size_t contentTypePos = headers.find("Content-Type:");
    if (contentTypePos == std::string::npos) {
        contentTypePos = headers.find("content-type:");
    }
    
    if (contentTypePos != std::string::npos) {
        size_t lineEnd = headers.find("\r\n", contentTypePos);
        std::string contentTypeLine = headers.substr(contentTypePos, lineEnd - contentTypePos);
        
        // Check for upload-related content types
        if (contentTypeLine.find("multipart/form-data") != std::string::npos ||
            contentTypeLine.find("application/octet-stream") != std::string::npos) {
            return true;
        }
    }
    
    // If no explicit upload indicators, return false
    // The path-based check is done in handlePost routing
    return false;
}

// Helper: Find upload directory for path
bool HttpRequest::findUploadLocation(const std::string& path, std::string& uploadDir, size_t serverIndex) {
    const ServerConfig& server = config.getServer(serverIndex);
    
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        
        if (path.find(loc.path) == 0) {
            // Check if POST or PUT is allowed
            bool uploadMethodAllowed = false;
            for (size_t j = 0; j < loc.allowMethods.size(); ++j) {
                if (loc.allowMethods[j] == "POST" || loc.allowMethods[j] == "PUT") {
                    uploadMethodAllowed = true;
                    break;
                }
            }
            
            if (uploadMethodAllowed && !loc.uploadStore.empty()) {
                uploadDir = loc.uploadStore;
                return true;
            }
        }
    }
    return false;
}

// Helper: Extract filename from headers
std::string HttpRequest::extractFilename(const std::string& headers) {
    size_t dispositionPos = headers.find("Content-Disposition:");
    if (dispositionPos != std::string::npos) {
        size_t filenamePos = headers.find("filename=", dispositionPos);
        if (filenamePos != std::string::npos) {
            size_t nameStart = filenamePos + 9;
            if (headers[nameStart] == '"') nameStart++;
            size_t nameEnd = headers.find_first_of("\"\r\n", nameStart);
            if (nameEnd != std::string::npos) {
                return headers.substr(nameStart, nameEnd - nameStart);
            }
        }
    }
    
    // Generate filename with timestamp
    std::ostringstream oss;
    oss << "upload_" << time(NULL) << ".bin";
    return oss.str();
}

// Helper: Save file to disk
bool HttpRequest::saveUploadedFile(const std::string& fullPath, const std::string& body) {
    std::cout << "Attempting to save file to: " << fullPath << std::endl;
    
    std::ofstream outFile(fullPath.c_str(), std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open file for writing: " << fullPath << std::endl;
        return false;
    }
    
    std::cout << "File opened successfully, writing " << body.length() << " bytes" << std::endl;
    outFile.write(body.c_str(), body.length());
    outFile.close();
    return true;
}

// Handle POST file upload
void HttpRequest::handlePostUpload(ClientConnection* client, const std::string& path,
                                  const std::string& headers, size_t bodyStart) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    
    size_t contentLength;
    if (!getContentLength(headers, contentLength)) {
        client->responseBuffer = HttpResponse::build411(&server);
        return;
    }
    
    // Check against max body size (location-specific or server default)
    size_t maxBodySize = getMaxBodySize(path, client->serverIndex);
    if (contentLength > maxBodySize) {
        client->responseBuffer = HttpResponse::build413(&server);
        return;
    }
    
    // Check if body is complete
    size_t bodyReceived = client->requestBuffer.length() - bodyStart;
    if (bodyReceived < contentLength) {
        std::cout << "POST body incomplete: " << bodyReceived 
                  << "/" << contentLength << " bytes received" << std::endl;
        return;
    }
    
    std::cout << "POST upload request complete (" << contentLength << " bytes)" << std::endl;
    
    // Find upload location
    std::string uploadDir;
    if (!findUploadLocation(path, uploadDir, client->serverIndex)) {
        client->responseBuffer = HttpResponse::build403("File upload not allowed for this location.", &server);
        return;
    }
    
    // Verify upload directory exists
    struct stat dirStat;
    if (stat(uploadDir.c_str(), &dirStat) != 0 || !S_ISDIR(dirStat.st_mode)) {
        std::cerr << "Upload directory does not exist: " << uploadDir << std::endl;
        client->responseBuffer = HttpResponse::build404(&server);
        return;
    }
    
    // Extract filename and construct full path
    std::string filename = extractFilename(headers);
    std::string fullPath = uploadDir;
    if (fullPath[fullPath.length() - 1] != '/') {
        fullPath += "/";
    }
    fullPath += filename;
    
    // Extract and save body
    std::string body = client->requestBuffer.substr(bodyStart, contentLength);
    if (!saveUploadedFile(fullPath, body)) {
        client->responseBuffer = HttpResponse::build500("Failed to save uploaded file.", &server);
        return;
    }
    
    // Success response
    std::ostringstream successBody;
    successBody << "<html><body><h1>Upload Successful</h1>"
                << "<p>File uploaded: " << filename << "</p>"
                << "<p>Size: " << contentLength << " bytes</p>"
                << "</body></html>";
    client->responseBuffer = HttpResponse::build201(successBody.str());
}

// Handle POST form data (non-upload)
void HttpRequest::handlePostData(ClientConnection* client, const std::string& path,
                                const std::string& headers, size_t bodyStart) {
    // Method allowed check is already done in handleRequest
    const ServerConfig& server = config.getServer(client->serverIndex);
    
    size_t contentLength;
    if (!getContentLength(headers, contentLength)) {
        client->responseBuffer = HttpResponse::build411(&server);
        return;
    }
    
    // Check against max body size (location-specific or server default)
    size_t maxBodySize = getMaxBodySize(path, client->serverIndex);
    if (contentLength > maxBodySize) {
        client->responseBuffer = HttpResponse::build413(&server);
        return;
    }
    
    // Check if body is complete
    size_t bodyReceived = client->requestBuffer.length() - bodyStart;
    if (bodyReceived < contentLength) {
        std::cout << "POST body incomplete: " << bodyReceived 
                  << "/" << contentLength << " bytes received" << std::endl;
        return;
    }
    
    std::cout << "POST data request complete (" << contentLength << " bytes)" << std::endl;
    
    // TODO: Process form data (application/x-www-form-urlencoded, application/json, etc.)
    // For now, return a simple acknowledgment
    std::string body = "<html><body><h1>POST Data Received</h1><p>Data processed successfully.</p></body></html>";
    client->responseBuffer = HttpResponse::build200("text/html", body);
}

// Main POST handler - routes to upload or data handler
void HttpRequest::handlePost(ClientConnection* client, const std::string& path, 
                              const std::string& headers, size_t bodyStart) {
    // Check if this is an upload request based on headers
    bool hasUploadHeaders = isUploadRequest(headers);
    
    // Check if path has upload_store configured
    std::string uploadDir;
    bool hasUploadLocation = findUploadLocation(path, uploadDir, client->serverIndex);
    
    // Route based on headers and location configuration
    if (hasUploadHeaders || hasUploadLocation) {
        handlePostUpload(client, path, headers, bodyStart);
    } else {
        handlePostData(client, path, headers, bodyStart);
    }
}

void HttpRequest::handleDelete(ClientConnection* client, const std::string& path) {
    // Method allowed check is already done in handleRequest
    const ServerConfig& server = config.getServer(client->serverIndex);
    std::string locationRoot;
    size_t bestMatchLength = 0;
    const LocationConfig* bestMatch = NULL;
    
    // Find the most specific location match (longest path match)
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        
        if (path.find(loc.path) == 0) {
            size_t matchLength = loc.path.length();
            if (matchLength > bestMatchLength) {
                bestMatchLength = matchLength;
                bestMatch = &loc;
            }
        }
    }
    
    if (bestMatch && !bestMatch->root.empty()) {
        locationRoot = bestMatch->root;
    }
    
    // Build full file path (same logic as GET)
    std::string root = server.root;
    if (bestMatch && !bestMatch->root.empty()) {
        root = bestMatch->root;
    }
    
    std::string filePath = root + path;
    
    // Check if file exists using stat
    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) != 0) {
        // File doesn't exist
        client->responseBuffer = HttpResponse::build404(&server);
        return;
    }
    
    // Check if it's a regular file (not a directory)
    if (!S_ISREG(fileStat.st_mode)) {
        // It's a directory or special file - forbidden to delete
        client->responseBuffer = HttpResponse::build403("Cannot delete directories or special files.", &server);
        return;
    }
    
    // Attempt to delete the file
    if (unlink(filePath.c_str()) != 0) {
        client->responseBuffer = HttpResponse::build500("Failed to delete file.", &server);
        return;
    }
    
    // Success response
    std::ostringstream successBody;
    successBody << "<html><body><h1>Delete Successful</h1>"
                << "<p>File deleted: " << path << "</p>"
                << "</body></html>";
    client->responseBuffer = HttpResponse::build200("text/html", successBody.str());
}

// Handle PUT request (file upload/creation)
void HttpRequest::handlePut(ClientConnection* client, const std::string& path,
                           const std::string& headers, size_t bodyStart) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    
    // Check Content-Length
    size_t contentLength;
    if (!getContentLength(headers, contentLength)) {
        client->responseBuffer = HttpResponse::build411(&server);
        return;
    }
    
    // Check against max body size
    size_t maxBodySize = getMaxBodySize(path, client->serverIndex);
    if (contentLength > maxBodySize) {
        client->responseBuffer = HttpResponse::build413(&server);
        return;
    }
    
    // Check if body is complete
    size_t bodyReceived = client->requestBuffer.length() - bodyStart;
    if (bodyReceived < contentLength) {
        std::cout << "PUT body incomplete: " << bodyReceived 
                  << "/" << contentLength << " bytes received" << std::endl;
        return;
    }
    
    std::cout << "PUT request complete (" << contentLength << " bytes)" << std::endl;
    
    // Find upload location
    std::string uploadDir;
    if (!findUploadLocation(path, uploadDir, client->serverIndex)) {
        // If no upload_store configured, construct file path from request path
        const LocationConfig* bestMatch = NULL;
        size_t bestMatchLength = 0;
        
        for (size_t i = 0; i < server.locations.size(); ++i) {
            const LocationConfig& loc = server.locations[i];
            if (path.find(loc.path) == 0) {
                size_t matchLength = loc.path.length();
                if (matchLength > bestMatchLength) {
                    bestMatchLength = matchLength;
                    bestMatch = &loc;
                }
            }
        }
        
        std::string root = server.root;
        if (bestMatch && !bestMatch->root.empty()) {
            root = bestMatch->root;
        }
        
        // For PUT, save to the exact path requested
        std::string fullPath = root + path;
        
        // Extract and save body
        std::string body = client->requestBuffer.substr(bodyStart, contentLength);
        if (!saveUploadedFile(fullPath, body)) {
            client->responseBuffer = HttpResponse::build500("Failed to save file.", &server);
            return;
        }
        
        // Success response (201 Created)
        std::ostringstream successBody;
        successBody << "<html><body><h1>Upload Successful</h1>"
                    << "<p>File created: " << path << "</p>"
                    << "<p>Size: " << contentLength << " bytes</p>"
                    << "</body></html>";
        client->responseBuffer = HttpResponse::build201(successBody.str());
        return;
    }
    
    // Upload location found - use upload directory
    std::string filename = extractFilename(headers);
    if (filename.empty()) {
        // No filename in headers, use path's filename
        size_t lastSlash = path.find_last_of('/');
        if (lastSlash != std::string::npos && lastSlash < path.length() - 1) {
            filename = path.substr(lastSlash + 1);
        } else {
            filename = "uploaded_file";
        }
    }
    
    std::string fullPath = uploadDir;
    if (fullPath[fullPath.length() - 1] != '/') {
        fullPath += "/";
    }
    fullPath += filename;
    
    // Extract and save body
    std::string body = client->requestBuffer.substr(bodyStart, contentLength);
    if (!saveUploadedFile(fullPath, body)) {
        client->responseBuffer = HttpResponse::build500("Failed to save file.", &server);
        return;
    }
    
    // Success response (201 Created)
    std::ostringstream successBody;
    successBody << "<html><body><h1>Upload Successful</h1>"
                << "<p>File created: " << filename << "</p>"
                << "<p>Size: " << contentLength << " bytes</p>"
                << "</body></html>";
    client->responseBuffer = HttpResponse::build201(successBody.str());
}

// Helper: Get max body size for a location
size_t HttpRequest::getMaxBodySize(const std::string& path, size_t serverIndex) {
    const ServerConfig& server = config.getServer(serverIndex);
    
    // Find the best matching location
    const LocationConfig* bestMatch = NULL;
    size_t bestMatchLength = 0;
    
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        if (path.find(loc.path) == 0) {
            size_t matchLength = loc.path.length();
            if (matchLength > bestMatchLength) {
                bestMatchLength = matchLength;
                bestMatch = &loc;
            }
        }
    }
    
    // If location has specific max body size, use it
    if (bestMatch && bestMatch->clientMaxBodySize > 0) {
        return bestMatch->clientMaxBodySize;
    }
    
    // Otherwise use server default
    return server.clientMaxBodySize;
}
