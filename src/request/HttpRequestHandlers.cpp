/**
 * HttpRequestHandlers.cpp - HTTP Method Handlers
 * 
 * This file contains the implementations for:
 * - GET handler
 * - HEAD handler
 * - POST handler (with upload/data routing)
 * - PUT handler
 * - DELETE handler
 */

#include "../../include/HttpRequest.hpp"
#include "../../include/HttpResponse.hpp"
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

// ==================== GET Handler ====================

void HttpRequest::handleGet(ClientConnection* client, const std::string& path) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    const LocationConfig* bestMatch = findBestLocation(path, server);
    
    // Determine root and autoindex settings
    bool autoindex = server.autoindex;
    std::string indexFile = server.index;
    
    if (bestMatch) {
        if (bestMatch->hasAutoindex) {
            autoindex = bestMatch->autoindex;
        }
        if (!bestMatch->index.empty()) {
            indexFile = bestMatch->index;
        }
    }
    
    // Build full file path using buildFilePath (handles location prefix correctly)
    std::string fullPath = buildFilePath(path, server, bestMatch);
    
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
                client->responseBuffer = HttpResponse::buildFileResponse(indexPath, &server);
                return;
            }
            
            // No index file - check autoindex
            if (autoindex) {
                client->responseBuffer = HttpResponse::buildDirectoryListing(fullPath, path);
                return;
            } else {
                client->responseBuffer = HttpResponse::build404(&server);
                return;
            }
        }
    }
    
    // It's a file or doesn't exist - try to serve it
    client->responseBuffer = HttpResponse::buildFileResponse(fullPath, &server);
}

// ==================== HEAD Handler ====================

void HttpRequest::handleHead(ClientConnection* client, const std::string& path) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    const LocationConfig* bestMatch = findBestLocation(path, server);
    
    std::string indexFile = server.index;
    
    if (bestMatch) {
        if (!bestMatch->index.empty()) {
            indexFile = bestMatch->index;
        }
    }
    
    // Build full file path using buildFilePath
    std::string fullPath = buildFilePath(path, server, bestMatch);
    
    // Check if path is a directory
    struct stat pathStat;
    if (stat(fullPath.c_str(), &pathStat) == 0) {
        if (S_ISDIR(pathStat.st_mode)) {
            // Try index file
            std::string indexPath = fullPath;
            if (indexPath[indexPath.length() - 1] != '/') {
                indexPath += "/";
            }
            indexPath += indexFile;
            
            struct stat indexStat;
            if (stat(indexPath.c_str(), &indexStat) == 0 && S_ISREG(indexStat.st_mode)) {
                fullPath = indexPath;
            } else {
                client->responseBuffer = HttpResponse::build404(&server);
                return;
            }
        }
    }
    
    // Build headers-only response
    client->responseBuffer = HttpResponse::buildHeadResponse(fullPath, &server);
}

// ==================== POST Handler ====================

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

void HttpRequest::handlePostUpload(ClientConnection* client, const std::string& path,
                                  const std::string& headers, size_t bodyStart) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    
    size_t contentLength;
    if (!getContentLength(headers, contentLength)) {
        client->responseBuffer = HttpResponse::build411();
        return;
    }
    
    // Check against max body size
    if (contentLength > server.clientMaxBodySize) {
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
    
    // Extract raw body
    std::string rawBody = client->requestBuffer.substr(bodyStart, contentLength);
    
    // Check if this is multipart/form-data and extract actual file content
    std::string extractedFilename;
    std::string fileContent = extractMultipartBody(rawBody, headers, extractedFilename);
    
    // Determine filename: from multipart, from headers, or generate
    std::string filename;
    if (!extractedFilename.empty()) {
        filename = extractedFilename;
    } else {
        filename = extractFilename(headers, path);
    }
    
    // Ensure unique filename to avoid overwriting existing files
    filename = generateUniqueFilename(uploadDir, filename);
    
    // Construct full path
    std::string fullPath = uploadDir;
    if (fullPath[fullPath.length() - 1] != '/') {
        fullPath += "/";
    }
    fullPath += filename;
    
    // Save the extracted file content
    if (!saveUploadedFile(fullPath, fileContent)) {
        client->responseBuffer = HttpResponse::build500("Failed to save uploaded file.", &server);
        return;
    }
    
    // Success response
    std::ostringstream successBody;
    successBody << "<html><body><h1>Upload Successful</h1>"
                << "<p>File uploaded: " << filename << "</p>"
                << "<p>Size: " << fileContent.length() << " bytes</p>"
                << "</body></html>";
    client->responseBuffer = HttpResponse::build201(successBody.str());
}

void HttpRequest::handlePostData(ClientConnection* client, const std::string& /* path */,
                                const std::string& headers, size_t bodyStart) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    
    size_t contentLength;
    if (!getContentLength(headers, contentLength)) {
        client->responseBuffer = HttpResponse::build411(&server);
        return;
    }
    
    // Check against max body size
    if (contentLength > server.clientMaxBodySize) {
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

// ==================== PUT Handler ====================

void HttpRequest::handlePut(ClientConnection* client, const std::string& path,
                            const std::string& headers, size_t bodyStart) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    
    size_t contentLength;
    if (!getContentLength(headers, contentLength)) {
        client->responseBuffer = HttpResponse::build411();
        return;
    }
    
    // Check against max body size
    if (contentLength > server.clientMaxBodySize) {
        client->responseBuffer = HttpResponse::build413();
        return;
    }
    
    // Check if body is complete
    size_t bodyReceived = client->requestBuffer.length() - bodyStart;
    if (bodyReceived < contentLength) {
        std::cout << "PUT body incomplete: " << bodyReceived 
                  << "/" << contentLength << " bytes received" << std::endl;
        return;
    }
    
    // Find upload location
    std::string uploadDir;
    if (!findUploadLocation(path, uploadDir, client->serverIndex)) {
        client->responseBuffer = HttpResponse::build403("PUT not allowed for this location.", &server);
        return;
    }
    
    // Extract the target filename from path
    std::string filename;
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash < path.length() - 1) {
        filename = sanitizeFilename(path.substr(lastSlash + 1));
    } else {
        client->responseBuffer = HttpResponse::build400(&server);
        return;
    }
    
    if (filename.empty()) {
        client->responseBuffer = HttpResponse::build400(&server);
        return;
    }
    
    // Construct full path
    std::string fullPath = uploadDir;
    if (fullPath[fullPath.length() - 1] != '/') {
        fullPath += "/";
    }
    fullPath += filename;
    
    // Check if file already exists (for determining response code)
    struct stat fileStat;
    bool fileExists = (stat(fullPath.c_str(), &fileStat) == 0);
    
    // Extract and save body
    std::string body = client->requestBuffer.substr(bodyStart, contentLength);
    if (!saveUploadedFile(fullPath, body)) {
        client->responseBuffer = HttpResponse::build500("Failed to save file.", &server);
        return;
    }
    
    // Success response: 201 Created if new, 204 No Content if replaced
    if (fileExists) {
        client->responseBuffer = HttpResponse::build204();
    } else {
        std::ostringstream successBody;
        successBody << "<html><body><h1>Created</h1>"
                    << "<p>File created: " << filename << "</p>"
                    << "</body></html>";
        client->responseBuffer = HttpResponse::build201(successBody.str());
    }
}

// ==================== DELETE Handler ====================

void HttpRequest::handleDelete(ClientConnection* client, const std::string& path) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    const LocationConfig* bestMatch = findBestLocation(path, server);
    
    // Build full file path using buildFilePath (handles location prefix correctly)
    std::string filePath = buildFilePath(path, server, bestMatch);
    
    // Check if file exists using stat
    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) != 0) {
        client->responseBuffer = HttpResponse::build404(&server);
        return;
    }
    
    // Check if it's a regular file (not a directory)
    if (!S_ISREG(fileStat.st_mode)) {
        client->responseBuffer = HttpResponse::build405(&server);
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
