#include "../include/WebServer.hpp"
#include "../include/HttpResponse.hpp"
#include "../include/StringUtils.hpp"
#include <sstream>
#include <cctype>

WebServer::WebServer() : epollFd(-1), running(false), connManager(NULL) {}

WebServer::~WebServer() {
    stop();
    for (size_t i = 0; i < httpHandlers.size(); ++i) {
        if (httpHandlers[i])
            delete httpHandlers[i];
    }
    httpHandlers.clear();
    
    if (connManager) {
        delete connManager;
        connManager = NULL;
    }
}

bool WebServer::initialize(const std::string& configFile) {
    if (!config.loadFromFile(configFile))
        return false;
    
    if (config.getServerCount() == 0) {
        std::cerr << "Error: No server blocks defined in configuration" << std::endl;
        return false;
    }
    
    try {
        setupEpoll();
        
        for (size_t i = 0; i < config.getServerCount(); ++i) {
            const ServerConfig& serverConfig = config.getServer(i);
            
            if (isDuplicateBinding(serverConfig.host, serverConfig.port)) {
                std::cerr << "Error: Duplicate server binding for " 
                          << serverConfig.host << ":" << serverConfig.port << std::endl;
                throw std::runtime_error("Duplicate server binding");
            }
            
            if (!setupServerSocket(serverConfig, i))
                throw std::runtime_error("Failed to setup server socket");
            
            httpHandlers.push_back(new HttpRequest(config));
        }
        
        connManager = new ConnectionManager(epollFd);
        std::cout << "Initialized " << serverSockets.size() << " server(s)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error initializing server: " << e.what() << std::endl;
        cleanupOnError();
        return false;
    }
    return true;
}

void WebServer::cleanupOnError() {
    for (size_t i = 0; i < httpHandlers.size(); ++i)
        delete httpHandlers[i];
    httpHandlers.clear();
    
    for (size_t i = 0; i < serverSockets.size(); ++i) {
        if (serverSockets[i].fd >= 0)
            close(serverSockets[i].fd);
    }
    serverSockets.clear();
    
    if (epollFd >= 0) {
        close(epollFd);
        epollFd = -1;
    }
}

bool WebServer::isDuplicateBinding(const std::string& host, int port) const {
    for (size_t i = 0; i < serverSockets.size(); ++i) {
        if (serverSockets[i].host == host && serverSockets[i].port == port)
            return true;
    }
    return false;
}

bool WebServer::setupServerSocket(const ServerConfig& serverConfig, size_t index) {
    int sockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFd < 0) {
        std::cerr << "Failed to create socket for " << serverConfig.host 
                  << ":" << serverConfig.port << std::endl;
        return false;
    }
    
    if (!setNonBlocking(sockFd)) {
        close(sockFd);
        return false;
    }
    
    int opt = 1;
    if (setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set socket options" << std::endl;
        close(sockFd);
        return false;
    }
    
    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverConfig.port);
    if (inet_pton(AF_INET, serverConfig.host.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid address: " << serverConfig.host << std::endl;
        close(sockFd);
        return false;
    }
    
    if (bind(sockFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Failed to bind " << serverConfig.host << ":" 
                  << serverConfig.port << " - " << strerror(errno) << std::endl;
        close(sockFd);
        return false;
    }
    
    if (listen(sockFd, 128) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(sockFd);
        return false;
    }
    
    if (!addToEpoll(sockFd, EPOLLIN)) {
        close(sockFd);
        return false;
    }
    
    ServerSocket serverSock;
    serverSock.fd = sockFd;
    serverSock.host = serverConfig.host;
    serverSock.port = serverConfig.port;
    serverSock.serverIndex = index;
    
    serverSockets.push_back(serverSock);
    fdToServerIndex[sockFd] = index;
    
    std::cout << "Server listening on " << serverConfig.host 
              << ":" << serverConfig.port << std::endl;
    return true;
}

bool WebServer::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "Failed to set socket to non-blocking" << std::endl;
        return false;
    }
    return true;
}

bool WebServer::addToEpoll(int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        std::cerr << "Failed to add fd to epoll: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

void WebServer::setupEpoll() {
    epollFd = epoll_create(1);
    if (epollFd < 0)
        throw std::runtime_error("Failed to create epoll instance");
    
    int flags = fcntl(epollFd, F_GETFD);
    if (flags >= 0)
        fcntl(epollFd, F_SETFD, flags | FD_CLOEXEC);
}

void WebServer::run() {
    running = true;
    const int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];
    
    std::cout << "Server running with epoll..." << std::endl;
    
    while (running) {
        int numEvents = epoll_wait(epollFd, events, MAX_EVENTS, 1000);
        
        if (numEvents < 0) {
            if (errno == EINTR)
                continue;
            std::cerr << "Error in epoll_wait: " << strerror(errno) << std::endl;
            break;
        }
        
        checkCgiTimeouts();
        
        if (numEvents == 0)
            continue;
        
        processEvents(events, numEvents);
    }
    std::cout << "Server stopped." << std::endl;
}

void WebServer::processEvents(struct epoll_event* events, int numEvents) {
    for (int i = 0; i < numEvents; ++i) {
        int fd = events[i].data.fd;
        uint32_t activeEvents = events[i].events;
        
        if (connManager->isCgiPipe(fd)) {
            handleCgiPipeEvent(fd, activeEvents);
            continue;
        }
        
        if (activeEvents & (EPOLLERR | EPOLLHUP)) {
            handleErrorEvent(fd);
            continue;
        }
        
        if (isServerSocket(fd)) {
            handleNewConnection(fd);
            continue;
        }
        
        handleClientEvent(fd, activeEvents);
    }
}

bool WebServer::isServerSocket(int fd) {
    for (size_t j = 0; j < serverSockets.size(); ++j) {
        if (fd == serverSockets[j].fd)
            return true;
    }
    return false;
}

void WebServer::handleCgiPipeEvent(int fd, uint32_t activeEvents) {
    if (activeEvents & (EPOLLERR | EPOLLHUP)) {
        ClientConnection* client = connManager->findClientByCgiPipe(fd);
        if (client) {
            completeCgiRequest(client, fd);
        }
        return;
    }
    
    if (activeEvents & EPOLLIN)
        handleCgiPipeRead(fd);
    
    if (activeEvents & EPOLLOUT)
        handleCgiPipeWrite(fd);
}

void WebServer::completeCgiRequest(ClientConnection* client, int fd) {
    if (client->serverIndex < httpHandlers.size()) {
        CgiHandler* cgiHandler = httpHandlers[client->serverIndex]->getCgiHandler();
        if (cgiHandler) {
            if (fd == client->cgiOutputFd)
                cgiHandler->readFromCgi(client);
            cgiHandler->checkCgiComplete(client);
            cgiHandler->buildResponse(client);
            cgiHandler->cleanup(client);
        }
    }
    connManager->removeCgiPipes(client);
    client->state = ClientConnection::SENDING_RESPONSE;
    connManager->prepareResponseMode(client);
}

void WebServer::handleErrorEvent(int fd) {
    std::cerr << "Error/Hangup on FD " << fd << std::endl;
    if (!isServerSocket(fd))
        connManager->removeClient(fd);
}

void WebServer::handleClientEvent(int fd, uint32_t activeEvents) {
    if (activeEvents & EPOLLRDHUP) {
        std::cout << "Client " << fd << " disconnected" << std::endl;
        connManager->removeClient(fd);
        return;
    }
    
    if (activeEvents & EPOLLIN)
        handleClientRead(fd);
    
    if (activeEvents & EPOLLOUT)
        handleClientWrite(fd);
}

void WebServer::handleNewConnection(int serverFd) {
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    
    int clientSocket = accept(serverFd, (struct sockaddr*)&clientAddr, &clientLen);
    if (clientSocket < 0) {
        std::cerr << "Error accepting connection: " << strerror(errno) << std::endl;
        return;
    }
    
    if (!setNonBlocking(clientSocket)) {
        close(clientSocket);
        return;
    }
    
    int flags = fcntl(clientSocket, F_GETFD);
    if (flags >= 0)
        fcntl(clientSocket, F_SETFD, flags | FD_CLOEXEC);
    
    if (!addToEpoll(clientSocket, EPOLLIN | EPOLLRDHUP)) {
        close(clientSocket);
        return;
    }
    
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
    
    size_t serverIndex = 0;
    if (fdToServerIndex.find(serverFd) != fdToServerIndex.end())
        serverIndex = fdToServerIndex[serverFd];
    
    connManager->addClient(clientSocket, serverIndex);
    
    const ServerConfig& serverConfig = config.getServer(serverIndex);
    std::cout << "New connection from " << clientIP 
              << ":" << ntohs(clientAddr.sin_port) 
              << " on socket " << clientSocket 
              << " (server: " << serverConfig.host << ":" << serverConfig.port << ")"
              << std::endl;
}

void WebServer::handleClientRead(int clientSocket) {
    ClientConnection* client = connManager->findClient(clientSocket);
    if (!client) {
        std::cerr << "Client not found: " << clientSocket << std::endl;
        return;
    }
    
    if (client->state == ClientConnection::CGI_RUNNING)
        return;
    
    char buffer[1000000];
    ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
    
    if (bytesRead < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        std::cerr << "recv error on fd=" << clientSocket << ": " << strerror(errno) << std::endl;
        connManager->removeClient(clientSocket);
        return;
    }
    
    if (bytesRead == 0) {
        std::cout << "Client " << clientSocket << " closed connection" << std::endl;
        connManager->removeClient(clientSocket);
        return;
    }
    
    size_t oldBufferSize = client->requestBuffer.size();
    client->requestBuffer.append(buffer, bytesRead);
    
    if (!client->headersComplete) {
        if (!parseHeaders(client, oldBufferSize))
            return;
    } else {
        client->bodyBytesReceived += bytesRead;
    }
    
    if (!checkBodySize(client))
        return;
    
    if (!waitForCompleteBody(client))
        return;
    
    processRequest(client);
}

bool WebServer::parseHeaders(ClientConnection* client, size_t oldBufferSize) {
    size_t searchStart = (oldBufferSize > 3) ? (oldBufferSize - 3) : 0;
    size_t headerEnd = client->requestBuffer.find("\r\n\r\n", searchStart);
    
    if (headerEnd == std::string::npos)
        return false;
    
    client->headersComplete = true;
    client->headerEndOffset = headerEnd + 4;
    
    if (client->requestBuffer.length() > client->headerEndOffset)
        client->bodyBytesReceived = client->requestBuffer.length() - client->headerEndOffset;
    
    determineMaxBodySize(client);
    
    if (!checkContentLengthHeader(client))
        return false;
    
    return true;
}

void WebServer::determineMaxBodySize(ClientConnection* client) {
    if (client->serverIndex >= config.getServerCount())
        return;
    
    const ServerConfig& server = config.getServer(client->serverIndex);
    std::string requestPath = extractRequestPath(client);
    
    size_t bestMatchLen = 0;
    size_t locationMaxBodySize = 0;
    bool locationHasBodySizeLimit = false;
    
    for (size_t i = 0; i < server.locations.size(); ++i) {
        const LocationConfig& loc = server.locations[i];
        if (requestPath.compare(0, loc.path.length(), loc.path) == 0) {
            if (isValidPathMatch(requestPath, loc.path) && loc.path.length() > bestMatchLen) {
                bestMatchLen = loc.path.length();
                if (loc.hasClientMaxBodySize) {
                    locationMaxBodySize = loc.clientMaxBodySize;
                    locationHasBodySizeLimit = true;
                } else {
                    locationHasBodySizeLimit = false;
                }
            }
        }
    }
    
    client->maxBodySize = locationHasBodySizeLimit ? locationMaxBodySize : server.clientMaxBodySize;
}

std::string WebServer::extractRequestPath(ClientConnection* client) {
    std::string requestPath;
    size_t firstSpace = client->requestBuffer.find(' ');
    if (firstSpace != std::string::npos) {
        size_t secondSpace = client->requestBuffer.find(' ', firstSpace + 1);
        if (secondSpace != std::string::npos) {
            requestPath = client->requestBuffer.substr(firstSpace + 1, secondSpace - firstSpace - 1);
            size_t queryPos = requestPath.find('?');
            if (queryPos != std::string::npos)
                requestPath = requestPath.substr(0, queryPos);
        }
    }
    return requestPath;
}

bool WebServer::isValidPathMatch(const std::string& requestPath, const std::string& locPath) {
    if (requestPath.length() == locPath.length())
        return true;
    if (locPath[locPath.length() - 1] == '/')
        return true;
    if (requestPath.length() > locPath.length() && requestPath[locPath.length()] == '/')
        return true;
    return false;
}

bool WebServer::checkContentLengthHeader(ClientConnection* client) {
    if (client->maxBodySize == 0)
        return true;
    
    std::string headers = client->requestBuffer.substr(0, client->headerEndOffset);
    std::string headersLower = StringUtils::toLower(headers);
    
    size_t pos = headersLower.find("content-length:");
    if (pos != std::string::npos) {
        size_t valueStart = headers.find_first_not_of(" \t", pos + 15);
        size_t valueEnd = headers.find("\r\n", valueStart);
        if (valueStart != std::string::npos && valueEnd != std::string::npos) {
            std::string lengthStr = headers.substr(valueStart, valueEnd - valueStart);
            size_t declaredLength = std::atoi(lengthStr.c_str());
            if (declaredLength > client->maxBodySize) {
                std::cout << "Content-Length " << declaredLength 
                          << " exceeds limit " << client->maxBodySize 
                          << " (early rejection)" << std::endl;
                const ServerConfig& server = config.getServer(client->serverIndex);
                client->responseBuffer = HttpResponse::build413(&server);
                client->state = ClientConnection::SENDING_RESPONSE;
                connManager->prepareResponseMode(client);
                return false;
            }
        }
    }
    return true;
}

bool WebServer::checkBodySize(ClientConnection* client) {
    if (!client->headersComplete || client->maxBodySize == 0)
        return true;
    
    std::string headers = client->requestBuffer.substr(0, client->headerEndOffset);
    std::string headersLower = StringUtils::toLower(headers);
    bool isChunked = headersLower.find("transfer-encoding: chunked") != std::string::npos;
    
    if (!isChunked && client->bodyBytesReceived > client->maxBodySize) {
        std::cout << "Body size " << client->bodyBytesReceived 
                  << " exceeds limit " << client->maxBodySize 
                  << " during reading (progressive check)" << std::endl;
        
        const ServerConfig* serverConfig = NULL;
        if (client->serverIndex < config.getServerCount())
            serverConfig = &config.getServer(client->serverIndex);
        
        client->responseBuffer = HttpResponse::build413(serverConfig);
        client->state = ClientConnection::SENDING_RESPONSE;
        connManager->prepareResponseMode(client);
        return false;
    }
    return true;
}

bool WebServer::waitForCompleteBody(ClientConnection* client) {
    if (!client->headersComplete)
        return false;
    
    std::string headers = client->requestBuffer.substr(0, client->headerEndOffset);
    std::string method = extractMethod(headers);
    
    if (method != "POST" && method != "PUT")
        return true;
    
    std::string headersLower = StringUtils::toLower(headers);
    bool isChunked = headersLower.find("transfer-encoding: chunked") != std::string::npos;
    
    if (isChunked) {
        std::string body = client->requestBuffer.substr(client->headerEndOffset);
        return body.find("0\r\n\r\n") != std::string::npos;
    }
    
    size_t pos = headersLower.find("content-length:");
    if (pos == std::string::npos) {
        std::cout << "Rejecting POST/PUT without Content-Length (not chunked)" << std::endl;
        client->responseBuffer = HttpResponse::build411();
        client->state = ClientConnection::SENDING_RESPONSE;
        connManager->prepareResponseMode(client);
        return false;
    }
    
    size_t valueStart = headers.find_first_not_of(" \t", pos + 15);
    size_t valueEnd = headers.find("\r\n", valueStart);
    std::string lengthStr = headers.substr(valueStart, valueEnd - valueStart);
    size_t contentLength = std::atoi(lengthStr.c_str());
    
    return contentLength == 0 || client->bodyBytesReceived >= contentLength;
}

std::string WebServer::extractMethod(const std::string& headers) {
    size_t firstSpace = headers.find(' ');
    return (firstSpace != std::string::npos) ? headers.substr(0, firstSpace) : "";
}

void WebServer::processRequest(ClientConnection* client) {
    if (client->serverIndex < httpHandlers.size())
        httpHandlers[client->serverIndex]->handleRequest(client);
    
    if (client->state == ClientConnection::CGI_RUNNING) {
        connManager->addCgiPipes(client);
        return;
    }
    
    if (!client->responseBuffer.empty()) {
        client->state = ClientConnection::SENDING_RESPONSE;
        connManager->prepareResponseMode(client);
    }
}

bool WebServer::shouldKeepAlive(ClientConnection* client) {
    std::string reqHeaders;
    if (client->headerEndOffset > 0 && client->requestBuffer.length() >= client->headerEndOffset)
        reqHeaders = client->requestBuffer.substr(0, client->headerEndOffset);
    
    std::string version;
    size_t firstLineEnd = reqHeaders.find("\r\n");
    if (firstLineEnd != std::string::npos) {
        std::string firstLine = reqHeaders.substr(0, firstLineEnd);
        std::istringstream fls(firstLine);
        std::string method, path;
        fls >> method >> path >> version;
    }
    
    std::string headersLower = StringUtils::toLower(reqHeaders);
    bool hasConnClose = headersLower.find("connection: close") != std::string::npos;
    bool hasConnKeepAlive = headersLower.find("connection: keep-alive") != std::string::npos;
    
    if (version == "HTTP/1.1")
        return !hasConnClose;
    if (version == "HTTP/1.0")
        return hasConnKeepAlive;
    return false;
}

void WebServer::prepareForNextRequest(ClientConnection* client, int clientSocket) {
    client->clearBuffers();
    client->state = ClientConnection::READING_REQUEST;
    
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP;
    ev.data.fd = clientSocket;
    if (epoll_ctl(epollFd, EPOLL_CTL_MOD, clientSocket, &ev) < 0)
        connManager->removeClient(clientSocket);
}

void WebServer::handleClientWrite(int clientSocket) {
    ClientConnection* client = connManager->findClient(clientSocket);
    if (!client) {
        std::cerr << "Client not found: " << clientSocket << std::endl;
        return;
    }
    
    if (client->isResponseComplete()) {
        if (shouldKeepAlive(client))
            prepareForNextRequest(client, clientSocket);
        else
            connManager->removeClient(clientSocket);
        return;
    }
    
    size_t remaining = client->getRemainingBytes();
    ssize_t sent = send(clientSocket, client->responseBuffer.c_str() + client->bytesSent, remaining, 0);
    
    if (sent < 0) {
        connManager->removeClient(clientSocket);
        return;
    }
    
    client->bytesSent += sent;
    
    if (client->isResponseComplete()) {
        std::string statusLine;
        size_t endOfLine = client->responseBuffer.find("\r\n");
        if (endOfLine != std::string::npos)
            statusLine = client->responseBuffer.substr(0, endOfLine);
        
        std::cout << "Response sent to socket " << clientSocket 
                  << " [" << statusLine << "]" << std::endl;
        
        if (shouldKeepAlive(client))
            prepareForNextRequest(client, clientSocket);
        else
            connManager->removeClient(clientSocket);
    }
}

void WebServer::stop() {
    if (!running)
        return;
    running = false;
    
    for (size_t i = 0; i < serverSockets.size(); ++i) {
        if (serverSockets[i].fd >= 0) {
            epoll_ctl(epollFd, EPOLL_CTL_DEL, serverSockets[i].fd, NULL);
            close(serverSockets[i].fd);
            std::cout << "Server socket closed: " << serverSockets[i].host 
                      << ":" << serverSockets[i].port << std::endl;
        }
    }
    serverSockets.clear();
    fdToServerIndex.clear();
    
    if (connManager)
        connManager->closeAllClients();
    
    if (epollFd >= 0) {
        close(epollFd);
        epollFd = -1;
        std::cout << "Epoll instance closed" << std::endl;
    }
    
    std::cout << "Server shutdown complete" << std::endl;
}

void WebServer::handleCgiPipeRead(int pipeFd) {
    ClientConnection* client = connManager->findClientByCgiPipe(pipeFd);
    if (!client) {
        std::cerr << "CGI: No client found for pipe " << pipeFd << std::endl;
        return;
    }
    
    if (client->state != ClientConnection::CGI_RUNNING || client->serverIndex >= httpHandlers.size())
        return;
    
    CgiHandler* cgiHandler = httpHandlers[client->serverIndex]->getCgiHandler();
    if (!cgiHandler)
        return;
    
    ssize_t bytesRead = cgiHandler->readFromCgi(client);
    
    if (bytesRead == 0 || bytesRead < 0) {
        std::cout << "CGI: Output complete for client " << client->fd << std::endl;
        cgiHandler->checkCgiComplete(client);
        cgiHandler->buildResponse(client);
        connManager->removeCgiPipes(client);
        cgiHandler->cleanup(client);
        client->state = ClientConnection::SENDING_RESPONSE;
        connManager->prepareResponseMode(client);
    }
}

void WebServer::handleCgiPipeWrite(int pipeFd) {
    ClientConnection* client = connManager->findClientByCgiPipe(pipeFd);
    if (!client) {
        std::cerr << "CGI: No client found for pipe " << pipeFd << std::endl;
        return;
    }
    
    if (client->serverIndex >= httpHandlers.size())
        return;
    
    CgiHandler* cgiHandler = httpHandlers[client->serverIndex]->getCgiHandler();
    if (!cgiHandler)
        return;
    
    if (client->cgiBodyOffset >= client->cgiBody.size()) {
        epoll_ctl(epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
        connManager->removeSingleCgiPipe(pipeFd);
        close(pipeFd);
        client->cgiInputFd = -1;
        return;
    }
    
    ssize_t bytesWritten = cgiHandler->writeToCgi(client);
    
    if (bytesWritten < 0) {
        std::cerr << "CGI: Error writing to CGI for client " << client->fd << std::endl;
        cgiHandler->killCgi(client);
        connManager->removeCgiPipes(client);
        
        const ServerConfig& server = config.getServer(client->serverIndex);
        client->responseBuffer = HttpResponse::build500("CGI execution error", &server);
        
        client->state = ClientConnection::SENDING_RESPONSE;
        connManager->prepareResponseMode(client);
    } else if (bytesWritten == 0 || (bytesWritten > 0 && client->cgiBodyOffset >= client->cgiBody.size())) {
        epoll_ctl(epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
        connManager->removeSingleCgiPipe(pipeFd);
        close(pipeFd);
        client->cgiInputFd = -1;
    }
}

void WebServer::checkCgiTimeouts() {
    std::vector<ClientConnection*>& clients = connManager->getClients();
    
    for (size_t i = 0; i < clients.size(); ++i) {
        ClientConnection* client = clients[i];
        
        if (client->state != ClientConnection::CGI_RUNNING || client->serverIndex >= httpHandlers.size())
            continue;
        
        CgiHandler* cgiHandler = httpHandlers[client->serverIndex]->getCgiHandler();
        if (!cgiHandler)
            continue;
        
        if (cgiHandler->hasTimedOut(client, CgiHandler::DEFAULT_CGI_TIMEOUT)) {
            std::cerr << "CGI: Timeout for client " << client->fd << std::endl;
            cgiHandler->killCgi(client);
            connManager->removeCgiPipes(client);
            
            const ServerConfig& server = config.getServer(client->serverIndex);
            client->responseBuffer = HttpResponse::build504(&server);
            
            client->state = ClientConnection::SENDING_RESPONSE;
            connManager->prepareResponseMode(client);
        }
    }
}
