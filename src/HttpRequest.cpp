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
    
    // Route to appropriate handler
    if (method == "GET") {
        handleGet(client, path);
    } else if (method == "POST") {
        handlePost(client, path, headers, bodyStart);
    } else if (method == "DELETE") {
        handleDelete(client, path);
    } else {
        client->responseBuffer = HttpResponse::build501();
    }
}

void HttpRequest::handleGet(ClientConnection* client, const std::string& path) {
    // Determine the file path
    std::string requestPath = path;
    
    // If path is just "/", use index file
    if (requestPath == "/") {
        requestPath = "/" + config.getIndex();
    }
    
    // Build full file path
    std::string fullPath = config.getRoot() + requestPath;
    
    // Generate response
    client->responseBuffer = HttpResponse::buildFileResponse(fullPath);
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
bool HttpRequest::findUploadLocation(const std::string& path, std::string& uploadDir) {
    const ServerConfig& server = config.getServer(0);
    
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        
        if (path.find(loc.path) == 0) {
            // Check if POST is allowed
            bool postAllowed = false;
            for (size_t j = 0; j < loc.allowMethods.size(); ++j) {
                if (loc.allowMethods[j] == "POST") {
                    postAllowed = true;
                    break;
                }
            }
            
            if (postAllowed && !loc.uploadStore.empty()) {
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
    size_t contentLength;
    if (!getContentLength(headers, contentLength)) {
        client->responseBuffer = HttpResponse::build411();
        return;
    }
    
    // Check against max body size
    if (contentLength > config.getServer(0).clientMaxBodySize) {
        client->responseBuffer = HttpResponse::build413();
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
    if (!findUploadLocation(path, uploadDir)) {
        client->responseBuffer = HttpResponse::build403("File upload not allowed for this location.");
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
        client->responseBuffer = HttpResponse::build500("Failed to save uploaded file.");
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
    // Check if POST is allowed for this location
    const ServerConfig& server = config.getServer(0);
    bool postAllowed = false;
    
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        
        if (path.find(loc.path) == 0) {
            for (size_t j = 0; j < loc.allowMethods.size(); ++j) {
                if (loc.allowMethods[j] == "POST") {
                    postAllowed = true;
                    break;
                }
            }
            break;
        }
    }
    
    if (!postAllowed) {
        client->responseBuffer = HttpResponse::build403("POST method not allowed for this location.");
        return;
    }
    
    size_t contentLength;
    if (!getContentLength(headers, contentLength)) {
        client->responseBuffer = HttpResponse::build411();
        return;
    }
    
    // Check against max body size
    if (contentLength > config.getServer(0).clientMaxBodySize) {
        client->responseBuffer = HttpResponse::build413();
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
    bool hasUploadLocation = findUploadLocation(path, uploadDir);
    
    // Route based on headers and location configuration
    if (hasUploadHeaders || hasUploadLocation) {
        handlePostUpload(client, path, headers, bodyStart);
    } else {
        handlePostData(client, path, headers, bodyStart);
    }
}

void HttpRequest::handleDelete(ClientConnection* client, const std::string& path) {
    // Check if DELETE is allowed for this location
    const ServerConfig& server = config.getServer(0);
    bool deleteAllowed = false;
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
    
    if (bestMatch) {
        // Check if DELETE is in allowed methods
        for (size_t j = 0; j < bestMatch->allowMethods.size(); ++j) {
            if (bestMatch->allowMethods[j] == "DELETE") {
                deleteAllowed = true;
                break;
            }
        }
        
        // Get location root if specified
        if (!bestMatch->root.empty()) {
            locationRoot = bestMatch->root;
        }
    }
    
    if (!deleteAllowed) {
        client->responseBuffer = HttpResponse::build403("DELETE method not allowed for this location.");
        return;
    }
    
    // Build full file path
    std::string filePath;
    if (!locationRoot.empty()) {
        // Use location-specific root
        std::string relativePath = path;
        // Remove location path prefix if present
        size_t prefixPos = path.find("/upload");
        if (prefixPos == 0) {
            relativePath = path.substr(7);  // Remove "/upload"
            if (relativePath.empty()) {
                relativePath = "/";
            }
        }
        filePath = locationRoot + relativePath;
    } else {
        // Use server root
        filePath = config.getRoot() + path;
    }
    
    // Check if file exists using stat
    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) != 0) {
        // File doesn't exist
        client->responseBuffer = HttpResponse::build404();
        return;
    }
    
    // Check if it's a regular file (not a directory)
    if (!S_ISREG(fileStat.st_mode)) {
        client->responseBuffer = HttpResponse::build403("Cannot delete directories.");
        return;
    }
    
    // Attempt to delete the file
    if (unlink(filePath.c_str()) != 0) {
        client->responseBuffer = HttpResponse::build500("Failed to delete file.");
        return;
    }
    
    // Success response
    std::ostringstream successBody;
    successBody << "<html><body><h1>Delete Successful</h1>"
                << "<p>File deleted: " << path << "</p>"
                << "</body></html>";
    client->responseBuffer = HttpResponse::build200("text/html", successBody.str());
}
