#include "../include/ConnectionManager.hpp"
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cerrno>

ConnectionManager::ConnectionManager(int epoll_fd) : epollFd(epoll_fd) {
}

ConnectionManager::~ConnectionManager() {
    closeAllClients();
}

void ConnectionManager::addClient(int clientSocket, size_t serverIndex) {
    ClientConnection* client = new ClientConnection(clientSocket, serverIndex);
    clients.push_back(client);
}

void ConnectionManager::removeClient(int clientSocket) {
    // Find the client first to cleanup CGI resources
    ClientConnection* client = findClient(clientSocket);
    if (client) {
        // Remove CGI pipes from epoll and mapping
        removeCgiPipes(client);
    }
    
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

ClientConnection* ConnectionManager::findClient(int fd) {
    for (size_t i = 0; i < clients.size(); ++i) {
        if (clients[i]->fd == fd) {
            return clients[i];
        }
    }
    return NULL;
}

void ConnectionManager::closeAllClients() {
    for (size_t i = 0; i < clients.size(); ++i) {
        // Clean up CGI resources first
        removeCgiPipes(clients[i]);
        
        epoll_ctl(epollFd, EPOLL_CTL_DEL, clients[i]->fd, NULL);
        close(clients[i]->fd);
        delete clients[i];
    }
    clients.clear();
    cgiPipeToClient.clear();
}

void ConnectionManager::prepareResponseMode(ClientConnection* client) {
    // Modify epoll to monitor for write events
    struct epoll_event ev;
    ev.events = EPOLLOUT | EPOLLRDHUP;
    ev.data.fd = client->fd;
    
    if (epoll_ctl(epollFd, EPOLL_CTL_MOD, client->fd, &ev) < 0) {
        std::cerr << "Failed to modify epoll for writing: " << strerror(errno) << std::endl;
        removeClient(client->fd);
    }
}

// ==================== CGI Pipe Management ====================

void ConnectionManager::addCgiPipes(ClientConnection* client) {
    // Add CGI input pipe (for writing) to epoll
    if (client->cgiInputFd >= 0) {
        struct epoll_event ev;
        ev.events = EPOLLOUT;  // Writing to CGI stdin
        ev.data.fd = client->cgiInputFd;
        
        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, client->cgiInputFd, &ev) < 0) {
            std::cerr << "Failed to add CGI input pipe to epoll: " << strerror(errno) << std::endl;
        } else {
            cgiPipeToClient[client->cgiInputFd] = client;
        }
    }
    
    // Add CGI output pipe (for reading) to epoll
    if (client->cgiOutputFd >= 0) {
        struct epoll_event ev;
        ev.events = EPOLLIN;  // Reading from CGI stdout
        ev.data.fd = client->cgiOutputFd;
        
        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, client->cgiOutputFd, &ev) < 0) {
            std::cerr << "Failed to add CGI output pipe to epoll: " << strerror(errno) << std::endl;
        } else {
            cgiPipeToClient[client->cgiOutputFd] = client;
        }
    }
}

void ConnectionManager::removeCgiPipes(ClientConnection* client) {
    // Remove and close CGI input pipe
    if (client->cgiInputFd >= 0) {
        epoll_ctl(epollFd, EPOLL_CTL_DEL, client->cgiInputFd, NULL);
        cgiPipeToClient.erase(client->cgiInputFd);
        close(client->cgiInputFd);
        client->cgiInputFd = -1;
    }
    
    // Remove and close CGI output pipe
    if (client->cgiOutputFd >= 0) {
        epoll_ctl(epollFd, EPOLL_CTL_DEL, client->cgiOutputFd, NULL);
        cgiPipeToClient.erase(client->cgiOutputFd);
        close(client->cgiOutputFd);
        client->cgiOutputFd = -1;
    }
}

ClientConnection* ConnectionManager::findClientByCgiPipe(int pipeFd) {
    std::map<int, ClientConnection*>::iterator it = cgiPipeToClient.find(pipeFd);
    if (it != cgiPipeToClient.end()) {
        return it->second;
    }
    return NULL;
}

bool ConnectionManager::isCgiPipe(int fd) {
    return cgiPipeToClient.find(fd) != cgiPipeToClient.end();
}

std::vector<ClientConnection*>& ConnectionManager::getClients() {
    return clients;
}
