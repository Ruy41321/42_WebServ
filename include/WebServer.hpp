#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include <string>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <vector>
#include <map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "Config.hpp"
#include "ConnectionManager.hpp"
#include "HttpRequest.hpp"
#include "CgiHandler.hpp"

// Structure to hold server socket information
struct ServerSocket {
    int fd;                     // Socket file descriptor
    std::string host;           // Host/interface
    int port;                   // Port number
    size_t serverIndex;         // Index in config.servers[]
    
    ServerSocket() : fd(-1), port(0), serverIndex(0) {}
};

class WebServer {
private:
    Config config;
    std::vector<ServerSocket> serverSockets;  // Multiple server sockets
    std::map<int, size_t> fdToServerIndex;    // Map socket FD to server config index
    int epollFd;
    bool running;
    
    ConnectionManager* connManager;
    std::vector<HttpRequest*> httpHandlers;   // One handler per server config
    
    bool setupServerSocket(const ServerConfig& serverConfig, size_t index);
    void setupEpoll();
    void handleNewConnection(int serverFd);
    void handleClientRead(int clientSocket);
    void handleClientWrite(int clientSocket);
    bool isDuplicateBinding(const std::string& host, int port) const;
    
    // CGI handling
    void handleCgiPipeRead(int pipeFd);
    void handleCgiPipeWrite(int pipeFd);
    void checkCgiTimeouts();
    
public:
    WebServer();
    ~WebServer();
    
    bool initialize(const std::string& configFile);
    void run();
    void stop();
};

#endif
