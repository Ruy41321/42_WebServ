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

void ConnectionManager::addClient(int clientSocket) {
    ClientConnection* client = new ClientConnection(clientSocket);
    clients.push_back(client);
}

void ConnectionManager::removeClient(int clientSocket) {
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
        epoll_ctl(epollFd, EPOLL_CTL_DEL, clients[i]->fd, NULL);
        close(clients[i]->fd);
        delete clients[i];
    }
    clients.clear();
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
