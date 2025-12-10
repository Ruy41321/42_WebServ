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

struct ServerSocket {
    int fd;
    std::string host;
    int port;
    size_t serverIndex;
    
    ServerSocket() : fd(-1), port(0), serverIndex(0) {}
};

class WebServer {
private:
    Config config;
    std::vector<ServerSocket> serverSockets;
    std::map<int, size_t> fdToServerIndex;
    int epollFd;
    bool running;
    
    ConnectionManager* connManager;
    std::vector<HttpRequest*> httpHandlers;
    
    bool setupServerSocket(const ServerConfig& serverConfig, size_t index);
    void setupEpoll();
    bool isDuplicateBinding(const std::string& host, int port) const;
    void cleanupOnError();
    
    bool setNonBlocking(int fd);
    bool addToEpoll(int fd, uint32_t events);
    bool isServerSocket(int fd);
    
    void processEvents(struct epoll_event* events, int numEvents);
    void handleNewConnection(int serverFd);
    void handleClientRead(int clientSocket);
    void handleClientWrite(int clientSocket);
    void handleErrorEvent(int fd);
    void handleClientEvent(int fd, uint32_t activeEvents);
    void handleCgiPipeEvent(int fd, uint32_t activeEvents);
    
    bool parseHeaders(ClientConnection* client, size_t oldBufferSize);
    void determineMaxBodySize(ClientConnection* client);
    std::string extractRequestPath(ClientConnection* client);
    bool isValidPathMatch(const std::string& requestPath, const std::string& locPath);
    bool checkContentLengthHeader(ClientConnection* client);
    bool checkBodySize(ClientConnection* client);
    bool waitForCompleteBody(ClientConnection* client);
    std::string extractMethod(const std::string& headers);
    void processRequest(ClientConnection* client);
    
    bool shouldKeepAlive(ClientConnection* client);
    void prepareForNextRequest(ClientConnection* client, int clientSocket);
    
    void handleCgiPipeRead(int pipeFd);
    void handleCgiPipeWrite(int pipeFd);
    void completeCgiRequest(ClientConnection* client, int fd);
    void checkCgiTimeouts();
    
public:
    WebServer();
    ~WebServer();
    
    bool initialize(const std::string& configFile);
    void run();
    void stop();
};

#endif
