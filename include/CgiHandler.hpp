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

class CgiHandler {
private:
    Config& config;
    
    std::string getCgiExtension(const std::string& path, const LocationConfig* location);
    std::string findInterpreter(const std::string& extension, const LocationConfig* location);
    
    char** buildEnvironment(ClientConnection* client, const std::string& scriptPath,
                           const std::string& pathInfo, const std::string& queryString,
                           const std::string& method, const std::string& headers,
                           size_t contentLength);
    void freeEnvironment(char** env);
    void addServerEnvVars(std::vector<std::string>& envVars, const ServerConfig& serverConfig);
    void addRequestEnvVars(std::vector<std::string>& envVars, ClientConnection* client,
                           const std::string& method, const std::string& absScriptPath,
                           const std::string& pathInfo, const std::string& queryString,
                           const std::string& headers, size_t contentLength);
    
    std::string getHeaderValue(const std::string& headers, const std::string& headerName);
    std::string convertHeaderToEnvName(const std::string& headerName);
    std::vector<std::string> buildHttpHeaderVars(const std::string& headers);
    
    std::string extractPathInfo(const std::string& path, const std::string& scriptPath);
    std::string getScriptDirectory(const std::string& scriptPath);
    void splitPathAndQuery(const std::string& fullPath, std::string& path, std::string& query);
    
    bool createPipes(int inputPipe[2], int outputPipe[2]);
    void setupChildProcess(int inputPipe[2], int outputPipe[2], const std::string& scriptFilePath);
    void setupParentProcess(ClientConnection* client, int inputPipe[2], int outputPipe[2],
                           pid_t pid, const std::string& body);
    bool isStandaloneCgi(const std::string& interpreter);
    void executeChild(const std::string& interpreter, const std::string& scriptFilePath, char** env);
    bool validateCgiSetup(const std::string& path, const LocationConfig* location,
                         const std::string& scriptFilePath, std::string& interpreter);
    void setScriptName(ClientConnection* client, const std::string& cleanPath);
    
    void parseCgiHeader(const std::string& line, int& statusCode, std::string& statusText,
                       std::string& contentType, std::string& location, std::string& additionalHeaders);
    
public:
    CgiHandler(Config& cfg);
    ~CgiHandler();
    
    static const int DEFAULT_CGI_TIMEOUT = 30;
    
    bool isCgiRequest(const std::string& path, const LocationConfig* location);
    bool startCgi(ClientConnection* client, const std::string& method,
                  const std::string& path, const std::string& headers,
                  const std::string& body, const LocationConfig* location,
                  const std::string& scriptFilePath);
    
    ssize_t writeToCgi(ClientConnection* client);
    ssize_t readFromCgi(ClientConnection* client);
    void buildResponse(ClientConnection* client);
    bool hasTimedOut(ClientConnection* client, int timeoutSeconds = 30);
    void killCgi(ClientConnection* client);
    void cleanup(ClientConnection* client);
    bool checkCgiComplete(ClientConnection* client);
};

#endif
