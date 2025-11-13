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
#include <sys/select.h>
#include "Config.hpp"

class WebServer {
private:
    Config config;
    int serverSocket;
    struct sockaddr_in serverAddr;
    bool running;
    
    void setupSocket();
    void handleClient(int clientSocket);
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
