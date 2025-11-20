#ifndef CLIENTCONNECTION_HPP
#define CLIENTCONNECTION_HPP

#include <string>

// Structure to hold client connection state
class ClientConnection {
public:
    int fd;
    size_t serverIndex;         // Which server config this client belongs to
    std::string requestBuffer;
    std::string responseBuffer;
    size_t bytesSent;           // Track how many bytes of response have been sent
    
    ClientConnection(int socket, size_t servIdx = 0);
    ~ClientConnection();
    
    void clearBuffers();
    bool isResponseComplete() const;
    size_t getRemainingBytes() const;
};

#endif
