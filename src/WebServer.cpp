#include "../include/WebServer.hpp"

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
        
        if (numEvents == 0) {
            // Timeout - no events, continue loop to check running flag
            continue;
        }
        
        // Process all events
        for (int i = 0; i < numEvents; ++i) {
            int fd = events[i].data.fd;
            uint32_t activeEvents = events[i].events;
            
            // Handle errors first
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
    
    char buffer[4096];
    
    // Read data from socket (non-blocking)
    ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
    
    // Check return value only (no errno checks)
    if (bytesRead < 0) {
        // Error occurred (including EAGAIN/EWOULDBLOCK)
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
    client->requestBuffer.append(buffer, bytesRead);
    
    // Use the appropriate HTTP handler for this server
    if (client->serverIndex < httpHandlers.size()) {
        httpHandlers[client->serverIndex]->handleRequest(client);
    }
    
    // If response is ready, prepare for sending
    if (!client->responseBuffer.empty()) {
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
        // Nothing to send, close connection (HTTP/1.0 behavior)
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
        // Extract response code from first line (e.g., "HTTP/1.0 200 OK")
        std::string statusLine;
        size_t endOfLine = client->responseBuffer.find("\r\n");
        if (endOfLine != std::string::npos) {
            statusLine = client->responseBuffer.substr(0, endOfLine);
        }
        // Response complete, close connection (HTTP/1.0 default)
        std::cout << "Response sent to socket " << clientSocket 
                  << " [" << statusLine << "]" << std::endl;
        connManager->removeClient(clientSocket);
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
