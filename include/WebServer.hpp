#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include <string>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "Config.hpp"
#include "ConnectionManager.hpp"
#include "HttpRequest.hpp"

class WebServer {
private:
    Config config;
    int serverSocket;
    int epollFd;
    struct sockaddr_in serverAddr;
    bool running;
    
    ConnectionManager* connManager;
    HttpRequest* httpHandler;
    
    void setupSocket();
    void setupEpoll();
    void handleNewConnection();
    void handleClientRead(int clientSocket);
    void handleClientWrite(int clientSocket);
    
public:
    WebServer();
    ~WebServer();
    
    bool initialize(const std::string& configFile);
    void run();
    void stop();
};

#endif
