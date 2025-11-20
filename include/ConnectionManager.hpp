#ifndef CONNECTIONMANAGER_HPP
#define CONNECTIONMANAGER_HPP

#include <vector>
#include <sys/epoll.h>
#include "ClientConnection.hpp"

class ConnectionManager {
private:
    std::vector<ClientConnection*> clients;
    int epollFd;
    
public:
    ConnectionManager(int epoll_fd);
    ~ConnectionManager();
    
    void addClient(int clientSocket, size_t serverIndex);
    void removeClient(int clientSocket);
    ClientConnection* findClient(int fd);
    void closeAllClients();
    
    void prepareResponseMode(ClientConnection* client);
};

#endif
