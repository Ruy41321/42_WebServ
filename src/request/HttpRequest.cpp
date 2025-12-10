#include "../../include/HttpRequest.hpp"
#include "../../include/HttpResponse.hpp"
#include "../../include/CgiHandler.hpp"
#include "../../include/StringUtils.hpp"
#include <sstream>
#include <iostream>
#include <sys/stat.h>

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

bool HttpRequest::isMethodImplemented(const std::string& method) {
    return method == "GET" || method == "HEAD" || method == "POST" || 
           method == "PUT" || method == "DELETE";
}

bool HttpRequest::validateRequestLine(const std::string& method, const std::string& path,
                                       const std::string& version, ClientConnection* client) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    
    if (method.empty() || path.empty() || version.empty()) {
        client->responseBuffer = HttpResponse::build400(&server);
        return false;
    }
    
    if (version.find("HTTP/") != 0) {
        client->responseBuffer = HttpResponse::build400(&server);
        return false;
    }
    return true;
}

const LocationConfig* HttpRequest::findBestLocation(const std::string& path, const ServerConfig& server) {
    const LocationConfig* bestMatch = NULL;
    size_t bestMatchLength = 0;
    
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        if (path.find(loc.path) == 0) {
            bool validMatch = (path.length() == loc.path.length()) ||
                             (loc.path[loc.path.length() - 1] == '/') ||
                             (path.length() > loc.path.length() && path[loc.path.length()] == '/');
            
            if (validMatch && loc.path.length() > bestMatchLength) {
                bestMatchLength = loc.path.length();
                bestMatch = &loc;
            }
        }
    }
    return bestMatch;
}

std::string HttpRequest::getPathRelativeToLocation(const std::string& path, const LocationConfig* location) {
    if (!location || location->path.empty() || location->path == "/")
        return path;
    
    if (path.find(location->path) == 0) {
        std::string relative = path.substr(location->path.length());
        return relative.empty() ? "/" : relative;
    }
    return path;
}

std::string HttpRequest::buildFilePath(const std::string& path, const ServerConfig& server,
                                        const LocationConfig* location) {
    std::string root = (location && !location->root.empty()) ? location->root : server.root;
    return root + getPathRelativeToLocation(path, location);
}

bool HttpRequest::isMethodAllowed(const std::string& method, const std::string& path, size_t serverIndex) {
    const ServerConfig& server = config.getServer(serverIndex);
    const LocationConfig* bestMatch = findBestLocation(path, server);
    
    if (!bestMatch)
        return true;
    
    for (size_t i = 0; i < bestMatch->allowMethods.size(); ++i) {
        if (bestMatch->allowMethods[i] == method)
            return true;
    }
    return false;
}

bool HttpRequest::checkRedirect(const std::string& path, size_t serverIndex, 
                                std::string& redirectUrl, int& statusCode) {
    const ServerConfig& server = config.getServer(serverIndex);
    
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        if (loc.path == path && !loc.redirect.empty()) {
            std::istringstream iss(loc.redirect);
            iss >> statusCode >> redirectUrl;
            return true;
        }
    }
    return false;
}

bool HttpRequest::checkHostHeader(const std::string& headers, const std::string& version) {
    if (version != "HTTP/1.1")
        return true;
    
    std::string headersLower = StringUtils::toLower(headers);
    return headersLower.find("host:") != std::string::npos;
}

void HttpRequest::handleRequest(ClientConnection* client) {
    if (client->state == ClientConnection::CGI_RUNNING)
        return;
    
    size_t headerEnd = client->requestBuffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return;
    
    std::string headers = client->requestBuffer.substr(0, headerEnd);
    size_t bodyStart = headerEnd + 4;
    
    std::istringstream iss(headers);
    std::string method, path, version;
    iss >> method >> path >> version;
    
    std::cout << "Request: " << method << " " << path << " " << version << std::endl;
    
    if (!validateRequestLine(method, path, version, client))
        return;
    
    if (!checkHostHeader(headers, version)) {
        const ServerConfig& server = config.getServer(client->serverIndex);
        client->responseBuffer = HttpResponse::build400(&server);
        return;
    }
    
    std::string redirectUrl;
    int statusCode;
    if (checkRedirect(path, client->serverIndex, redirectUrl, statusCode)) {
        client->responseBuffer = (statusCode == 301) 
            ? HttpResponse::build301(redirectUrl) 
            : HttpResponse::build302(redirectUrl);
        return;
    }
    
    if (!isMethodImplemented(method)) {
        const ServerConfig& server = config.getServer(client->serverIndex);
        client->responseBuffer = HttpResponse::build501(&server);
        return;
    }
    
    if (!isMethodAllowed(method, path, client->serverIndex)) {
        const ServerConfig& server = config.getServer(client->serverIndex);
        client->responseBuffer = HttpResponse::build405(&server);
        return;
    }
    
    if ((method == "POST" || method == "PUT") && !checkBodySizeLimit(client, method, path, headers, bodyStart))
        return;
    
    if ((method == "GET" || method == "POST") && handleCgiRequest(client, method, path, headers, bodyStart))
        return;
    
    if (method == "GET") handleGet(client, path);
    else if (method == "HEAD") handleHead(client, path);
    else if (method == "POST") handlePost(client, path, headers, bodyStart);
    else if (method == "PUT") handlePut(client, path, headers, bodyStart);
    else if (method == "DELETE") handleDelete(client, path);
}

bool HttpRequest::checkBodySizeLimit(ClientConnection* client, const std::string& method,
                                     const std::string& path, const std::string& headers, size_t bodyStart) {
    (void)method;
    const ServerConfig& server = config.getServer(client->serverIndex);
    const LocationConfig* location = findBestLocation(path, server);
    
    size_t maxBodySize = (location && location->hasClientMaxBodySize) 
        ? location->clientMaxBodySize 
        : server.clientMaxBodySize;
    
    if (maxBodySize == 0)
        return true;
    
    size_t actualBodySize = 0;
    size_t contentLength;
    
    if (isChunkedTransferEncoding(headers)) {
        std::string body = client->requestBuffer.substr(bodyStart);
        actualBodySize = unchunkBody(body).length();
    } else if (getContentLength(headers, contentLength)) {
        actualBodySize = contentLength;
    }
    
    if (actualBodySize > maxBodySize) {
        std::cout << "Body size " << actualBodySize << " exceeds limit " << maxBodySize << std::endl;
        client->responseBuffer = HttpResponse::build413(&server);
        return false;
    }
    return true;
}

bool HttpRequest::handleCgiRequest(ClientConnection* client, const std::string& method,
                                   const std::string& path, const std::string& headers,
                                   size_t bodyStart) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    const LocationConfig* location = findBestLocation(path, server);
    
    if (!cgiHandler->isCgiRequest(path, location))
        return false;
    
    std::cout << "CGI request detected for: " << path << std::endl;
    
    std::string scriptPath = buildFilePath(path, server, location);
    
    size_t queryPos = scriptPath.find('?');
    if (queryPos != std::string::npos)
        scriptPath = scriptPath.substr(0, queryPos);
    
    if (location) {
        for (size_t i = 0; i < location->cgiExt.size(); ++i) {
            size_t extPos = scriptPath.find(location->cgiExt[i]);
            if (extPos != std::string::npos) {
                size_t afterExt = extPos + location->cgiExt[i].length();
                if (afterExt < scriptPath.length() && scriptPath[afterExt] == '/')
                    scriptPath = scriptPath.substr(0, afterExt);
                break;
            }
        }
    }
    
    struct stat fileStat;
    if (stat(scriptPath.c_str(), &fileStat) != 0) {
        std::cerr << "CGI script not found: " << scriptPath << std::endl;
        client->responseBuffer = HttpResponse::build404(&server);
        return true;
    }
    
    std::string body;
    if (method == "POST") {
        body = extractCgiBody(client, headers, bodyStart);
        if (body.empty() && bodyStart < client->requestBuffer.length())
            return true;
    }
    
    if (!cgiHandler->startCgi(client, method, path, headers, body, location, scriptPath)) {
        client->responseBuffer = HttpResponse::build500("CGI execution failed", &server);
        return true;
    }
    return true;
}

std::string HttpRequest::extractCgiBody(ClientConnection* client, const std::string& headers, size_t bodyStart) {
    size_t contentLength = 0;
    bool hasContentLength = getContentLength(headers, contentLength);
    bool isChunked = isChunkedTransferEncoding(headers);
    
    if (isChunked) {
        size_t chunkEnd = client->requestBuffer.find("0\r\n\r\n", bodyStart);
        if (chunkEnd == std::string::npos)
            return "";
        return unchunkBody(client->requestBuffer.substr(bodyStart, chunkEnd + 5 - bodyStart));
    }
    
    if (hasContentLength) {
        size_t bodyReceived = client->requestBuffer.length() - bodyStart;
        if (bodyReceived < contentLength)
            return "";
        return client->requestBuffer.substr(bodyStart, contentLength);
    }
    return "";
}

bool HttpRequest::isChunkedTransferEncoding(const std::string& headers) {
    std::string headersLower = StringUtils::toLower(headers);
    size_t pos = headersLower.find("transfer-encoding:");
    if (pos == std::string::npos)
        return false;
    
    size_t lineEnd = headersLower.find("\r\n", pos + 18);
    if (lineEnd == std::string::npos)
        lineEnd = headersLower.length();
    
    return headersLower.substr(pos + 18, lineEnd - pos - 18).find("chunked") != std::string::npos;
}

std::string HttpRequest::unchunkBody(const std::string& chunkedBody) {
    std::string result;
    size_t pos = 0;
    
    while (pos < chunkedBody.length()) {
        size_t lineEnd = chunkedBody.find("\r\n", pos);
        if (lineEnd == std::string::npos)
            break;
        
        std::string sizeStr = chunkedBody.substr(pos, lineEnd - pos);
        size_t semicolon = sizeStr.find(';');
        if (semicolon != std::string::npos)
            sizeStr = sizeStr.substr(0, semicolon);
        
        char* endPtr;
        unsigned long chunkSize = strtoul(sizeStr.c_str(), &endPtr, 16);
        
        if (chunkSize == 0)
            break;
        
        pos = lineEnd + 2;
        
        if (pos + chunkSize <= chunkedBody.length()) {
            result.append(chunkedBody, pos, chunkSize);
            pos += chunkSize;
        } else {
            break;
        }
        
        if (pos + 2 <= chunkedBody.length())
            pos += 2;
    }
    return result;
}
