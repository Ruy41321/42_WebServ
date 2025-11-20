#include "../include/ClientConnection.hpp"

ClientConnection::ClientConnection(int socket, size_t servIdx) 
    : fd(socket), serverIndex(servIdx), bytesSent(0) {
}

ClientConnection::~ClientConnection() {
}

void ClientConnection::clearBuffers() {
    requestBuffer.clear();
    responseBuffer.clear();
    bytesSent = 0;
}

bool ClientConnection::isResponseComplete() const {
    return bytesSent >= responseBuffer.length();
}

size_t ClientConnection::getRemainingBytes() const {
    if (bytesSent >= responseBuffer.length()) {
        return 0;
    }
    return responseBuffer.length() - bytesSent;
}
