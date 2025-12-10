#include "../include/CgiHandler.hpp"
#include "../include/HttpResponse.hpp"
#include "../include/StringUtils.hpp"
#include <sstream>
#include <iostream>
#include <ctime>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

CgiHandler::CgiHandler(Config& cfg) : config(cfg) {}

CgiHandler::~CgiHandler() {}

std::string CgiHandler::getCgiExtension(const std::string& path, const LocationConfig* location) {
    if (!location || location->cgiExt.empty())
        return "";
    
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos)
        return "";
    
    std::string extension;
    size_t slashAfterDot = path.find('/', dotPos);
    if (slashAfterDot != std::string::npos) {
        extension = path.substr(dotPos, slashAfterDot - dotPos);
    } else {
        size_t queryPos = path.find('?', dotPos);
        extension = (queryPos != std::string::npos) 
            ? path.substr(dotPos, queryPos - dotPos) 
            : path.substr(dotPos);
    }
    
    for (size_t i = 0; i < location->cgiExt.size(); ++i) {
        if (extension == location->cgiExt[i])
            return extension;
    }
    return "";
}

std::string CgiHandler::findInterpreter(const std::string& extension, const LocationConfig* location) {
    if (!location || location->cgiPath.empty() || location->cgiExt.empty())
        return "";
    
    for (size_t i = 0; i < location->cgiExt.size(); ++i) {
        if (location->cgiExt[i] == extension) {
            return (i < location->cgiPath.size()) ? location->cgiPath[i] : location->cgiPath.back();
        }
    }
    return "";
}

bool CgiHandler::isCgiRequest(const std::string& path, const LocationConfig* location) {
    return !getCgiExtension(path, location).empty();
}

std::string CgiHandler::getHeaderValue(const std::string& headers, const std::string& headerName) {
    std::string searchKey = headerName + ":";
    std::string headersLower = StringUtils::toLower(headers);
    std::string keyLower = StringUtils::toLower(searchKey);
    
    size_t pos = headersLower.find(keyLower);
    if (pos == std::string::npos)
        return "";
    
    size_t valueStart = pos + searchKey.length();
    while (valueStart < headers.size() && (headers[valueStart] == ' ' || headers[valueStart] == '\t'))
        valueStart++;
    
    size_t valueEnd = headers.find("\r\n", valueStart);
    if (valueEnd == std::string::npos)
        valueEnd = headers.find("\n", valueStart);
    if (valueEnd == std::string::npos)
        valueEnd = headers.length();
    
    return headers.substr(valueStart, valueEnd - valueStart);
}

std::string CgiHandler::convertHeaderToEnvName(const std::string& headerName) {
    std::string envName = "HTTP_";
    for (size_t i = 0; i < headerName.size(); ++i) {
        char c = headerName[i];
        if (c == '-')
            envName += '_';
        else if (c >= 'a' && c <= 'z')
            envName += (char)(c - 32);
        else
            envName += c;
    }
    return envName;
}

std::vector<std::string> CgiHandler::buildHttpHeaderVars(const std::string& headers) {
    std::vector<std::string> vars;
    size_t pos = 0;
    
    size_t lineEnd = headers.find("\r\n", pos);
    if (lineEnd != std::string::npos)
        pos = lineEnd + 2;
    
    while (pos < headers.length()) {
        lineEnd = headers.find("\r\n", pos);
        if (lineEnd == std::string::npos) {
            lineEnd = headers.find("\n", pos);
            if (lineEnd == std::string::npos)
                break;
        }
        
        std::string line = headers.substr(pos, lineEnd - pos);
        if (line.empty())
            break;
        
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string name = line.substr(0, colonPos);
            std::string value = StringUtils::trim(line.substr(colonPos + 1));
            
            std::string nameLower = StringUtils::toLower(name);
            if (nameLower != "content-type" && nameLower != "content-length") {
                vars.push_back(convertHeaderToEnvName(name) + "=" + value);
            }
        }
        pos = lineEnd + 2;
    }
    return vars;
}

void CgiHandler::splitPathAndQuery(const std::string& fullPath, std::string& path, std::string& query) {
    size_t queryPos = fullPath.find('?');
    if (queryPos != std::string::npos) {
        path = fullPath.substr(0, queryPos);
        query = fullPath.substr(queryPos + 1);
    } else {
        path = fullPath;
        query = "";
    }
}

std::string CgiHandler::extractPathInfo(const std::string& path, const std::string& scriptPath) {
    (void)scriptPath;
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos)
        return "";
    
    size_t slashAfterScript = path.find('/', dotPos);
    return (slashAfterScript != std::string::npos) ? path.substr(slashAfterScript) : "";
}

std::string CgiHandler::getScriptDirectory(const std::string& scriptPath) {
    size_t lastSlash = scriptPath.rfind('/');
    return (lastSlash != std::string::npos) ? scriptPath.substr(0, lastSlash) : ".";
}

void CgiHandler::addServerEnvVars(std::vector<std::string>& envVars, const ServerConfig& serverConfig) {
    envVars.push_back("GATEWAY_INTERFACE=CGI/1.1");
    envVars.push_back("SERVER_PROTOCOL=HTTP/1.1");
    envVars.push_back("SERVER_SOFTWARE=WebServ/1.0");
    envVars.push_back("SERVER_NAME=" + serverConfig.host);
    envVars.push_back("SERVER_PORT=" + StringUtils::intToString(serverConfig.port));
    envVars.push_back("DOCUMENT_ROOT=" + serverConfig.root);
}

void CgiHandler::addRequestEnvVars(std::vector<std::string>& envVars, ClientConnection* client,
                                   const std::string& method, const std::string& absScriptPath,
                                   const std::string& pathInfo, const std::string& queryString,
                                   const std::string& headers, size_t contentLength) {
    envVars.push_back("REQUEST_METHOD=" + method);
    envVars.push_back("SCRIPT_NAME=" + client->cgiScriptName);
    envVars.push_back("SCRIPT_FILENAME=" + absScriptPath);
    
    if (pathInfo.empty())
        envVars.push_back("PATH_INFO=" + client->cgiScriptName);
    else
        envVars.push_back("PATH_INFO=" + pathInfo);
    
    envVars.push_back("QUERY_STRING=" + queryString);
    
    std::string requestUri = client->cgiScriptName;
    if (!pathInfo.empty())
        requestUri += pathInfo;
    if (!queryString.empty())
        requestUri += "?" + queryString;
    envVars.push_back("REQUEST_URI=" + requestUri);
    
    if (contentLength > 0)
        envVars.push_back("CONTENT_LENGTH=" + StringUtils::sizeToString(contentLength));
    
    std::string contentType = getHeaderValue(headers, "Content-Type");
    if (!contentType.empty())
        envVars.push_back("CONTENT_TYPE=" + contentType);
    
    envVars.push_back("REMOTE_ADDR=127.0.0.1");
    envVars.push_back("REMOTE_HOST=localhost");
    envVars.push_back("REDIRECT_STATUS=200");
}

char** CgiHandler::buildEnvironment(ClientConnection* client, const std::string& scriptPath,
                                    const std::string& pathInfo, const std::string& queryString,
                                    const std::string& method, const std::string& headers,
                                    size_t contentLength) {
    std::vector<std::string> envVars;
    const ServerConfig& serverConfig = config.getServer(client->serverIndex);
    
    char absolutePath[PATH_MAX];
    std::string absScriptPath = scriptPath;
    if (realpath(scriptPath.c_str(), absolutePath) != NULL)
        absScriptPath = absolutePath;
    
    addServerEnvVars(envVars, serverConfig);
    addRequestEnvVars(envVars, client, method, absScriptPath, pathInfo, queryString, headers, contentLength);
    
    if (!pathInfo.empty())
        envVars.push_back("PATH_TRANSLATED=" + serverConfig.root + pathInfo);
    
    std::vector<std::string> httpVars = buildHttpHeaderVars(headers);
    for (size_t i = 0; i < httpVars.size(); ++i)
        envVars.push_back(httpVars[i]);
    
    char** env = new char*[envVars.size() + 1];
    for (size_t i = 0; i < envVars.size(); ++i) {
        env[i] = new char[envVars[i].size() + 1];
        std::strcpy(env[i], envVars[i].c_str());
    }
    env[envVars.size()] = NULL;
    return env;
}

void CgiHandler::freeEnvironment(char** env) {
    if (!env)
        return;
    for (size_t i = 0; env[i] != NULL; ++i)
        delete[] env[i];
    delete[] env;
}

bool CgiHandler::createPipes(int inputPipe[2], int outputPipe[2]) {
    if (pipe(inputPipe) < 0) {
        std::cerr << "CGI: Failed to create input pipe" << std::endl;
        return false;
    }
    if (pipe(outputPipe) < 0) {
        std::cerr << "CGI: Failed to create output pipe" << std::endl;
        close(inputPipe[0]);
        close(inputPipe[1]);
        return false;
    }
    return true;
}

void CgiHandler::setupChildProcess(int inputPipe[2], int outputPipe[2], const std::string& scriptFilePath) {
    close(inputPipe[1]);
    close(outputPipe[0]);
    
    if (dup2(inputPipe[0], STDIN_FILENO) < 0) {
        std::cerr << "CGI: dup2 stdin failed" << std::endl;
        _exit(1);
    }
    close(inputPipe[0]);
    
    if (dup2(outputPipe[1], STDOUT_FILENO) < 0) {
        std::cerr << "CGI: dup2 stdout failed" << std::endl;
        _exit(1);
    }
    close(outputPipe[1]);
    
    std::string scriptDir = getScriptDirectory(scriptFilePath);
    if (chdir(scriptDir.c_str()) < 0)
        std::cerr << "CGI: chdir failed to " << scriptDir << std::endl;
}

bool CgiHandler::isStandaloneCgi(const std::string& interpreter) {
    return interpreter.find("php") == std::string::npos &&
           interpreter.find("python") == std::string::npos &&
           interpreter.find("perl") == std::string::npos &&
           interpreter.find("ruby") == std::string::npos;
}

void CgiHandler::executeChild(const std::string& interpreter, const std::string& scriptFilePath, char** env) {
    std::string scriptName = scriptFilePath;
    size_t lastSlash = scriptName.rfind('/');
    if (lastSlash != std::string::npos)
        scriptName = scriptName.substr(lastSlash + 1);
    
    char* argv[3];
    argv[0] = const_cast<char*>(interpreter.c_str());
    
    if (isStandaloneCgi(interpreter)) {
        argv[1] = NULL;
    } else {
        argv[1] = const_cast<char*>(scriptName.c_str());
        argv[2] = NULL;
    }
    
    execve(interpreter.c_str(), argv, env);
    std::cerr << "CGI: execve failed for " << interpreter << std::endl;
    _exit(1);
}

void CgiHandler::setupParentProcess(ClientConnection* client, int inputPipe[2], int outputPipe[2],
                                    pid_t pid, const std::string& body) {
    close(inputPipe[0]);
    close(outputPipe[1]);
    
    int flags = fcntl(inputPipe[1], F_GETFL, 0);
    if (flags >= 0)
        fcntl(inputPipe[1], F_SETFL, flags | O_NONBLOCK);
    
    flags = fcntl(outputPipe[0], F_GETFL, 0);
    if (flags >= 0)
        fcntl(outputPipe[0], F_SETFL, flags | O_NONBLOCK);
    
    client->cgiPid = pid;
    client->cgiInputFd = inputPipe[1];
    client->cgiOutputFd = outputPipe[0];
    client->cgiBody = body;
    client->cgiBodyOffset = 0;
    client->cgiOutputBuffer.clear();
    client->cgiStartTime = std::time(NULL);
    client->state = ClientConnection::CGI_RUNNING;
}

bool CgiHandler::validateCgiSetup(const std::string& path, const LocationConfig* location,
                                  const std::string& scriptFilePath, std::string& interpreter) {
    std::string extension = getCgiExtension(path, location);
    interpreter = findInterpreter(extension, location);
    
    if (interpreter.empty()) {
        std::cerr << "CGI: No interpreter found for extension: " << extension << std::endl;
        return false;
    }
    
    char interpreterAbsPath[PATH_MAX];
    if (realpath(interpreter.c_str(), interpreterAbsPath) != NULL)
        interpreter = interpreterAbsPath;
    
    if (access(interpreter.c_str(), X_OK) != 0) {
        std::cerr << "CGI: Interpreter not found or not executable: " << interpreter << std::endl;
        return false;
    }
    
    if (access(scriptFilePath.c_str(), F_OK) != 0) {
        std::cerr << "CGI: Script not found: " << scriptFilePath << std::endl;
        return false;
    }
    return true;
}

void CgiHandler::setScriptName(ClientConnection* client, const std::string& cleanPath) {
    size_t dotPos = cleanPath.rfind('.');
    if (dotPos != std::string::npos) {
        size_t slashAfterDot = cleanPath.find('/', dotPos);
        client->cgiScriptName = (slashAfterDot != std::string::npos) 
            ? cleanPath.substr(0, slashAfterDot) 
            : cleanPath;
    } else {
        client->cgiScriptName = cleanPath;
    }
}

bool CgiHandler::startCgi(ClientConnection* client, const std::string& method,
                         const std::string& path, const std::string& headers,
                         const std::string& body, const LocationConfig* location,
                         const std::string& scriptFilePath) {
    std::string interpreter;
    if (!validateCgiSetup(path, location, scriptFilePath, interpreter))
        return false;
    
    std::string cleanPath, queryString;
    splitPathAndQuery(path, cleanPath, queryString);
    
    std::string pathInfo = extractPathInfo(cleanPath, scriptFilePath);
    setScriptName(client, cleanPath);
    
    int inputPipe[2], outputPipe[2];
    if (!createPipes(inputPipe, outputPipe))
        return false;
    
    char** env = buildEnvironment(client, scriptFilePath, pathInfo, queryString, method, headers, body.size());
    
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "CGI: Fork failed" << std::endl;
        freeEnvironment(env);
        close(inputPipe[0]);
        close(inputPipe[1]);
        close(outputPipe[0]);
        close(outputPipe[1]);
        return false;
    }
    
    if (pid == 0) {
        setupChildProcess(inputPipe, outputPipe, scriptFilePath);
        executeChild(interpreter, scriptFilePath, env);
    }
    
    freeEnvironment(env);
    setupParentProcess(client, inputPipe, outputPipe, pid, body);
    std::cout << "CGI: Started process " << pid << " for " << scriptFilePath << std::endl;
    return true;
}

ssize_t CgiHandler::writeToCgi(ClientConnection* client) {
    if (client->cgiInputFd < 0)
        return 0;
    
    if (client->cgiBodyOffset >= client->cgiBody.size())
        return 0;
    
    size_t remaining = client->cgiBody.size() - client->cgiBodyOffset;
    size_t chunkSize = (remaining > 65536) ? 65536 : remaining;
    ssize_t written = write(client->cgiInputFd, client->cgiBody.c_str() + client->cgiBodyOffset, chunkSize);
    
    if (written > 0)
        client->cgiBodyOffset += written;
    else if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return 1;
    
    return written;
}

ssize_t CgiHandler::readFromCgi(ClientConnection* client) {
    if (client->cgiOutputFd < 0)
        return 0;
    
    char buffer[1000000];
    ssize_t bytesRead = read(client->cgiOutputFd, buffer, sizeof(buffer));
    
    if (bytesRead > 0)
        client->cgiOutputBuffer.append(buffer, bytesRead);
    else if (bytesRead < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return 1;
    
    return bytesRead;
}

void CgiHandler::parseCgiHeader(const std::string& line, int& statusCode, std::string& statusText,
                                std::string& contentType, std::string& location, std::string& additionalHeaders) {
    size_t colonPos = line.find(':');
    if (colonPos == std::string::npos)
        return;
    
    std::string name = line.substr(0, colonPos);
    std::string value = StringUtils::trim(line.substr(colonPos + 1));
    std::string nameLower = StringUtils::toLower(name);
    
    if (nameLower == "status") {
        size_t spacePos = value.find(' ');
        if (spacePos != std::string::npos) {
            statusCode = std::atoi(value.substr(0, spacePos).c_str());
            statusText = value.substr(spacePos + 1);
        } else {
            statusCode = std::atoi(value.c_str());
            statusText = HttpResponse::getStatusText(statusCode);
        }
    } else if (nameLower == "content-type") {
        contentType = value;
    } else if (nameLower == "location") {
        location = value;
        if (statusCode == 200) {
            statusCode = 302;
            statusText = "Found";
        }
    } else if (nameLower == "content-length") {
        return;
    } else {
        additionalHeaders += name + ": " + value + "\r\n";
    }
}

void CgiHandler::buildResponse(ClientConnection* client) {
    std::string& output = client->cgiOutputBuffer;
    
    size_t headerEnd = output.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        headerEnd = output.find("\n\n");
        if (headerEnd == std::string::npos) {
            client->responseBuffer = HttpResponse::build500("CGI Error: Invalid output format", NULL);
            return;
        }
    }
    
    size_t separatorLen = (output[headerEnd] == '\r') ? 4 : 2;
    std::string cgiHeaders = output.substr(0, headerEnd);
    std::string body = output.substr(headerEnd + separatorLen);
    
    int statusCode = 200;
    std::string statusText = "OK";
    std::string contentType = "text/html";
    std::string location;
    std::string additionalHeaders;
    
    size_t pos = 0;
    while (pos < cgiHeaders.length()) {
        size_t lineEnd = cgiHeaders.find("\r\n", pos);
        bool hasCR = false;
        if (lineEnd == std::string::npos) {
            lineEnd = cgiHeaders.find("\n", pos);
            if (lineEnd == std::string::npos) {
                lineEnd = cgiHeaders.length();
            }
        } else {
            hasCR = true;
        }
        
        std::string line = cgiHeaders.substr(pos, lineEnd - pos);
        if (!line.empty())
            parseCgiHeader(line, statusCode, statusText, contentType, location, additionalHeaders);
        
        if (lineEnd >= cgiHeaders.length())
            break;
        pos = lineEnd + (hasCR ? 2 : 1);
    }
    
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n";
    
    if (!location.empty())
        response << "Location: " << location << "\r\n";
    
    response << additionalHeaders << "\r\n" << body;
    client->responseBuffer = response.str();
}

bool CgiHandler::hasTimedOut(ClientConnection* client, int timeoutSeconds) {
    if (client->cgiStartTime == 0)
        return false;
    return (std::time(NULL) - client->cgiStartTime) >= timeoutSeconds;
}

void CgiHandler::killCgi(ClientConnection* client) {
    if (client->cgiPid > 0) {
        kill(client->cgiPid, SIGKILL);
        int status;
        waitpid(client->cgiPid, &status, WNOHANG);
        client->cgiPid = -1;
    }
    cleanup(client);
}

void CgiHandler::cleanup(ClientConnection* client) {
    client->cgiBody.clear();
    client->cgiOutputBuffer.clear();
    client->cgiBodyOffset = 0;
    client->cgiStartTime = 0;
}

bool CgiHandler::checkCgiComplete(ClientConnection* client) {
    if (client->cgiPid <= 0)
        return true;
    
    int status;
    pid_t result = waitpid(client->cgiPid, &status, WNOHANG);
    
    if (result > 0) {
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
            std::cerr << "CGI: Process exited with code " << WEXITSTATUS(status) << std::endl;
        else if (WIFSIGNALED(status))
            std::cerr << "CGI: Process killed by signal " << WTERMSIG(status) << std::endl;
        client->cgiPid = -1;
        return true;
    }
    return false;
}
