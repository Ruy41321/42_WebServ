#include "../../include/HttpRequest.hpp"
#include "../../include/StringUtils.hpp"
#include <sstream>
#include <iostream>
#include <fstream>
#include <ctime>
#include <sys/stat.h>
#include <cstdlib>

bool HttpRequest::getContentLength(const std::string& headers, size_t& contentLength) {
    std::string headersLower = StringUtils::toLower(headers);
    size_t pos = headersLower.find("content-length:");
    
    if (pos == std::string::npos)
        return false;
    
    size_t valueStart = headers.find_first_not_of(" \t", pos + 15);
    size_t valueEnd = headers.find("\r\n", valueStart);
    std::string lengthStr = headers.substr(valueStart, valueEnd - valueStart);
    contentLength = std::atoi(lengthStr.c_str());
    return true;
}

std::string HttpRequest::getContentType(const std::string& headers) {
    std::string headersLower = StringUtils::toLower(headers);
    size_t pos = headersLower.find("content-type:");
    
    if (pos == std::string::npos)
        return "";
    
    size_t valueStart = headers.find_first_not_of(" \t", pos + 13);
    size_t valueEnd = headers.find("\r\n", valueStart);
    return headers.substr(valueStart, valueEnd - valueStart);
}

std::string HttpRequest::getBoundary(const std::string& headers) {
    std::string headersLower = StringUtils::toLower(headers);
    size_t contentTypePos = headersLower.find("content-type:");
    if (contentTypePos == std::string::npos)
        return "";
    
    size_t boundaryPos = headersLower.find("boundary=", contentTypePos);
    if (boundaryPos == std::string::npos)
        return "";
    
    size_t boundaryStart = boundaryPos + 9;
    if (boundaryStart < headers.length() && headers[boundaryStart] == '"')
        boundaryStart++;
    
    size_t boundaryEnd = headers.find_first_of("\"\r\n; ", boundaryStart);
    if (boundaryEnd == std::string::npos)
        boundaryEnd = headers.length();
    
    return headers.substr(boundaryStart, boundaryEnd - boundaryStart);
}

bool HttpRequest::isUploadRequest(const std::string& headers) {
    if (headers.find("Content-Disposition:") != std::string::npos)
        return true;
    
    std::string contentType = getContentType(headers);
    return contentType.find("multipart/form-data") != std::string::npos ||
           contentType.find("application/octet-stream") != std::string::npos;
}

bool HttpRequest::findUploadLocation(const std::string& path, std::string& uploadDir, size_t serverIndex) {
    const ServerConfig& server = config.getServer(serverIndex);
    
    const LocationConfig* bestMatch = NULL;
    size_t bestMatchLength = 0;
    
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        if (path.find(loc.path) == 0 && loc.path.length() > bestMatchLength) {
            bestMatchLength = loc.path.length();
            bestMatch = &loc;
        }
    }
    
    if (!bestMatch)
        return false;
    
    bool uploadAllowed = false;
    for (size_t j = 0; j < bestMatch->allowMethods.size(); ++j) {
        if (bestMatch->allowMethods[j] == "POST" || bestMatch->allowMethods[j] == "PUT") {
            uploadAllowed = true;
            break;
        }
    }
    
    if (uploadAllowed && !bestMatch->uploadStore.empty()) {
        uploadDir = bestMatch->uploadStore;
        return true;
    }
    return false;
}

std::string HttpRequest::extractFilename(const std::string& headers, const std::string& path) {
    size_t dispositionPos = headers.find("Content-Disposition:");
    if (dispositionPos != std::string::npos) {
        size_t filenamePos = headers.find("filename=", dispositionPos);
        if (filenamePos != std::string::npos) {
            size_t nameStart = filenamePos + 9;
            if (headers[nameStart] == '"') nameStart++;
            size_t nameEnd = headers.find_first_of("\"\r\n", nameStart);
            if (nameEnd != std::string::npos)
                return sanitizeFilename(headers.substr(nameStart, nameEnd - nameStart));
        }
    }
    
    std::string extension = ".bin";
    size_t lastSlash = path.find_last_of('/');
    std::string filename = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
    
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos && dotPos > 0)
        extension = filename.substr(dotPos);
    
    std::ostringstream oss;
    oss << "upload_" << time(NULL) << extension;
    return oss.str();
}

std::string HttpRequest::sanitizeFilename(const std::string& filename) {
    std::string result;
    
    size_t lastSlash = filename.find_last_of("/\\");
    std::string baseName = (lastSlash != std::string::npos) ? filename.substr(lastSlash + 1) : filename;
    
    for (size_t i = 0; i < baseName.length(); ++i) {
        char c = baseName[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
            result += c;
    }
    
    while (!result.empty() && result[0] == '.')
        result = result.substr(1);
    
    if (result.empty()) {
        std::ostringstream oss;
        oss << "upload_" << time(NULL) << ".bin";
        result = oss.str();
    }
    return result;
}

std::string HttpRequest::generateUniqueFilename(const std::string& directory, const std::string& filename) {
    std::string fullPath = directory;
    if (!fullPath.empty() && fullPath[fullPath.length() - 1] != '/')
        fullPath += "/";
    fullPath += filename;
    
    struct stat fileStat;
    if (stat(fullPath.c_str(), &fileStat) != 0)
        return filename;
    
    std::string base = filename;
    std::string ext;
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos && dotPos > 0) {
        base = filename.substr(0, dotPos);
        ext = filename.substr(dotPos);
    }
    
    for (int counter = 1; counter < 10000; ++counter) {
        std::ostringstream oss;
        oss << base << "_" << counter << ext;
        std::string newName = oss.str();
        
        fullPath = directory;
        if (!fullPath.empty() && fullPath[fullPath.length() - 1] != '/')
            fullPath += "/";
        fullPath += newName;
        
        if (stat(fullPath.c_str(), &fileStat) != 0)
            return newName;
    }
    
    std::ostringstream oss;
    oss << base << "_" << time(NULL) << ext;
    return oss.str();
}

std::string HttpRequest::extractMultipartBody(const std::string& body, const std::string& headers, 
                                               std::string& extractedFilename) {
    std::string boundary = getBoundary(headers);
    if (boundary.empty())
        return body;
    
    std::string delimiter = "--" + boundary;
    
    size_t partStart = body.find(delimiter);
    if (partStart == std::string::npos)
        return body;
    
    partStart = body.find("\r\n", partStart);
    if (partStart == std::string::npos)
        return body;
    partStart += 2;
    
    size_t headersEnd = body.find("\r\n\r\n", partStart);
    if (headersEnd == std::string::npos)
        return body;
    
    std::string partHeaders = body.substr(partStart, headersEnd - partStart);
    
    size_t filenamePos = partHeaders.find("filename=");
    if (filenamePos != std::string::npos) {
        size_t nameStart = filenamePos + 9;
        if (nameStart < partHeaders.length() && partHeaders[nameStart] == '"')
            nameStart++;
        size_t nameEnd = partHeaders.find_first_of("\"\r\n", nameStart);
        if (nameEnd != std::string::npos)
            extractedFilename = sanitizeFilename(partHeaders.substr(nameStart, nameEnd - nameStart));
    }
    
    size_t contentStart = headersEnd + 4;
    size_t contentEnd = body.find(delimiter, contentStart);
    if (contentEnd == std::string::npos)
        return body;
    
    if (contentEnd >= 2 && body[contentEnd - 2] == '\r' && body[contentEnd - 1] == '\n')
        contentEnd -= 2;
    
    return body.substr(contentStart, contentEnd - contentStart);
}

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
