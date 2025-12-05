/**
 * HttpRequest.cpp - Main routing and validation
 * 
 * This file contains:
 * - Request dispatching and routing
 * - Request validation
 * - Method implementation checks
 * - Location matching
 * - CGI integration
 */

#include "../../include/HttpRequest.hpp"
#include "../../include/HttpResponse.hpp"
#include "../../include/CgiHandler.hpp"
#include <sstream>
#include <iostream>
#include <sys/stat.h>

// ==================== Constructor/Destructor ====================

HttpRequest::HttpRequest(Config& cfg) : config(cfg), cgiHandler(NULL) {
    cgiHandler = new CgiHandler(config);
}

HttpRequest::~HttpRequest() {
    if (cgiHandler) {
        delete cgiHandler;
        cgiHandler = NULL;
    }
}

CgiHandler* HttpRequest::getCgiHandler() const {
    return cgiHandler;
}

// ==================== Static Utilities ====================

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

// ==================== Validation ====================

bool HttpRequest::isMethodImplemented(const std::string& method) {
    return (method == "GET" || method == "HEAD" || method == "POST" || 
            method == "PUT" || method == "DELETE");
}

bool HttpRequest::validateRequestLine(const std::string& method, const std::string& path,
                                       const std::string& version, ClientConnection* client) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    
    // Validate request line format (must have method, path, and HTTP version)
    if (method.empty() || path.empty() || version.empty()) {
        client->responseBuffer = HttpResponse::build400(&server);
        return false;
    }
    
    // Validate HTTP version format
    if (version.find("HTTP/") != 0) {
        client->responseBuffer = HttpResponse::build400(&server);
        return false;
    }
    
    return true;
}

// ==================== Location Helpers ====================

const LocationConfig* HttpRequest::findBestLocation(const std::string& path, const ServerConfig& server) {
    const LocationConfig* bestMatch = NULL;
    size_t bestMatchLength = 0;
    
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        if (path.find(loc.path) == 0) {
            // Check that match is at a proper path boundary
            // Either: exact match, or followed by '/', or location ends with '/'
            bool validMatch = false;
            if (path.length() == loc.path.length()) {
                // Exact match
                validMatch = true;
            } else if (loc.path[loc.path.length() - 1] == '/') {
                // Location ends with /
                validMatch = true;
            } else if (path.length() > loc.path.length() && 
                       path[loc.path.length()] == '/') {
                // Request path continues with /
                validMatch = true;
            }
            
            if (validMatch && loc.path.length() > bestMatchLength) {
                bestMatchLength = loc.path.length();
                bestMatch = &loc;
            }
        }
    }
    return bestMatch;
}

std::string HttpRequest::getPathRelativeToLocation(const std::string& path, const LocationConfig* location) {
    if (!location || location->path.empty() || location->path == "/") {
        return path;
    }
    
    // Remove the location prefix from the path
    std::string locPath = location->path;
    if (path.find(locPath) == 0) {
        std::string relative = path.substr(locPath.length());
        // If relative path is empty, return "/" for directory
        if (relative.empty()) {
            return "/";
        }
        return relative;
    }
    
    return path;
}

std::string HttpRequest::buildFilePath(const std::string& path, const ServerConfig& server,
                                        const LocationConfig* location) {
    std::string root = server.root;
    if (location && !location->root.empty()) {
        root = location->root;
    }
    
    // Get path relative to location
    std::string relativePath = getPathRelativeToLocation(path, location);
    
    return root + relativePath;
}

bool HttpRequest::isMethodAllowed(const std::string& method, const std::string& path, size_t serverIndex) {
    const ServerConfig& server = config.getServer(serverIndex);
    const LocationConfig* bestMatch = findBestLocation(path, server);
    
    // If no location match, allow by default
    if (!bestMatch) {
        return true;
    }
    
    // Check if method is in allowed list
    for (size_t i = 0; i < bestMatch->allowMethods.size(); ++i) {
        if (bestMatch->allowMethods[i] == method) {
            return true;
        }
    }
    
    return false;
}

bool HttpRequest::checkRedirect(const std::string& path, size_t serverIndex, 
                                std::string& redirectUrl, int& statusCode) {
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

// ==================== Main Request Handler ====================

void HttpRequest::handleRequest(ClientConnection* client) {
    // If client is already processing CGI, don't re-process request
    if (client->state == ClientConnection::CGI_RUNNING) {
        return;
    }
    
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
    
    // Validate request line
    if (!validateRequestLine(method, path, version, client)) {
        return;
    }

    // Enforce Host header for HTTP/1.1 requests (RFC 7230)
    if (version == "HTTP/1.1") {
        std::string headersLower = headers;
        for (size_t i = 0; i < headersLower.size(); ++i) {
            if (headersLower[i] >= 'A' && headersLower[i] <= 'Z')
                headersLower[i] = headersLower[i] + 32;
        }
        if (headersLower.find("host:") == std::string::npos) {
            const ServerConfig& server = config.getServer(client->serverIndex);
            client->responseBuffer = HttpResponse::build400(&server);
            return;
        }
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
    if (!isMethodImplemented(method)) {
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
    
    // For POST/PUT requests, check body size against max limit EARLY
    if (method == "POST" || method == "PUT") {
        const ServerConfig& server = config.getServer(client->serverIndex);
        
        // Find best matching location for body size limit
        const LocationConfig* location = findBestLocation(path, server);
        size_t maxBodySize;
        
        // Use location limit if explicitly configured, otherwise server limit
        // Value of 0 means unlimited
        if (location && location->hasClientMaxBodySize) {
            maxBodySize = location->clientMaxBodySize;
        } else {
            maxBodySize = server.clientMaxBodySize;
        }
        
        // Only check if limit is set (0 = unlimited)
        if (maxBodySize > 0) {
            size_t actualBodySize = 0;
            size_t contentLength;
            
            // Check if this is chunked transfer encoding
            if (isChunkedTransferEncoding(headers)) {
                // For chunked, we need to decode and count
                std::string body = client->requestBuffer.substr(bodyStart);
                std::string decodedBody = unchunkBody(body);
                actualBodySize = decodedBody.length();
                std::cout << "Chunked body decoded: " << actualBodySize << " bytes" << std::endl;
            } else if (getContentLength(headers, contentLength)) {
                actualBodySize = contentLength;
            }
            
            if (actualBodySize > maxBodySize) {
                std::cout << "Body size " << actualBodySize << " exceeds limit " 
                          << maxBodySize << std::endl;
                client->responseBuffer = HttpResponse::build413(&server);
                return;
            }
        }
    }
    
    // Try CGI handling first for GET and POST
    if (method == "GET" || method == "POST") {
        if (handleCgiRequest(client, method, path, headers, bodyStart)) {
            return;  // CGI is handling this request
        }
    }
    
    // Route to appropriate handler (non-CGI)
    if (method == "GET") {
        handleGet(client, path);
    } else if (method == "HEAD") {
        handleHead(client, path);
    } else if (method == "POST") {
        handlePost(client, path, headers, bodyStart);
    } else if (method == "PUT") {
        handlePut(client, path, headers, bodyStart);
    } else if (method == "DELETE") {
        handleDelete(client, path);
    }
}

// ==================== CGI Handler ====================

bool HttpRequest::handleCgiRequest(ClientConnection* client, const std::string& method,
                                   const std::string& path, const std::string& headers,
                                   size_t bodyStart) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    const LocationConfig* location = findBestLocation(path, server);
    
    // Check if this is a CGI request
    if (!cgiHandler->isCgiRequest(path, location)) {
        return false;  // Not a CGI request
    }
    
    std::cout << "CGI request detected for: " << path << std::endl;
    
    // Build the script file path
    std::string scriptPath = buildFilePath(path, server, location);
    
    // Handle query string - need to strip it for file path check
    size_t queryPos = scriptPath.find('?');
    if (queryPos != std::string::npos) {
        scriptPath = scriptPath.substr(0, queryPos);
    }
    
    // Also handle PATH_INFO - find where the extension ends
    std::string extension;
    if (location) {
        for (size_t i = 0; i < location->cgiExt.size(); ++i) {
            size_t extPos = scriptPath.find(location->cgiExt[i]);
            if (extPos != std::string::npos) {
                size_t afterExt = extPos + location->cgiExt[i].length();
                if (afterExt < scriptPath.length() && scriptPath[afterExt] == '/') {
                    // There's PATH_INFO after the script name
                    scriptPath = scriptPath.substr(0, afterExt);
                }
                break;
            }
        }
    }
    
    // Check if script file exists
    struct stat fileStat;
    if (stat(scriptPath.c_str(), &fileStat) != 0) {
        std::cerr << "CGI script not found: " << scriptPath << std::endl;
        client->responseBuffer = HttpResponse::build404(&server);
        return true;  // We handled it (with error)
    }
    
    // For POST, wait for complete body
    std::string body;
    if (method == "POST") {
        size_t contentLength = 0;
        bool hasContentLength = getContentLength(headers, contentLength);
        bool isChunked = isChunkedTransferEncoding(headers);
        
        if (isChunked) {
            // Handle chunked transfer encoding
            // Find end of chunked data (0\r\n\r\n)
            size_t chunkEnd = client->requestBuffer.find("0\r\n\r\n", bodyStart);
            if (chunkEnd == std::string::npos) {
                return true;  // Not complete yet
            }
            
            std::string chunkedBody = client->requestBuffer.substr(bodyStart, 
                chunkEnd + 5 - bodyStart);
            body = unchunkBody(chunkedBody);
        } else if (hasContentLength) {
            size_t bodyReceived = client->requestBuffer.length() - bodyStart;
            if (bodyReceived < contentLength) {
                return true;  // Body not complete yet
            }
            body = client->requestBuffer.substr(bodyStart, contentLength);
        }
        // If no Content-Length and not chunked, body is empty
    }
    
    // Start CGI execution
    if (!cgiHandler->startCgi(client, method, path, headers, body, location, scriptPath)) {
        client->responseBuffer = HttpResponse::build500("CGI execution failed", &server);
        return true;
    }
    
    // CGI started successfully - state is now CGI_RUNNING
    return true;
}

// ==================== Chunked Transfer Encoding ====================

bool HttpRequest::isChunkedTransferEncoding(const std::string& headers) {
    // Look for Transfer-Encoding: chunked (case-insensitive)
    std::string headersLower = headers;
    for (size_t i = 0; i < headersLower.size(); ++i) {
        if (headersLower[i] >= 'A' && headersLower[i] <= 'Z') {
            headersLower[i] = headersLower[i] + 32;
        }
    }
    
    size_t pos = headersLower.find("transfer-encoding:");
    if (pos == std::string::npos) {
        return false;
    }
    
    size_t valueStart = pos + 18;  // Length of "transfer-encoding:"
    size_t lineEnd = headersLower.find("\r\n", valueStart);
    if (lineEnd == std::string::npos) {
        lineEnd = headersLower.length();
    }
    
    std::string value = headersLower.substr(valueStart, lineEnd - valueStart);
    return value.find("chunked") != std::string::npos;
}

std::string HttpRequest::unchunkBody(const std::string& chunkedBody) {
    std::string result;
    size_t pos = 0;
    
    while (pos < chunkedBody.length()) {
        // Find the chunk size line
        size_t lineEnd = chunkedBody.find("\r\n", pos);
        if (lineEnd == std::string::npos) {
            break;
        }
        
        // Parse chunk size (hexadecimal)
        std::string sizeStr = chunkedBody.substr(pos, lineEnd - pos);
        // Remove any chunk extensions (after semicolon)
        size_t semicolon = sizeStr.find(';');
        if (semicolon != std::string::npos) {
            sizeStr = sizeStr.substr(0, semicolon);
        }
        
        char* endPtr;
        unsigned long chunkSize = strtoul(sizeStr.c_str(), &endPtr, 16);
        
        if (chunkSize == 0) {
            break;  // Last chunk
        }
        
        // Move past the size line
        pos = lineEnd + 2;
        
        // Extract chunk data
        if (pos + chunkSize <= chunkedBody.length()) {
            result.append(chunkedBody, pos, chunkSize);
            pos += chunkSize;
        } else {
            break;  // Incomplete chunk
        }
        
        // Skip trailing CRLF after chunk data
        if (pos + 2 <= chunkedBody.length()) {
            pos += 2;
        }
    }
    
    return result;
}
