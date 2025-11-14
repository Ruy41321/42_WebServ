#include "../include/WebServer.hpp"

WebServer::WebServer() : serverSocket(-1), epollFd(-1), running(false) {
    std::memset(&serverAddr, 0, sizeof(serverAddr));
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::initialize(const std::string& configFile) {
    if (!config.loadFromFile(configFile)) {
        return false;
    }
    
    try {
        setupSocket();
        setupEpoll();
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
                    closeClient(fd);
                continue;
            }

            if (fd == serverSocket) {
                handleNewConnection();
            }
            else { // Client socket
                // Check peer disconnect
                if (activeEvents & EPOLLRDHUP) {
                    std::cout << "Client " << fd << " disconnected" << std::endl;
                    closeClient(fd);
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
    
    // Create ClientConnection object
    ClientConnection* client = new ClientConnection(clientSocket);
    clients.push_back(client);
    
    std::cout << "New connection from " << clientIP 
              << ":" << ntohs(clientAddr.sin_port) 
              << " on socket " << clientSocket << std::endl;
}

void WebServer::closeClient(int clientSocket) {
    epoll_ctl(epollFd, EPOLL_CTL_DEL, clientSocket, NULL);
    close(clientSocket);
    
    // Remove from clients vector and free memory
    for (std::vector<ClientConnection*>::iterator it = clients.begin(); it != clients.end(); ++it) {
        if ((*it)->fd == clientSocket) {
            delete *it;
            clients.erase(it);
            break;
        }
    }
    
    std::cout << "Closed connection on socket " << clientSocket << std::endl;
}

void WebServer::handleClientRead(int clientSocket) {
    // Find client connection
    ClientConnection* client = findClient(clientSocket);
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
        closeClient(clientSocket);
        return;
    }
    
    if (bytesRead == 0) {
        // Client closed connection
        std::cout << "Client " << clientSocket << " closed connection" << std::endl;
        closeClient(clientSocket);
        return;
    }
    
    // Append received data to request buffer
    client->requestBuffer.append(buffer, bytesRead);
    
    // Check if we have received complete HTTP headers (\r\n\r\n marks end of headers)
    size_t headerEnd = client->requestBuffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        // Headers not complete yet, wait for more data
        return;
    }
    
    // Extract headers
    std::string headers = client->requestBuffer.substr(0, headerEnd);
    size_t bodyStart = headerEnd + 4;  // Skip \r\n\r\n
    
    // Parse request line (first line)
    std::istringstream iss(headers);
    std::string method, path, version;
    iss >> method >> path >> version;
    
    std::cout << "Request: " << method << " " << path << " " << version << std::endl;
    
    // Identify method using switch (convert to enum-like)
    int methodType = 0;  // 0=unknown, 1=GET, 2=POST, 3=DELETE
    if (method == "GET") methodType = 1;
    else if (method == "POST") methodType = 2;
    else if (method == "DELETE") methodType = 3;
    
    switch (methodType) {
        case 1:  // GET
            handleGetRequest(client, path);
            break;
            
        case 2:  // POST
            handlePostRequest(client, path, headers, bodyStart);
            break;
            
        case 3:  // DELETE
            handleDeleteRequest(client, path);
            break;
            
        default:  // Unknown method
            client->responseBuffer = "HTTP/1.0 501 Not Implemented\r\n"
                                     "Content-Type: text/html\r\n"
                                     "Content-Length: 58\r\n"
                                     "\r\n"
                                     "<html><body><h1>501 Not Implemented</h1></body></html>";
            prepareResponse(client);
            break;
    }
}

void WebServer::handleClientWrite(int clientSocket) {
    // Find client connection
    ClientConnection* client = findClient(clientSocket);
    if (!client) {
        std::cerr << "Client not found: " << clientSocket << std::endl;
        return;
    }
    
    // Check if there's data to send
    if (client->responseBuffer.empty() || client->bytesSent >= client->responseBuffer.length()) {
        // Nothing to send, close connection (HTTP/1.0 behavior)
        closeClient(clientSocket);
        return;
    }
    
    // Send remaining data
    size_t remaining = client->responseBuffer.length() - client->bytesSent;
    ssize_t sent = send(clientSocket, 
                       client->responseBuffer.c_str() + client->bytesSent, 
                       remaining, 
                       0);
    
    // Check return value only (no errno checks)
    if (sent < 0) {
        // Error occurred
        closeClient(clientSocket);
        return;
    }
    
    client->bytesSent += sent;
    
    // Check if all data has been sent
    if (client->bytesSent >= client->responseBuffer.length()) {
        // Response complete, close connection (HTTP/1.0 default)
        std::cout << "Response sent completely to socket " << clientSocket << std::endl;
        closeClient(clientSocket);
    }
    // If not complete, EPOLLOUT will trigger again when socket is ready
}

// Helper function to find client by fd
ClientConnection* WebServer::findClient(int fd) {
    for (size_t i = 0; i < clients.size(); ++i) {
        if (clients[i]->fd == fd) {
            return clients[i];
        }
    }
    return NULL;
}

// Prepare response for sending by adding EPOLLOUT to epoll
void WebServer::prepareResponse(ClientConnection* client) {
    // Modify epoll to monitor for write events
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLRDHUP;
    ev.data.fd = client->fd;
    
    if (epoll_ctl(epollFd, EPOLL_CTL_MOD, client->fd, &ev) < 0) {
        std::cerr << "Failed to modify epoll for writing: " << strerror(errno) << std::endl;
        closeClient(client->fd);
    }
}

// Handle GET request
void WebServer::handleGetRequest(ClientConnection* client, const std::string& path) {
    // Determine the file path
    std::string requestPath = path;
    
    // If path is just "/", use index file
    if (requestPath == "/") {
        requestPath = "/" + config.getIndex();
    }
    
    // Build full file path
    std::string fullPath = config.getRoot() + requestPath;
    
    // Try to open the file
    std::ifstream file(fullPath.c_str(), std::ios::binary);
    
    if (!file.is_open()) {
        // File not found - 404 response
        std::string content = "<html><body><h1>404 Not Found</h1><p>The requested resource was not found.</p></body></html>";
        std::ostringstream oss;
        oss << "HTTP/1.0 404 Not Found\r\n"
            << "Content-Type: text/html\r\n"
            << "Content-Length: " << content.length() << "\r\n"
            << "\r\n"
            << content;
        client->responseBuffer = oss.str();
        prepareResponse(client);
        return;
    }
    
    // Read file content
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    // Determine Content-Type based on file extension
    std::string contentType = "text/html";
    size_t dotPos = fullPath.find_last_of('.');
    if (dotPos != std::string::npos) {
        std::string extension = fullPath.substr(dotPos);
        if (extension == ".html" || extension == ".htm") {
            contentType = "text/html";
        } else if (extension == ".css") {
            contentType = "text/css";
        } else if (extension == ".js") {
            contentType = "application/javascript";
        } else if (extension == ".jpg" || extension == ".jpeg") {
            contentType = "image/jpeg";
        } else if (extension == ".png") {
            contentType = "image/png";
        } else if (extension == ".gif") {
            contentType = "image/gif";
        } else if (extension == ".txt") {
            contentType = "text/plain";
        } else if (extension == ".json") {
            contentType = "application/json";
        } else if (extension == ".xml") {
            contentType = "application/xml";
        } else {
            contentType = "application/octet-stream";
        }
    }
    
    // Generate HTTP 200 OK response
    std::ostringstream oss;
    oss << "HTTP/1.0 200 OK\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << content.length() << "\r\n"
        << "\r\n"
        << content;
    
    client->responseBuffer = oss.str();
    prepareResponse(client);
}

// Handle POST request
void WebServer::handlePostRequest(ClientConnection* client, const std::string& path, 
                                  const std::string& headers, size_t bodyStart) {
    (void)path;  // Suppress unused parameter warning
    
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
        client->responseBuffer = "HTTP/1.0 200 OK\r\n"
                                 "Content-Type: text/html\r\n"
                                 "Content-Length: 51\r\n"
                                 "\r\n"
                                 "<html><body><h1>POST received</h1></body></html>";
        prepareResponse(client);
        return;
    }
    
    // No Content-Length header
    client->responseBuffer = "HTTP/1.0 411 Length Required\r\n"
                             "Content-Type: text/html\r\n"
                             "Content-Length: 57\r\n"
                             "\r\n"
                             "<html><body><h1>411 Length Required</h1></body></html>";
    prepareResponse(client);
}

// Handle DELETE request
void WebServer::handleDeleteRequest(ClientConnection* client, const std::string& path) {
    (void)path;  // Suppress unused parameter warning
    
    // TODO: Implement DELETE functionality
    client->responseBuffer = "HTTP/1.0 200 OK\r\n"
                             "Content-Type: text/html\r\n"
                             "Content-Length: 54\r\n"
                             "\r\n"
                             "<html><body><h1>DELETE received</h1></body></html>";
    prepareResponse(client);
}

void WebServer::stop() {
    if (!running)
        return;
    running = false;
    
    if (serverSocket >= 0) {
        // ✅ Rimuovi server socket da epoll (non accettare nuove connessioni)
        epoll_ctl(epollFd, EPOLL_CTL_DEL, serverSocket, NULL);
        
        // ✅ Chiudi server socket (rifiuta nuove connessioni)
        close(serverSocket);
        serverSocket = -1;
        std::cout << "Server socket closed (no new connections)" << std::endl;
    }

    // Close all client sockets
    for (size_t i = 0; i < clients.size(); ++i) {
        epoll_ctl(epollFd, EPOLL_CTL_DEL, clients[i]->fd, NULL);
        close(clients[i]->fd);
        delete clients[i];
    }
    clients.clear();
    
    if (epollFd >= 0) {
        close(epollFd);
        epollFd = -1;
        std::cout << "Epoll instance closed" << std::endl;
    }
    
    std::cout << "Server shutdown complete" << std::endl;
}
