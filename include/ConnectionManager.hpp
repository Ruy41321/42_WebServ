#ifndef CONNECTIONMANAGER_HPP
#define CONNECTIONMANAGER_HPP

#include <vector>
#include <map>
#include <sys/epoll.h>
#include "ClientConnection.hpp"

class ConnectionManager {
private:
    std::vector<ClientConnection*> clients;
    std::map<int, ClientConnection*> cgiPipeToClient;  // Map CGI pipe FDs to client
    int epollFd;
    
public:
    ConnectionManager(int epoll_fd);
    ~ConnectionManager();
    
    void addClient(int clientSocket, size_t serverIndex);
    void removeClient(int clientSocket);
    ClientConnection* findClient(int fd);
    void closeAllClients();
    
    void prepareResponseMode(ClientConnection* client);
    
    // CGI pipe management
    void addCgiPipes(ClientConnection* client);
    void removeCgiPipes(ClientConnection* client);
    void removeSingleCgiPipe(int pipeFd);  // Remove just one pipe from map (for partial cleanup)
    ClientConnection* findClientByCgiPipe(int pipeFd);
    bool isCgiPipe(int fd);
    
    // Get all clients (for timeout checking)
    std::vector<ClientConnection*>& getClients();
};

#endif
