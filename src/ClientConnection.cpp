#include "../include/ClientConnection.hpp"
#include <unistd.h>

ClientConnection::ClientConnection(int socket, size_t servIdx) 
    : fd(socket), 
      serverIndex(servIdx), 
      state(READING_REQUEST),
      bytesSent(0),
      headersComplete(false),
      headerEndOffset(0),
      bodyBytesReceived(0),
      maxBodySize(0),
      cgiPid(-1),
      cgiInputFd(-1),
      cgiOutputFd(-1),
      cgiBodyOffset(0),
      cgiStartTime(0) {
}

ClientConnection::~ClientConnection() {
    // Clean up any open CGI pipes
    if (cgiInputFd >= 0) {
        close(cgiInputFd);
    }
    if (cgiOutputFd >= 0) {
        close(cgiOutputFd);
    }
}

void ClientConnection::clearBuffers() {
    requestBuffer.clear();
    responseBuffer.clear();
    bytesSent = 0;
    headersComplete = false;
    headerEndOffset = 0;
    bodyBytesReceived = 0;
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

void ClientConnection::resetCgiState() {
    if (cgiInputFd >= 0) {
        close(cgiInputFd);
        cgiInputFd = -1;
    }
    if (cgiOutputFd >= 0) {
        close(cgiOutputFd);
        cgiOutputFd = -1;
    }
    cgiPid = -1;
    cgiBody.clear();
    cgiBodyOffset = 0;
    cgiOutputBuffer.clear();
    cgiScriptName.clear();
    cgiStartTime = 0;
}

bool ClientConnection::isCgiActive() const {
    return cgiPid > 0 || cgiInputFd >= 0 || cgiOutputFd >= 0;
}
