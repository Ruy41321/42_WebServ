#include "../include/WebServer.hpp"

WebServer::WebServer() : serverSocket(-1), running(false) {
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
    
    // Listen for connections
    if (listen(serverSocket, 10) < 0) {
        close(serverSocket);
        throw std::runtime_error("Failed to listen on socket");
    }
    
    std::cout << "Server listening on " << config.getHost() << ":" << config.getPort() << std::endl;
}

void WebServer::run() {
    running = true;
    
    while (running) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        
        // Accept incoming connection
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Error accepting connection: " << strerror(errno) << std::endl;
            continue;
        }
        
        // Handle client request
        handleClient(clientSocket);
        
        // Close client socket
        close(clientSocket);
    }
}

void WebServer::handleClient(int clientSocket) {
    char buffer[4096];
    std::memset(buffer, 0, sizeof(buffer));
    
    // Read request
    ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0) {
        return;
    }
    
    std::string request(buffer);
    std::string path = parseRequest(request);
    std::string response = generateResponse(path);
    
    // Send response
    send(clientSocket, response.c_str(), response.length(), 0);
}

std::string WebServer::parseRequest(const std::string& request) {
    // Parse the first line of the HTTP request
    std::istringstream iss(request);
    std::string method, path, version;
    iss >> method >> path >> version;
    
    // If path is just "/", use index file
    if (path == "/") {
        path = "/" + config.getIndex();
    }
    
    return path;
}

std::string WebServer::generateResponse(const std::string& path) {
    std::string fullPath = config.getRoot() + path;
    std::ifstream file(fullPath.c_str(), std::ios::binary);
    
    std::string response;
    
    if (file.is_open()) {
        // Read file content
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();
        
        // Generate HTTP response
        response = "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: text/html\r\n";
        response += "Content-Length: " + std::string(static_cast<std::ostringstream*>( &(std::ostringstream() << content.length()) )->str()) + "\r\n";
        response += "\r\n";
        response += content;
    } else {
        // File not found
        std::string content = "<html><body><h1>404 Not Found</h1></body></html>";
        response = "HTTP/1.1 404 Not Found\r\n";
        response += "Content-Type: text/html\r\n";
        response += "Content-Length: " + std::string(static_cast<std::ostringstream*>( &(std::ostringstream() << content.length()) )->str()) + "\r\n";
        response += "\r\n";
        response += content;
    }
    
    return response;
}

void WebServer::stop() {
    running = false;
    if (serverSocket >= 0) {
        close(serverSocket);
        serverSocket = -1;
    }
}
