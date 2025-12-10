#include "../../include/HttpRequest.hpp"
#include "../../include/HttpResponse.hpp"
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

void HttpRequest::handleGet(ClientConnection* client, const std::string& path) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    const LocationConfig* bestMatch = findBestLocation(path, server);
    
    bool autoindex = server.autoindex;
    std::string indexFile = server.index;
    
    if (bestMatch) {
        if (bestMatch->hasAutoindex)
            autoindex = bestMatch->autoindex;
        if (!bestMatch->index.empty())
            indexFile = bestMatch->index;
    }
    
    std::string fullPath = buildFilePath(path, server, bestMatch);
    
    struct stat pathStat;
    if (stat(fullPath.c_str(), &pathStat) == 0 && S_ISDIR(pathStat.st_mode)) {
        std::string indexPath = fullPath;
        if (indexPath[indexPath.length() - 1] != '/')
            indexPath += "/";
        indexPath += indexFile;
        
        struct stat indexStat;
        if (stat(indexPath.c_str(), &indexStat) == 0 && S_ISREG(indexStat.st_mode)) {
            client->responseBuffer = HttpResponse::buildFileResponse(indexPath, &server);
            return;
        }
        
        if (autoindex)
            client->responseBuffer = HttpResponse::buildDirectoryListing(fullPath, path);
        else
            client->responseBuffer = HttpResponse::build404(&server);
        return;
    }
    
    client->responseBuffer = HttpResponse::buildFileResponse(fullPath, &server);
}

void HttpRequest::handleHead(ClientConnection* client, const std::string& path) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    const LocationConfig* bestMatch = findBestLocation(path, server);
    
    std::string indexFile = server.index;
    if (bestMatch && !bestMatch->index.empty())
        indexFile = bestMatch->index;
    
    std::string fullPath = buildFilePath(path, server, bestMatch);
    
    struct stat pathStat;
    if (stat(fullPath.c_str(), &pathStat) == 0 && S_ISDIR(pathStat.st_mode)) {
        std::string indexPath = fullPath;
        if (indexPath[indexPath.length() - 1] != '/')
            indexPath += "/";
        indexPath += indexFile;
        
        struct stat indexStat;
        if (stat(indexPath.c_str(), &indexStat) == 0 && S_ISREG(indexStat.st_mode)) {
            fullPath = indexPath;
        } else {
            client->responseBuffer = HttpResponse::build404(&server);
            return;
        }
    }
    
    client->responseBuffer = HttpResponse::buildHeadResponse(fullPath, &server);
}

void HttpRequest::handlePost(ClientConnection* client, const std::string& path, 
                              const std::string& headers, size_t bodyStart) {
    std::string uploadDir;
    if (findUploadLocation(path, uploadDir, client->serverIndex))
        handlePostUpload(client, path, headers, bodyStart);
    else
        client->responseBuffer = HttpResponse::build200("text/html",
            "<html><body><h1>403 Forbidden</h1><p>POST not allowed for this location.</p></body></html>");
}

void HttpRequest::handlePostUpload(ClientConnection* client, const std::string& path,
                                  const std::string& headers, size_t bodyStart) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    
    size_t contentLength;
    if (!getContentLength(headers, contentLength)) {
        client->responseBuffer = HttpResponse::build411();
        return;
    }
    
    size_t bodyReceived = (client->requestBuffer.length() > bodyStart) 
        ? client->requestBuffer.length() - bodyStart 
        : 0;
    
    if (bodyReceived < contentLength) {
        std::cout << "POST body incomplete: " << bodyReceived << "/" << contentLength << " bytes received" << std::endl;
        return;
    }
    
    std::cout << "POST upload request complete (" << contentLength << " bytes)" << std::endl;
    
    std::string uploadDir;
    if (!findUploadLocation(path, uploadDir, client->serverIndex)) {
        client->responseBuffer = HttpResponse::build403("File upload not allowed for this location.", &server);
        return;
    }
    
    struct stat dirStat;
    if (stat(uploadDir.c_str(), &dirStat) != 0 || !S_ISDIR(dirStat.st_mode)) {
        std::cerr << "Upload directory does not exist: " << uploadDir << std::endl;
        client->responseBuffer = HttpResponse::build404(&server);
        return;
    }
    
    std::string rawBody = client->requestBuffer.substr(bodyStart, contentLength);
    std::string extractedFilename;
    std::string fileContent = extractMultipartBody(rawBody, headers, extractedFilename);
    
    std::string filename = extractedFilename.empty() ? extractFilename(headers, path) : extractedFilename;
    filename = generateUniqueFilename(uploadDir, filename);
    
    std::string fullPath = uploadDir;
    if (fullPath[fullPath.length() - 1] != '/')
        fullPath += "/";
    fullPath += filename;
    
    if (!saveUploadedFile(fullPath, fileContent)) {
        client->responseBuffer = HttpResponse::build500("Failed to save uploaded file.", &server);
        return;
    }
    
    std::ostringstream successBody;
    successBody << "<html><body><h1>Upload Successful</h1>"
                << "<p>File uploaded: " << filename << "</p>"
                << "<p>Size: " << fileContent.length() << " bytes</p></body></html>";
    client->responseBuffer = HttpResponse::build201(successBody.str());
}

void HttpRequest::handlePut(ClientConnection* client, const std::string& path,
                            const std::string& headers, size_t bodyStart) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    (void)headers;
    
    std::string uploadDir;
    if (!findUploadLocation(path, uploadDir, client->serverIndex)) {
        client->responseBuffer = HttpResponse::build403("PUT not allowed for this location.", &server);
        return;
    }
    
    std::string filename;
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash < path.length() - 1)
        filename = sanitizeFilename(path.substr(lastSlash + 1));
    
    if (filename.empty()) {
        client->responseBuffer = HttpResponse::build400(&server);
        return;
    }
    
    std::string fullPath = uploadDir;
    if (fullPath[fullPath.length() - 1] != '/')
        fullPath += "/";
    fullPath += filename;
    
    struct stat fileStat;
    bool fileExists = (stat(fullPath.c_str(), &fileStat) == 0);
    
    std::string body = client->requestBuffer.substr(bodyStart, client->bodyBytesReceived);
    if (!saveUploadedFile(fullPath, body)) {
        client->responseBuffer = HttpResponse::build500("Failed to save file.", &server);
        return;
    }
    
    if (fileExists) {
        client->responseBuffer = HttpResponse::build204();
    } else {
        std::ostringstream successBody;
        successBody << "<html><body><h1>Created</h1><p>File created: " << filename << "</p></body></html>";
        client->responseBuffer = HttpResponse::build201(successBody.str());
    }
}

void HttpRequest::handleDelete(ClientConnection* client, const std::string& path) {
    const ServerConfig& server = config.getServer(client->serverIndex);
    const LocationConfig* bestMatch = findBestLocation(path, server);
    
    std::string filePath = buildFilePath(path, server, bestMatch);
    
    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) != 0) {
        client->responseBuffer = HttpResponse::build404(&server);
        return;
    }
    
    if (!S_ISREG(fileStat.st_mode)) {
        client->responseBuffer = HttpResponse::build405(&server);
        return;
    }
    
    if (unlink(filePath.c_str()) != 0) {
        client->responseBuffer = HttpResponse::build500("Failed to delete file.", &server);
        return;
    }
    
    std::ostringstream successBody;
    successBody << "<html><body><h1>Delete Successful</h1><p>File deleted: " << path << "</p></body></html>";
    client->responseBuffer = HttpResponse::build200("text/html", successBody.str());
}
