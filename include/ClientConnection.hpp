#ifndef CLIENTCONNECTION_HPP
#define CLIENTCONNECTION_HPP

#include <string>

// Structure to hold client connection state
class ClientConnection {
public:
    int fd;
    std::string requestBuffer;
    std::string responseBuffer;
    size_t bytesSent;  // Track how many bytes of response have been sent
    
    ClientConnection(int socket);
    ~ClientConnection();
    
    void clearBuffers();
    bool isResponseComplete() const;
    size_t getRemainingBytes() const;
};

#endif
