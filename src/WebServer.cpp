#include "../include/WebServer.hpp"

WebServer::WebServer() : serverSocket(-1), epollFd(-1), running(false), 
                         connManager(NULL), httpHandler(NULL) {
    std::memset(&serverAddr, 0, sizeof(serverAddr));
}

WebServer::~WebServer() {
    stop();
    delete connManager;
    delete httpHandler;
}

bool WebServer::initialize(const std::string& configFile) {
    if (!config.loadFromFile(configFile)) {
        return false;
    }
    
    try {
        setupSocket();
        setupEpoll();
        
        // Initialize managers after epoll is created
        connManager = new ConnectionManager(epollFd);
        httpHandler = new HttpRequest(config);
    } catch (const std::exception& e) {
        std::cerr << "Error initializing server: " << e.what() << std::endl;
        return false;
    }
    
    return true;
}

void WebServer::setupSocket() {
    // Create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        throw std::runtime_error("Failed to create socket");
    }
    
    // Set socket to non-blocking mode (required by subject)
    int flags = fcntl(serverSocket, F_GETFL, 0);
    if (flags < 0 || fcntl(serverSocket, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(serverSocket);
        throw std::runtime_error("Failed to set socket to non-blocking");
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(serverSocket);
        throw std::runtime_error("Failed to set socket options");
    }
    
    // Configure server address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(config.getPort());
    if (inet_pton(AF_INET, config.getHost().c_str(), &serverAddr.sin_addr) <= 0) {
        close(serverSocket);
        throw std::runtime_error("Invalid address");
    }
    
    // Bind socket
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(serverSocket);
        throw std::runtime_error("Failed to bind socket");
    }
    
    // Listen for connections (increased backlog to 128 for better performance)
    if (listen(serverSocket, 128) < 0) {
        close(serverSocket);
        throw std::runtime_error("Failed to listen on socket");
    }
    
    std::cout << "Server listening on " << config.getHost() << ":" << config.getPort() << std::endl;
}

void WebServer::setupEpoll() {
    // Create epoll instance
    epollFd = epoll_create(1);
    if (epollFd < 0) {
        close(serverSocket);
        throw std::runtime_error("Failed to create epoll instance");
    }
    
    // Set FD_CLOEXEC flag on epoll file descriptor
    int flags = fcntl(epollFd, F_GETFD);
    if (flags >= 0) {
        fcntl(epollFd, F_SETFD, flags | FD_CLOEXEC);
    }
    
    // Add server socket to epoll
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  // Monitor for read events
    ev.data.fd = serverSocket;
    
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, serverSocket, &ev) < 0) {
        close(epollFd);
        close(serverSocket);
        throw std::runtime_error("Failed to add server socket to epoll");
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
            uint32_t activeEvents = events[i].events;  // ← Solo eventi attivi
            
            // Handle errors first
            if (activeEvents & (EPOLLERR | EPOLLHUP)) {
                std::cerr << "Error/Hangup on FD " << fd << std::endl;
                if (fd != serverSocket)
                    connManager->removeClient(fd);
                continue;
            }

            if (fd == serverSocket) {
                handleNewConnection();
            }
            else { // Client socket
                // Check peer disconnect
                if (activeEvents & EPOLLRDHUP) {
                    std::cout << "Client " << fd << " disconnected" << std::endl;
                    connManager->removeClient(fd);
                    continue;
                }
                
                // ✅ Handle READ event (if present)
                if (activeEvents & EPOLLIN) {
                    handleClientRead(fd);
                }
                
                // ✅ Handle WRITE event (if present)
                if (activeEvents & EPOLLOUT) {
                    handleClientWrite(fd);
                }
            }
        }
    }
    std::cout << "Server stopped." << std::endl;
}

void WebServer::handleNewConnection() {
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    
    // Accept one connection (level-triggered will notify again if more are pending)
    int clientSocket = accept(serverSocket, 
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
    
    // Add client to connection manager
    connManager->addClient(clientSocket);
    
    std::cout << "New connection from " << clientIP 
              << ":" << ntohs(clientAddr.sin_port) 
              << " on socket " << clientSocket << std::endl;
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
    
    // Handle the HTTP request
    httpHandler->handleRequest(client);
    
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
        // Response complete, close connection (HTTP/1.0 default)
        std::cout << "Response sent completely to socket " << clientSocket << std::endl;
        connManager->removeClient(clientSocket);
    }
    // If not complete, EPOLLOUT will trigger again when socket is ready
}

void WebServer::stop() {
    if (!running)
        return;
    running = false;
    
    if (serverSocket >= 0) {
        // Remove server socket from epoll
        epoll_ctl(epollFd, EPOLL_CTL_DEL, serverSocket, NULL);
        
        // Close server socket
        close(serverSocket);
        serverSocket = -1;
        std::cout << "Server socket closed (no new connections)" << std::endl;
    }

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
