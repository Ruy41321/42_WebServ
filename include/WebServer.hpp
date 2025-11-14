#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "Config.hpp"

// Structure to hold client connection state
struct ClientConnection {
    int fd;
    std::string requestBuffer;
    std::string responseBuffer;
    size_t bytesSent;  // Track how many bytes of response have been sent
    
    ClientConnection(int socket) : fd(socket), bytesSent(0) {}
};

class WebServer {
private:
    Config config;
    int serverSocket;
    int epollFd;
    struct sockaddr_in serverAddr;
    bool running;
    std::vector<ClientConnection*> clients;  // All active client connections
    
    void setupSocket();
    void setupEpoll();
    void handleNewConnection();
    void closeClient(int clientSocket);
    void handleClientRead(int clientSocket);
    void handleClientWrite(int clientSocket);
    
    // Helper methods
    ClientConnection* findClient(int fd);
    void handleGetRequest(ClientConnection* client, const std::string& path);
    void handlePostRequest(ClientConnection* client, const std::string& path, const std::string& headers, size_t bodyStart);
    void handleDeleteRequest(ClientConnection* client, const std::string& path);
    void prepareResponse(ClientConnection* client);
    
    std::string parseRequest(const std::string& request);
    std::string generateResponse(const std::string& path);
    
public:
    WebServer();
    ~WebServer();
    
    bool initialize(const std::string& configFile);
    void run();
    void stop();
};

#endif
