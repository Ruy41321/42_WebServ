#ifndef CGIHANDLER_HPP
#define CGIHANDLER_HPP

#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#include "Config.hpp"
#include "ClientConnection.hpp"

// CGI Handler - Manages CGI script execution for the web server
// Follows CGI/1.1 specification for environment variables and I/O
class CgiHandler {
private:
    Config& config;
    
    // ==================== CGI Detection ====================
    // Check if request path matches a CGI extension in the location
    std::string getCgiExtension(const std::string& path, const LocationConfig* location);
    
    // Find the interpreter path for the given extension
    std::string findInterpreter(const std::string& extension, const LocationConfig* location);
    
    // ==================== Environment Setup ====================
    // Build CGI environment variables as char** for execve
    char** buildEnvironment(ClientConnection* client, const std::string& scriptPath,
                           const std::string& pathInfo, const std::string& queryString,
                           const std::string& method, const std::string& headers,
                           size_t contentLength);
    
    // Free environment array allocated by buildEnvironment
    void freeEnvironment(char** env);
    
    // Parse HTTP headers to extract specific header value
    std::string getHeaderValue(const std::string& headers, const std::string& headerName);
    
    // Convert HTTP headers to CGI HTTP_* environment variables
    std::vector<std::string> buildHttpHeaderVars(const std::string& headers);
    
    // ==================== Path Helpers ====================
    // Extract PATH_INFO from request (part after script name)
    std::string extractPathInfo(const std::string& path, const std::string& scriptPath);
    
    // Get the script's directory for chdir
    std::string getScriptDirectory(const std::string& scriptPath);
    
    // Split path and query string
    void splitPathAndQuery(const std::string& fullPath, std::string& path, std::string& query);
    
public:
    CgiHandler(Config& cfg);
    ~CgiHandler();
    
    // ==================== Main CGI Interface ====================
    
    // Check if this request should be handled as CGI
    // Returns true if the path matches a CGI extension in the location config
    bool isCgiRequest(const std::string& path, const LocationConfig* location);
    
    // Start CGI execution for a request
    // Creates pipes, forks, and sets up the CGI process
    // Returns true if CGI was started successfully
    // The client's CGI state fields will be set up for the event loop to manage
    bool startCgi(ClientConnection* client, const std::string& method,
                  const std::string& path, const std::string& headers,
                  const std::string& body, const LocationConfig* location,
                  const std::string& scriptFilePath);
    
    // Write request body to CGI stdin pipe (non-blocking)
    // Returns: >0 bytes written, 0 if done, -1 on error
    ssize_t writeToCgi(ClientConnection* client);
    
    // Read CGI output from stdout pipe (non-blocking)
    // Returns: >0 bytes read, 0 if EOF, -1 on error
    ssize_t readFromCgi(ClientConnection* client);
    
    // Parse CGI output and build HTTP response
    // Called when CGI output is complete (EOF or Content-Length reached)
    void buildResponse(ClientConnection* client);
    
    // Check if CGI process has timed out
    bool hasTimedOut(ClientConnection* client, int timeoutSeconds = 30);
    
    // Kill CGI process and clean up resources
    void killCgi(ClientConnection* client);
    
    // Clean up CGI resources (close pipes, reap child)
    void cleanup(ClientConnection* client);
    
    // Wait for CGI process to finish (non-blocking check)
    // Returns true if process has exited
    bool checkCgiComplete(ClientConnection* client);
    
    // ==================== CGI Timeout (configurable) ====================
    static const int DEFAULT_CGI_TIMEOUT = 30;  // seconds
};

#endif
