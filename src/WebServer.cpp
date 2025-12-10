#include "../include/WebServer.hpp"
#include "../include/HttpResponse.hpp"
#include <sstream>
#include <cctype>

WebServer::WebServer() : epollFd(-1), running(false), connManager(NULL) {
}

WebServer::~WebServer() {
    stop();
    
    // Clean up HTTP handlers
    for (size_t i = 0; i < httpHandlers.size(); ++i) {
        if (httpHandlers[i]) {
            delete httpHandlers[i];
        }
    }
    httpHandlers.clear();
    
    // Clean up connection manager
    if (connManager) {
        delete connManager;
        connManager = NULL;
    }
}

bool WebServer::initialize(const std::string& configFile) {
    if (!config.loadFromFile(configFile)) {
        return false;
    }
    
    if (config.getServerCount() == 0) {
        std::cerr << "Error: No server blocks defined in configuration" << std::endl;
        return false;
    }
    
    try {
        // Setup epoll first
        setupEpoll();
        
        // Setup all server sockets
        for (size_t i = 0; i < config.getServerCount(); ++i) {
            const ServerConfig& serverConfig = config.getServer(i);
            
            // Check for duplicate bindings
            if (isDuplicateBinding(serverConfig.host, serverConfig.port)) {
                std::cerr << "Error: Duplicate server binding for " 
                          << serverConfig.host << ":" << serverConfig.port << std::endl;
                throw std::runtime_error("Duplicate server binding");
            }
            
            if (!setupServerSocket(serverConfig, i)) {
                throw std::runtime_error("Failed to setup server socket");
            }
            
            // Create HTTP handler for this server
            httpHandlers.push_back(new HttpRequest(config));
        }
        
        // Initialize connection manager
        connManager = new ConnectionManager(epollFd);
        
        std::cout << "Initialized " << serverSockets.size() << " server(s)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error initializing server: " << e.what() << std::endl;
        // Cleanup partially initialized resources
        for (size_t i = 0; i < httpHandlers.size(); ++i) {
            delete httpHandlers[i];
        }
        httpHandlers.clear();
        for (size_t i = 0; i < serverSockets.size(); ++i) {
            if (serverSockets[i].fd >= 0) {
                close(serverSockets[i].fd);
            }
        }
        serverSockets.clear();
        if (epollFd >= 0) {
            close(epollFd);
            epollFd = -1;
        }
        return false;
    }
    
    return true;
}

bool WebServer::isDuplicateBinding(const std::string& host, int port) const {
    for (size_t i = 0; i < serverSockets.size(); ++i) {
        if (serverSockets[i].host == host && serverSockets[i].port == port) {
            return true;
        }
    }
    return false;
}

bool WebServer::setupServerSocket(const ServerConfig& serverConfig, size_t index) {
    // Create socket
    int sockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFd < 0) {
        std::cerr << "Failed to create socket for " << serverConfig.host 
                  << ":" << serverConfig.port << std::endl;
        return false;
    }
    
    // Set socket to non-blocking mode
    int flags = fcntl(sockFd, F_GETFL, 0);
    if (flags < 0 || fcntl(sockFd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "Failed to set socket to non-blocking" << std::endl;
        close(sockFd);
        return false;
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(sockFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set socket options" << std::endl;
        close(sockFd);
        return false;
    }
    
    // Configure server address
    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverConfig.port);
    if (inet_pton(AF_INET, serverConfig.host.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid address: " << serverConfig.host << std::endl;
        close(sockFd);
        return false;
    }
    
    // Bind socket
    if (bind(sockFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Failed to bind " << serverConfig.host << ":" 
                  << serverConfig.port << " - " << strerror(errno) << std::endl;
        close(sockFd);
        return false;
    }
    
    // Listen for connections
    if (listen(sockFd, 128) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(sockFd);
        return false;
    }
    
    // Add to epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;  // Level-triggered
    ev.data.fd = sockFd;
    
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, sockFd, &ev) < 0) {
        std::cerr << "Failed to add server socket to epoll" << std::endl;
        close(sockFd);
        return false;
    }
    
    // Store server socket info
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

void WebServer::setupEpoll() {
    // Create epoll instance
    epollFd = epoll_create(1);
    if (epollFd < 0) {
        throw std::runtime_error("Failed to create epoll instance");
    }
    
    // Set FD_CLOEXEC flag on epoll file descriptor
    int flags = fcntl(epollFd, F_GETFD);
    if (flags >= 0) {
        fcntl(epollFd, F_SETFD, flags | FD_CLOEXEC);
    }
}

void WebServer::run() {
    running = true;
    
    // Array to hold events returned by epoll_wait
    const int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];
    
    std::cout << "Server running with epoll..." << std::endl;
    
    while (running) {
        // Wait for events (1 second timeout)
        int numEvents = epoll_wait(epollFd, events, MAX_EVENTS, 1000);
        
        if (numEvents < 0) {
            if (errno == EINTR) {
                // Signal interrupted epoll_wait, check if we should stop
                continue;
            }
            std::cerr << "Error in epoll_wait: " << strerror(errno) << std::endl;
            break;
        }
        
        // Check CGI timeouts (even on timeout with no events)
        checkCgiTimeouts();
        
        if (numEvents == 0) {
            // Timeout - no events, continue loop to check running flag
            continue;
        }
        
        // Process all events
        for (int i = 0; i < numEvents; ++i) {
            int fd = events[i].data.fd;
            uint32_t activeEvents = events[i].events;
            
            // Check if this is a CGI pipe
            if (connManager->isCgiPipe(fd)) {
                // Handle CGI pipe errors
                if (activeEvents & (EPOLLERR | EPOLLHUP)) {
                    ClientConnection* client = connManager->findClientByCgiPipe(fd);
                    if (client) {
                        // CGI process ended or error
                        if (client->serverIndex < httpHandlers.size()) {
                            CgiHandler* cgiHandler = httpHandlers[client->serverIndex]->getCgiHandler();
                            if (cgiHandler) {
                                // Read any remaining output
                                if (fd == client->cgiOutputFd) {
                                    cgiHandler->readFromCgi(client);
                                }
                                // Check if CGI completed
                                cgiHandler->checkCgiComplete(client);
                                // Build response
                                cgiHandler->buildResponse(client);
                                cgiHandler->cleanup(client);
                            }
                        }
                        connManager->removeCgiPipes(client);
                        client->state = ClientConnection::SENDING_RESPONSE;
                        connManager->prepareResponseMode(client);
                    }
                    continue;
                }
                
                // Handle CGI pipe read (output from CGI)
                if (activeEvents & EPOLLIN) {
                    handleCgiPipeRead(fd);
                }
                
                // Handle CGI pipe write (input to CGI)
                if (activeEvents & EPOLLOUT) {
                    handleCgiPipeWrite(fd);
                }
                continue;
            }
            
            // Handle errors first (non-CGI fds)
            if (activeEvents & (EPOLLERR | EPOLLHUP)) {
                std::cerr << "Error/Hangup on FD " << fd << std::endl;
                // Check if it's a server socket
                bool isServerSocket = false;
                for (size_t j = 0; j < serverSockets.size(); ++j) {
                    if (fd == serverSockets[j].fd) {
                        isServerSocket = true;
                        break;
                    }
                }
                if (!isServerSocket) {
                    connManager->removeClient(fd);
                }
                continue;
            }

            // Check if this is a server socket (listening socket)
            bool isServerSocket = false;
            for (size_t j = 0; j < serverSockets.size(); ++j) {
                if (fd == serverSockets[j].fd) {
                    handleNewConnection(fd);
                    isServerSocket = true;
                    break;
                }
            }
            
            if (!isServerSocket) { // Client socket
                // Check peer disconnect
                if (activeEvents & EPOLLRDHUP) {
                    std::cout << "Client " << fd << " disconnected" << std::endl;
                    connManager->removeClient(fd);
                    continue;
                }
                
                // Handle READ event (if present)
                if (activeEvents & EPOLLIN) {
                    handleClientRead(fd);
                }
                
                // Handle WRITE event (if present)
                if (activeEvents & EPOLLOUT) {
                    handleClientWrite(fd);
                }
            }
        }
    }
    std::cout << "Server stopped." << std::endl;
}

void WebServer::handleNewConnection(int serverFd) {
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    
    // Accept one connection
    int clientSocket = accept(serverFd, 
                             (struct sockaddr*)&clientAddr, 
                             &clientLen);
    
    if (clientSocket < 0) {
        std::cerr << "Error accepting connection: " 
                  << strerror(errno) << std::endl;
        return;
    }
    
    // Set client socket to non-blocking mode
    int flags = fcntl(clientSocket, F_GETFL, 0);
    if (flags < 0 || fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "Failed to set client socket to non-blocking" << std::endl;
        close(clientSocket);
        return;
    }
    
    // Set FD_CLOEXEC flag on client socket
    flags = fcntl(clientSocket, F_GETFD);
    if (flags >= 0) {
        fcntl(clientSocket, F_SETFD, flags | FD_CLOEXEC);
    }
    
    // Add client socket to epoll (level-triggered, no EPOLLET)
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP;  // Level-triggered, read events, and disconnect detection
    ev.data.fd = clientSocket;
    
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientSocket, &ev) < 0) {
        std::cerr << "Failed to add client socket to epoll: " 
                  << strerror(errno) << std::endl;
        close(clientSocket);
        return;
    }
    
    // Get client IP address
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
    
    // Find which server this connection belongs to
    size_t serverIndex = 0;
    if (fdToServerIndex.find(serverFd) != fdToServerIndex.end()) {
        serverIndex = fdToServerIndex[serverFd];
    }
    
    // Add client to connection manager
    connManager->addClient(clientSocket, serverIndex);
    
    const ServerConfig& serverConfig = config.getServer(serverIndex);
    std::cout << "New connection from " << clientIP 
              << ":" << ntohs(clientAddr.sin_port) 
              << " on socket " << clientSocket 
              << " (server: " << serverConfig.host << ":" << serverConfig.port << ")"
              << std::endl;
}

void WebServer::handleClientRead(int clientSocket) {
    // Find client connection
    ClientConnection* client = connManager->findClient(clientSocket);
    if (!client) {
        std::cerr << "Client not found: " << clientSocket << std::endl;
        return;
    }
    
    // If client is in CGI_RUNNING state, we shouldn't be reading from client
    if (client->state == ClientConnection::CGI_RUNNING) {
        return;
    }
    
    char buffer[1000000];  // 1MB - original size, matches kernel default SO_RCVBUF
    
    // Read data from socket (non-blocking)
    ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
    
    // Handle EAGAIN/EWOULDBLOCK - no data available yet, just return
    if (bytesRead < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        // Real error occurred
        std::cerr << "recv error on fd=" << clientSocket << ": " << strerror(errno) << std::endl;
        connManager->removeClient(clientSocket);
        return;
    }
    
    if (bytesRead == 0) {
        // Client closed connection
        std::cout << "Client " << clientSocket << " closed connection" << std::endl;
        connManager->removeClient(clientSocket);
        return;
    }
    
    // Append received data to request buffer
    size_t oldBufferSize = client->requestBuffer.size();
    client->requestBuffer.append(buffer, bytesRead);
    
    if (!client->headersComplete) {
        // Optimize header search: only search in newly received data + small overlap
        // to avoid searching the entire buffer repeatedly for large uploads
        size_t searchStart = (oldBufferSize > 3) ? (oldBufferSize - 3) : 0;
        size_t headerEnd = client->requestBuffer.find("\r\n\r\n", searchStart);
        if (headerEnd != std::string::npos) {
            client->headersComplete = true;
            client->headerEndOffset = headerEnd + 4; // Position after \r\n\r\n
            
            // Calculate body bytes already received with headers
            if (client->requestBuffer.length() > client->headerEndOffset) {
                client->bodyBytesReceived = client->requestBuffer.length() - client->headerEndOffset;
            }
            
            // Determine max body size based on location (best match)
            if (client->serverIndex < config.getServerCount()) {
                const ServerConfig& server = config.getServer(client->serverIndex);
                
                // Extract path from request line
                std::string requestPath;
                size_t firstSpace = client->requestBuffer.find(' ');
                if (firstSpace != std::string::npos) {
                    size_t secondSpace = client->requestBuffer.find(' ', firstSpace + 1);
                    if (secondSpace != std::string::npos) {
                        requestPath = client->requestBuffer.substr(firstSpace + 1, secondSpace - firstSpace - 1);
                        // Remove query string if present
                        size_t queryPos = requestPath.find('?');
                        if (queryPos != std::string::npos) {
                            requestPath = requestPath.substr(0, queryPos);
                        }
                    }
                }
                
                // Find best matching location for body size limit
                size_t bestMatchLen = 0;
                size_t locationMaxBodySize = 0;
                bool locationHasBodySizeLimit = false;
                
                for (size_t i = 0; i < server.locations.size(); ++i) {
                    const LocationConfig& loc = server.locations[i];
                    // Check if path starts with location path (prefix match)
                    // For proper matching, we need to ensure the location ends at a path boundary
                    if (requestPath.compare(0, loc.path.length(), loc.path) == 0) {
                        // Check that match is at a proper path boundary
                        // Either: exact match, or followed by '/', or location ends with '/'
                        bool validMatch = false;
                        if (requestPath.length() == loc.path.length()) {
                            // Exact match
                            validMatch = true;
                        } else if (loc.path[loc.path.length() - 1] == '/') {
                            // Location ends with /
                            validMatch = true;
                        } else if (requestPath.length() > loc.path.length() && 
                                   requestPath[loc.path.length()] == '/') {
                            // Request path continues with /
                            validMatch = true;
                        }
                        
                        // For best match, prefer longer paths
                        if (validMatch && loc.path.length() > bestMatchLen) {
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
                
                // Use location limit if explicitly configured, otherwise server limit
                // Value of 0 means "unlimited" (no check)
                if (locationHasBodySizeLimit) {
                    client->maxBodySize = locationMaxBodySize;
                } else {
                    client->maxBodySize = server.clientMaxBodySize;
                }
                
                // Early rejection: check Content-Length header against limit
                // This prevents waiting for a huge body that will be rejected anyway
                if (client->maxBodySize > 0) {
                    std::string headers = client->requestBuffer.substr(0, client->headerEndOffset);
                    size_t pos = headers.find("Content-Length:");
                    if (pos == std::string::npos) {
                        pos = headers.find("content-length:");
                    }
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
                                client->responseBuffer = HttpResponse::build413(&server);
                                client->state = ClientConnection::SENDING_RESPONSE;
                                connManager->prepareResponseMode(client);
                                return;
                            }
                        }
                    }
                }
            }
        }
    } else {
        // Headers already complete, add new bytes to body count
        client->bodyBytesReceived += bytesRead;
    }
    
    // Progressive body size check (only after headers are complete and limit is set)
    // maxBodySize == 0 means unlimited
    // Skip this check for chunked encoding - will be checked after decoding
    if (client->headersComplete && client->maxBodySize > 0) {
        // Check if this is NOT chunked transfer encoding
        std::string headers = client->requestBuffer.substr(0, client->headerEndOffset);
        bool isChunked = (headers.find("Transfer-Encoding: chunked") != std::string::npos ||
                          headers.find("transfer-encoding: chunked") != std::string::npos);
        
        if (!isChunked && client->bodyBytesReceived > client->maxBodySize) {
            std::cout << "Body size " << client->bodyBytesReceived 
                      << " exceeds limit " << client->maxBodySize 
                      << " during reading (progressive check)" << std::endl;
            
            // Get server config for error page
            const ServerConfig* serverConfig = NULL;
            if (client->serverIndex < config.getServerCount()) {
                serverConfig = &config.getServer(client->serverIndex);
            }
            
            client->responseBuffer = HttpResponse::build413(serverConfig);
            client->state = ClientConnection::SENDING_RESPONSE;
            connManager->prepareResponseMode(client);
            return;
        }
    }
    
    // Check if we need to wait for more body data (for POST/PUT)
    if (client->headersComplete) {
        std::string headers = client->requestBuffer.substr(0, client->headerEndOffset);
        std::string method;
        size_t firstSpace = headers.find(' ');
        if (firstSpace != std::string::npos) {
            method = headers.substr(0, firstSpace);
        }
        
        // For POST and PUT, wait for complete body before processing
        if (method == "POST" || method == "PUT") {
            // Check for Transfer-Encoding: chunked
            bool isChunked = (headers.find("Transfer-Encoding: chunked") != std::string::npos ||
                              headers.find("transfer-encoding: chunked") != std::string::npos);
            
            if (isChunked) {
                // For chunked encoding, wait for the final chunk marker "0\r\n\r\n"
                std::string body = client->requestBuffer.substr(client->headerEndOffset);
                // The final chunk is "0\r\n" followed by optional trailer headers and "\r\n"
                // Minimum ending is "0\r\n\r\n"
                if (body.find("0\r\n\r\n") == std::string::npos) {
                    // Haven't received the final chunk yet, wait for more data
                    return;
                }
            } else {
                // Extract Content-Length from headers
                size_t contentLength = 0;
                bool hasContentLength = false;
                size_t pos = headers.find("Content-Length:");
                if (pos == std::string::npos) {
                    pos = headers.find("content-length:");
                }
                
                if (pos != std::string::npos) {
                    hasContentLength = true;
                    size_t valueStart = headers.find_first_not_of(" \t", pos + 15);
                    size_t valueEnd = headers.find("\r\n", valueStart);
                    if (valueStart != std::string::npos && valueEnd != std::string::npos) {
                        std::string lengthStr = headers.substr(valueStart, valueEnd - valueStart);
                        contentLength = std::atoi(lengthStr.c_str());
                    }
                }
                
                // POST/PUT requires Content-Length if not chunked
                if (!hasContentLength) {
                    std::cout << "Rejecting POST/PUT without Content-Length (not chunked)" << std::endl;
                    client->responseBuffer = HttpResponse::build411();
                    client->state = ClientConnection::SENDING_RESPONSE;
                    connManager->prepareResponseMode(client);
                    return;
                }
                
                // If we have Content-Length, wait for the complete body
                if (contentLength > 0 && client->bodyBytesReceived < contentLength) {
                    return; // Wait for more data
                }
            }
        }
    }
    
    // Use the appropriate HTTP handler for this server
    if (client->serverIndex < httpHandlers.size()) {
        httpHandlers[client->serverIndex]->handleRequest(client);
    }
    
    // Check if CGI was started
    if (client->state == ClientConnection::CGI_RUNNING) {
        // Add CGI pipes to epoll
        connManager->addCgiPipes(client);
        return;
    }
    
    // If response is ready, prepare for sending
    if (!client->responseBuffer.empty()) {
        client->state = ClientConnection::SENDING_RESPONSE;
        connManager->prepareResponseMode(client);
    }
}

void WebServer::handleClientWrite(int clientSocket) {
    // Find client connection
    ClientConnection* client = connManager->findClient(clientSocket);
    if (!client) {
        std::cerr << "Client not found: " << clientSocket << std::endl;
        return;
    }
    
    // Check if there's data to send
    if (client->isResponseComplete()) {
        // Nothing to send — treat as response complete. Decide keep-alive per HTTP version/Connection header.
        // Determine request headers if available
        std::string reqHeaders;
        if (client->headerEndOffset > 0 && client->requestBuffer.length() >= client->headerEndOffset) {
            reqHeaders = client->requestBuffer.substr(0, client->headerEndOffset);
        }

        // Parse request line to get HTTP version
        std::string version;
        size_t firstLineEnd = reqHeaders.find("\r\n");
        if (firstLineEnd != std::string::npos) {
            std::string firstLine = reqHeaders.substr(0, firstLineEnd);
            std::istringstream fls(firstLine);
            std::string method, path;
            fls >> method >> path >> version;
        }

        // Lowercase headers for case-insensitive search
        std::string headersLower = reqHeaders;
        for (size_t i = 0; i < headersLower.size(); ++i) {
            headersLower[i] = std::tolower(headersLower[i]);
        }

        bool hasConnClose = (headersLower.find("connection: close") != std::string::npos);
        bool hasConnKeepAlive = (headersLower.find("connection: keep-alive") != std::string::npos);

        bool keepAlive = false;
        if (version == "HTTP/1.1") {
            keepAlive = !hasConnClose; // default keep-alive in HTTP/1.1
        } else if (version == "HTTP/1.0") {
            keepAlive = hasConnKeepAlive; // only keep if client requested it
        }

        if (keepAlive) {
            // Reset client to read next request
            client->clearBuffers();
            client->state = ClientConnection::READING_REQUEST;

            // Modify epoll to monitor for reading again
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLRDHUP;
            ev.data.fd = clientSocket;
            if (epoll_ctl(epollFd, EPOLL_CTL_MOD, clientSocket, &ev) < 0) {
                connManager->removeClient(clientSocket);
                return;
            }
            return;
        }

        connManager->removeClient(clientSocket);
        return;
    }
    
    // Send remaining data
    size_t remaining = client->getRemainingBytes();
    ssize_t sent = send(clientSocket, 
                       client->responseBuffer.c_str() + client->bytesSent, 
                       remaining, 
                       0);
    
    // Check return value only (no errno checks)
    if (sent < 0) {
        // Error occurred
        connManager->removeClient(clientSocket);
        return;
    }
    
    client->bytesSent += sent;
    
    // Check if all data has been sent
    if (client->isResponseComplete()) {
        // Extract response code from first line (e.g., "HTTP/1.1 200 OK")
        std::string statusLine;
        size_t endOfLine = client->responseBuffer.find("\r\n");
        if (endOfLine != std::string::npos) {
            statusLine = client->responseBuffer.substr(0, endOfLine);
        }

        // Determine keep-alive based on the request version and Connection header
        std::string reqHeaders;
        if (client->headerEndOffset > 0 && client->requestBuffer.length() >= client->headerEndOffset) {
            reqHeaders = client->requestBuffer.substr(0, client->headerEndOffset);
        }

        std::string version;
        size_t firstLineEnd = reqHeaders.find("\r\n");
        if (firstLineEnd != std::string::npos) {
            std::string firstLine = reqHeaders.substr(0, firstLineEnd);
            std::istringstream fls(firstLine);
            std::string method, path;
            fls >> method >> path >> version;
        }

        std::string headersLower = reqHeaders;
        for (size_t i = 0; i < headersLower.size(); ++i) {
            headersLower[i] = std::tolower(headersLower[i]);
        }
        bool hasConnClose = (headersLower.find("connection: close") != std::string::npos);
        bool hasConnKeepAlive = (headersLower.find("connection: keep-alive") != std::string::npos);

        bool keepAlive = false;
        if (version == "HTTP/1.1") {
            keepAlive = !hasConnClose;
        } else if (version == "HTTP/1.0") {
            keepAlive = hasConnKeepAlive;
        }

        std::cout << "Response sent to socket " << clientSocket 
                  << " [" << statusLine << "]" << std::endl;

        if (keepAlive) {
            // Prepare to read next request on same connection
            client->clearBuffers();
            client->state = ClientConnection::READING_REQUEST;

            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLRDHUP;
            ev.data.fd = clientSocket;
            if (epoll_ctl(epollFd, EPOLL_CTL_MOD, clientSocket, &ev) < 0) {
                connManager->removeClient(clientSocket);
                return;
            }
        } else {
            connManager->removeClient(clientSocket);
        }
    }
    // If not complete, EPOLLOUT will trigger again when socket is ready
}

void WebServer::stop() {
    if (!running)
        return;
    running = false;
    
    // Close all server sockets
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

    // Close all client sockets
    if (connManager) {
        connManager->closeAllClients();
    }
    
    if (epollFd >= 0) {
        close(epollFd);
        epollFd = -1;
        std::cout << "Epoll instance closed" << std::endl;
    }
    
    std::cout << "Server shutdown complete" << std::endl;
}

// ==================== CGI Handling Methods ====================

void WebServer::handleCgiPipeRead(int pipeFd) {
    // Find the client associated with this CGI pipe
    ClientConnection* client = connManager->findClientByCgiPipe(pipeFd);
    if (!client) {
        std::cerr << "CGI: No client found for pipe " << pipeFd << std::endl;
        return;
    }
    
    // If client is no longer in CGI_RUNNING state, skip processing
    // (This can happen if timeout handler already processed this connection)
    if (client->state != ClientConnection::CGI_RUNNING) {
        return;
    }
    
    if (client->serverIndex >= httpHandlers.size()) {
        return;
    }
    
    CgiHandler* cgiHandler = httpHandlers[client->serverIndex]->getCgiHandler();
    if (!cgiHandler) {
        return;
    }
    
    // Read from CGI output
    ssize_t bytesRead = cgiHandler->readFromCgi(client);
    
    if (bytesRead == 0 || bytesRead < 0) {
        // EOF or error - CGI finished output
        std::cout << "CGI: Output complete for client " << client->fd << std::endl;
        
        // Check if process has exited
        cgiHandler->checkCgiComplete(client);
        
        // Build HTTP response from CGI output
        cgiHandler->buildResponse(client);
        
        // Clean up CGI resources
        connManager->removeCgiPipes(client);
        cgiHandler->cleanup(client);
        
        // Prepare to send response
        client->state = ClientConnection::SENDING_RESPONSE;
        connManager->prepareResponseMode(client);
    }
}

void WebServer::handleCgiPipeWrite(int pipeFd) {
    // Find the client associated with this CGI pipe
    ClientConnection* client = connManager->findClientByCgiPipe(pipeFd);
    if (!client) {
        std::cerr << "CGI: No client found for pipe " << pipeFd << std::endl;
        return;
    }
    
    if (client->serverIndex >= httpHandlers.size()) {
        return;
    }
    
    CgiHandler* cgiHandler = httpHandlers[client->serverIndex]->getCgiHandler();
    if (!cgiHandler) {
        return;
    }
    
    // Check if all data is already written
    if (client->cgiBodyOffset >= client->cgiBody.size()) {
        // All data written, close and remove only the input pipe
        epoll_ctl(epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
        connManager->removeSingleCgiPipe(pipeFd);
        close(pipeFd);
        client->cgiInputFd = -1;
        return;
    }
    
    // Write to CGI input
    ssize_t bytesWritten = cgiHandler->writeToCgi(client);
    
    if (bytesWritten < 0) {
        // Error writing to CGI
        std::cerr << "CGI: Error writing to CGI for client " << client->fd << std::endl;
        
        // Kill CGI process and return error
        cgiHandler->killCgi(client);
        connManager->removeCgiPipes(client);
        
        const ServerConfig& server = config.getServer(client->serverIndex);
        client->responseBuffer = HttpResponse::build500("CGI execution error", &server);
        
        client->state = ClientConnection::SENDING_RESPONSE;
        connManager->prepareResponseMode(client);
    }
    // If bytesWritten >= 0, keep monitoring for more writes
}

void WebServer::checkCgiTimeouts() {
    std::vector<ClientConnection*>& clients = connManager->getClients();
    
    for (size_t i = 0; i < clients.size(); ++i) {
        ClientConnection* client = clients[i];
        
        if (client->state != ClientConnection::CGI_RUNNING) {
            continue;
        }
        
        if (client->serverIndex >= httpHandlers.size()) {
            continue;
        }
        
        CgiHandler* cgiHandler = httpHandlers[client->serverIndex]->getCgiHandler();
        if (!cgiHandler) {
            continue;
        }
        
        // Check timeout (30 seconds default)
        if (cgiHandler->hasTimedOut(client, CgiHandler::DEFAULT_CGI_TIMEOUT)) {
            std::cerr << "CGI: Timeout for client " << client->fd << std::endl;
            
            // Kill the CGI process
            cgiHandler->killCgi(client);
            connManager->removeCgiPipes(client);
            
            // Send 504 Gateway Timeout
            const ServerConfig& server = config.getServer(client->serverIndex);
            client->responseBuffer = HttpResponse::build504(&server);
            
            client->state = ClientConnection::SENDING_RESPONSE;
            connManager->prepareResponseMode(client);
        }
    }
}
