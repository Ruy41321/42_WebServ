#ifndef CLIENTCONNECTION_HPP
#define CLIENTCONNECTION_HPP

#include <string>
#include <ctime>
#include <sys/types.h>

// Structure to hold client connection state
class ClientConnection {
public:
    // Connection state enum
    enum State {
        READING_REQUEST,     // Reading HTTP request from client
        CGI_RUNNING,         // CGI process is running
        SENDING_RESPONSE     // Sending response to client
    };
    
    // Basic connection info
    int fd;
    size_t serverIndex;         // Which server config this client belongs to
    State state;                // Current connection state
    
    // Request/Response buffers
    std::string requestBuffer;
    std::string responseBuffer;
    size_t bytesSent;           // Track how many bytes of response have been sent
    
    // Progressive body size tracking
    bool headersComplete;       // True when we've received all headers
    size_t headerEndOffset;     // Position where headers end (after \r\n\r\n)
    size_t bodyBytesReceived;   // Actual body bytes received so far
    size_t maxBodySize;         // Max allowed body size for this server
    
    // CGI state fields
    pid_t cgiPid;               // CGI child process ID (-1 if not running)
    int cgiInputFd;             // Pipe to write to CGI stdin (-1 if closed)
    int cgiOutputFd;            // Pipe to read from CGI stdout (-1 if closed)
    std::string cgiBody;        // Request body to send to CGI
    size_t cgiBodyOffset;       // Offset of bytes already sent to CGI
    std::string cgiOutputBuffer; // Accumulated CGI output
    std::string cgiScriptName;  // Script name for SCRIPT_NAME env var
    time_t cgiStartTime;        // When CGI was started (for timeout)
    
    ClientConnection(int socket, size_t servIdx = 0);
    ~ClientConnection();
    
    void clearBuffers();
    bool isResponseComplete() const;
    size_t getRemainingBytes() const;
    
    // CGI helpers
    void resetCgiState();
    bool isCgiActive() const;
};

#endif
