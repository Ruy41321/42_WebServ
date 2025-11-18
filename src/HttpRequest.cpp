#include "../include/HttpRequest.hpp"
#include "../include/HttpResponse.hpp"
#include <sstream>
#include <cstdlib>
#include <iostream>

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

void HttpRequest::handlePost(ClientConnection* client, const std::string& path, 
                              const std::string& headers, size_t bodyStart) {
    (void)path;  // TODO: Use path for routing
    
    // Look for Content-Length header
    size_t contentLengthPos = headers.find("Content-Length:");
    if (contentLengthPos == std::string::npos) {
        contentLengthPos = headers.find("content-length:");
    }
    
    if (contentLengthPos != std::string::npos) {
        // Extract Content-Length value
        size_t valueStart = headers.find_first_not_of(" \t", contentLengthPos + 15);
        size_t valueEnd = headers.find("\r\n", valueStart);
        std::string lengthStr = headers.substr(valueStart, valueEnd - valueStart);
        size_t contentLength = std::atoi(lengthStr.c_str());
        
        size_t bodyReceived = client->requestBuffer.length() - bodyStart;
        
        if (bodyReceived < contentLength) {
            // Body not complete yet, wait for more data
            std::cout << "POST body incomplete: " << bodyReceived 
                      << "/" << contentLength << " bytes received" << std::endl;
            return;
        }
        
        // Complete POST request received
        std::cout << "POST request complete (" << contentLength << " bytes)" << std::endl;
        
        // TODO: Handle POST request properly (file upload, etc.)
        std::string body = "<html><body><h1>POST received</h1></body></html>";
        client->responseBuffer = HttpResponse::build200("text/html", body);
        return;
    }
    
    // No Content-Length header
    client->responseBuffer = HttpResponse::build411();
}

void HttpRequest::handleDelete(ClientConnection* client, const std::string& path) {
    (void)path;  // TODO: Use path to delete specific resource
    
    // TODO: Implement DELETE functionality
    std::string body = "<html><body><h1>DELETE received</h1></body></html>";
    client->responseBuffer = HttpResponse::build200("text/html", body);
}
