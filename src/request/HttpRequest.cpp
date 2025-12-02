/**
 * HttpRequest.cpp - Main routing and validation
 * 
 * This file contains:
 * - Request dispatching and routing
 * - Request validation
 * - Method implementation checks
 * - Location matching
 */

#include "../../include/HttpRequest.hpp"
#include "../../include/HttpResponse.hpp"
#include <sstream>
#include <iostream>
#include <sys/stat.h>

// ==================== Constructor ====================

HttpRequest::HttpRequest(Config& cfg) : config(cfg) {
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
            size_t matchLength = loc.path.length();
            if (matchLength > bestMatchLength) {
                bestMatchLength = matchLength;
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
    
    // For POST/PUT requests, check Content-Length against max body size EARLY
    if (method == "POST" || method == "PUT") {
        size_t contentLength;
        if (getContentLength(headers, contentLength)) {
            const ServerConfig& server = config.getServer(client->serverIndex);
            if (contentLength > server.clientMaxBodySize) {
                std::cout << "Body size " << contentLength << " exceeds limit " 
                          << server.clientMaxBodySize << std::endl;
                client->responseBuffer = HttpResponse::build413(&server);
                return;
            }
        }
    }
    
    // Route to appropriate handler
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
