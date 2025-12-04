#include "../include/CgiHandler.hpp"
#include "../include/HttpResponse.hpp"
#include <sstream>
#include <iostream>
#include <ctime>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

CgiHandler::CgiHandler(Config& cfg) : config(cfg) {
}

CgiHandler::~CgiHandler() {
}

// ==================== CGI Detection ====================

std::string CgiHandler::getCgiExtension(const std::string& path, const LocationConfig* location) {
    if (!location || location->cgiExt.empty()) {
        return "";
    }
    
    // Find the extension in the path
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos) {
        return "";
    }
    
    // Handle case where there might be PATH_INFO after the script
    // e.g., /cgi-bin/script.php/extra/path
    std::string extension;
    size_t slashAfterDot = path.find('/', dotPos);
    if (slashAfterDot != std::string::npos) {
        extension = path.substr(dotPos, slashAfterDot - dotPos);
    } else {
        // Check for query string
        size_t queryPos = path.find('?', dotPos);
        if (queryPos != std::string::npos) {
            extension = path.substr(dotPos, queryPos - dotPos);
        } else {
            extension = path.substr(dotPos);
        }
    }
    
    // Check if this extension matches any configured CGI extension
    for (size_t i = 0; i < location->cgiExt.size(); ++i) {
        if (extension == location->cgiExt[i]) {
            return extension;
        }
    }
    
    return "";
}

std::string CgiHandler::findInterpreter(const std::string& extension, const LocationConfig* location) {
    if (!location || location->cgiPath.empty() || location->cgiExt.empty()) {
        return "";
    }
    
    // Find the index of the extension
    size_t extIndex = 0;
    bool found = false;
    for (size_t i = 0; i < location->cgiExt.size(); ++i) {
        if (location->cgiExt[i] == extension) {
            extIndex = i;
            found = true;
            break;
        }
    }
    
    if (!found) {
        return "";
    }
    
    // Return the corresponding interpreter (or last one if not enough interpreters)
    if (extIndex < location->cgiPath.size()) {
        return location->cgiPath[extIndex];
    }
    return location->cgiPath.back();
}

bool CgiHandler::isCgiRequest(const std::string& path, const LocationConfig* location) {
    return !getCgiExtension(path, location).empty();
}

// ==================== Environment Setup ====================

std::string CgiHandler::getHeaderValue(const std::string& headers, const std::string& headerName) {
    std::string searchKey = headerName + ":";
    size_t pos = headers.find(searchKey);
    
    // Try case-insensitive search
    if (pos == std::string::npos) {
        std::string headersLower = headers;
        std::string keyLower = searchKey;
        for (size_t i = 0; i < headersLower.size(); ++i) {
            if (headersLower[i] >= 'A' && headersLower[i] <= 'Z') {
                headersLower[i] = headersLower[i] + 32;
            }
        }
        for (size_t i = 0; i < keyLower.size(); ++i) {
            if (keyLower[i] >= 'A' && keyLower[i] <= 'Z') {
                keyLower[i] = keyLower[i] + 32;
            }
        }
        pos = headersLower.find(keyLower);
        if (pos == std::string::npos) {
            return "";
        }
    }
    
    size_t valueStart = pos + searchKey.length();
    // Skip whitespace
    while (valueStart < headers.size() && 
           (headers[valueStart] == ' ' || headers[valueStart] == '\t')) {
        valueStart++;
    }
    
    size_t valueEnd = headers.find("\r\n", valueStart);
    if (valueEnd == std::string::npos) {
        valueEnd = headers.find("\n", valueStart);
    }
    if (valueEnd == std::string::npos) {
        valueEnd = headers.length();
    }
    
    return headers.substr(valueStart, valueEnd - valueStart);
}

std::vector<std::string> CgiHandler::buildHttpHeaderVars(const std::string& headers) {
    std::vector<std::string> vars;
    
    // Parse each header line and convert to HTTP_* format
    size_t pos = 0;
    size_t lineEnd;
    
    // Skip the request line
    lineEnd = headers.find("\r\n", pos);
    if (lineEnd != std::string::npos) {
        pos = lineEnd + 2;
    }
    
    while (pos < headers.length()) {
        lineEnd = headers.find("\r\n", pos);
        if (lineEnd == std::string::npos) {
            lineEnd = headers.find("\n", pos);
            if (lineEnd == std::string::npos) {
                break;
            }
        }
        
        std::string line = headers.substr(pos, lineEnd - pos);
        if (line.empty()) {
            break; // End of headers
        }
        
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string name = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            
            // Trim leading whitespace from value
            size_t valueStart = 0;
            while (valueStart < value.size() && 
                   (value[valueStart] == ' ' || value[valueStart] == '\t')) {
                valueStart++;
            }
            value = value.substr(valueStart);
            
            // Skip Content-Type and Content-Length (handled separately)
            std::string nameLower = name;
            for (size_t i = 0; i < nameLower.size(); ++i) {
                if (nameLower[i] >= 'A' && nameLower[i] <= 'Z') {
                    nameLower[i] = nameLower[i] + 32;
                }
            }
            if (nameLower == "content-type" || nameLower == "content-length") {
                pos = lineEnd + 2;
                continue;
            }
            
            // Convert header name to HTTP_HEADER_NAME format
            std::string envName = "HTTP_";
            for (size_t i = 0; i < name.size(); ++i) {
                char c = name[i];
                if (c == '-') {
                    envName += '_';
                } else if (c >= 'a' && c <= 'z') {
                    envName += (char)(c - 32);
                } else {
                    envName += c;
                }
            }
            
            vars.push_back(envName + "=" + value);
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
    // PATH_INFO is the part of the URL path after the script name
    // e.g., if request is /cgi-bin/script.php/extra/path
    // and script is /cgi-bin/script.php
    // then PATH_INFO is /extra/path
    
    // Find where the script extension ends
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos) {
        return "";
    }
    
    size_t slashAfterScript = path.find('/', dotPos);
    if (slashAfterScript != std::string::npos) {
        return path.substr(slashAfterScript);
    }
    
    (void)scriptPath; // May be used in future for more complex path resolution
    return "";
}

std::string CgiHandler::getScriptDirectory(const std::string& scriptPath) {
    size_t lastSlash = scriptPath.rfind('/');
    if (lastSlash != std::string::npos) {
        return scriptPath.substr(0, lastSlash);
    }
    return ".";
}

char** CgiHandler::buildEnvironment(ClientConnection* client, const std::string& scriptPath,
                                    const std::string& pathInfo, const std::string& queryString,
                                    const std::string& method, const std::string& headers,
                                    size_t contentLength) {
    std::vector<std::string> envVars;
    
    // Get server config
    const ServerConfig& serverConfig = config.getServer(client->serverIndex);
    
    // Convert script path to absolute path for SCRIPT_FILENAME
    char absolutePath[PATH_MAX];
    std::string absScriptPath = scriptPath;
    if (realpath(scriptPath.c_str(), absolutePath) != NULL) {
        absScriptPath = absolutePath;
    }
    
    // Required CGI/1.1 environment variables
    envVars.push_back("GATEWAY_INTERFACE=CGI/1.1");
    envVars.push_back("SERVER_PROTOCOL=HTTP/1.1");
    envVars.push_back("SERVER_SOFTWARE=WebServ/1.0");
    
    // Server info
    std::ostringstream portStr;
    portStr << serverConfig.port;
    envVars.push_back("SERVER_NAME=" + serverConfig.host);
    envVars.push_back("SERVER_PORT=" + portStr.str());
    
    // Request info
    envVars.push_back("REQUEST_METHOD=" + method);
    envVars.push_back("SCRIPT_NAME=" + client->cgiScriptName);
    envVars.push_back("SCRIPT_FILENAME=" + absScriptPath);
    
    // PATH_INFO - if empty, set to the script name (request path)
    // Some CGIs (like ubuntu_cgi_tester) expect PATH_INFO to contain the request path
    if (pathInfo.empty()) {
        envVars.push_back("PATH_INFO=" + client->cgiScriptName);
    } else {
        envVars.push_back("PATH_INFO=" + pathInfo);
    }
    envVars.push_back("QUERY_STRING=" + queryString);
    
    // PATH_TRANSLATED (absolute path for PATH_INFO)
    if (!pathInfo.empty()) {
        envVars.push_back("PATH_TRANSLATED=" + serverConfig.root + pathInfo);
    }
    
    // Request URI (original request)
    std::string requestUri = client->cgiScriptName;
    if (!pathInfo.empty()) {
        requestUri += pathInfo;
    }
    if (!queryString.empty()) {
        requestUri += "?" + queryString;
    }
    envVars.push_back("REQUEST_URI=" + requestUri);
    
    // Content info (for POST requests)
    if (contentLength > 0) {
        std::ostringstream clStr;
        clStr << contentLength;
        envVars.push_back("CONTENT_LENGTH=" + clStr.str());
    }
    
    std::string contentType = getHeaderValue(headers, "Content-Type");
    if (!contentType.empty()) {
        envVars.push_back("CONTENT_TYPE=" + contentType);
    }
    
    // Remote client info (simplified - could be enhanced)
    envVars.push_back("REMOTE_ADDR=127.0.0.1");
    envVars.push_back("REMOTE_HOST=localhost");
    
    // Add HTTP headers as HTTP_* variables
    std::vector<std::string> httpVars = buildHttpHeaderVars(headers);
    for (size_t i = 0; i < httpVars.size(); ++i) {
        envVars.push_back(httpVars[i]);
    }
    
    // Redirect status for php-cgi
    envVars.push_back("REDIRECT_STATUS=200");
    
    // Document root
    envVars.push_back("DOCUMENT_ROOT=" + serverConfig.root);
    
    // Convert to char** array
    char** env = new char*[envVars.size() + 1];
    for (size_t i = 0; i < envVars.size(); ++i) {
        env[i] = new char[envVars[i].size() + 1];
        std::strcpy(env[i], envVars[i].c_str());
    }
    env[envVars.size()] = NULL;
    
    return env;
}

void CgiHandler::freeEnvironment(char** env) {
    if (!env) return;
    
    for (size_t i = 0; env[i] != NULL; ++i) {
        delete[] env[i];
    }
    delete[] env;
}

// ==================== CGI Execution ====================

bool CgiHandler::startCgi(ClientConnection* client, const std::string& method,
                         const std::string& path, const std::string& headers,
                         const std::string& body, const LocationConfig* location,
                         const std::string& scriptFilePath) {
    // Get CGI extension and interpreter
    std::string extension = getCgiExtension(path, location);
    std::string interpreter = findInterpreter(extension, location);
    
    if (interpreter.empty()) {
        std::cerr << "CGI: No interpreter found for extension: " << extension << std::endl;
        return false;
    }
    
    // Convert interpreter to absolute path (needed because we chdir later)
    char interpreterAbsPath[PATH_MAX];
    if (realpath(interpreter.c_str(), interpreterAbsPath) != NULL) {
        interpreter = interpreterAbsPath;
    }
    
    // Check if interpreter exists and is executable
    if (access(interpreter.c_str(), X_OK) != 0) {
        std::cerr << "CGI: Interpreter not found or not executable: " << interpreter << std::endl;
        return false;
    }
    
    // Check if script exists
    if (access(scriptFilePath.c_str(), F_OK) != 0) {
        std::cerr << "CGI: Script not found: " << scriptFilePath << std::endl;
        return false;
    }
    
    // Parse path and query string
    std::string cleanPath, queryString;
    splitPathAndQuery(path, cleanPath, queryString);
    
    // Extract PATH_INFO
    std::string pathInfo = extractPathInfo(cleanPath, scriptFilePath);
    
    // Store script name in client for environment
    // Script name is the URL path to the script (without PATH_INFO)
    size_t dotPos = cleanPath.rfind('.');
    if (dotPos != std::string::npos) {
        size_t slashAfterDot = cleanPath.find('/', dotPos);
        if (slashAfterDot != std::string::npos) {
            client->cgiScriptName = cleanPath.substr(0, slashAfterDot);
        } else {
            client->cgiScriptName = cleanPath;
        }
    } else {
        client->cgiScriptName = cleanPath;
    }
    
    // Create pipes for CGI communication
    int inputPipe[2];   // Parent writes, CGI reads (stdin)
    int outputPipe[2];  // CGI writes, parent reads (stdout)
    
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
    
    // Build environment before fork
    char** env = buildEnvironment(client, scriptFilePath, pathInfo, queryString,
                                  method, headers, body.size());
    
    // Fork CGI process
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
        // Child process - CGI execution
        
        // Close unused pipe ends
        close(inputPipe[1]);   // Close write end of input pipe
        close(outputPipe[0]);  // Close read end of output pipe
        
        // Redirect stdin to input pipe
        if (dup2(inputPipe[0], STDIN_FILENO) < 0) {
            std::cerr << "CGI: dup2 stdin failed" << std::endl;
            _exit(1);
        }
        close(inputPipe[0]);
        
        // Redirect stdout to output pipe
        if (dup2(outputPipe[1], STDOUT_FILENO) < 0) {
            std::cerr << "CGI: dup2 stdout failed" << std::endl;
            _exit(1);
        }
        close(outputPipe[1]);
        
        // Change to script directory for relative path access
        std::string scriptDir = getScriptDirectory(scriptFilePath);
        if (chdir(scriptDir.c_str()) < 0) {
            std::cerr << "CGI: chdir failed to " << scriptDir << std::endl;
            // Continue anyway, script might still work
        }
        
        // Get just the script filename (not full path) for execution
        // since we've already changed to the script's directory
        std::string scriptName = scriptFilePath;
        size_t lastSlash = scriptName.rfind('/');
        if (lastSlash != std::string::npos) {
            scriptName = scriptName.substr(lastSlash + 1);
        }
        
        // Check if this is a standalone CGI binary (like ubuntu_cgi_tester)
        // vs an interpreter that takes a script (like php-cgi, python)
        // Standalone CGI binaries don't need the script as an argument -
        // they read SCRIPT_FILENAME from the environment
        bool isStandaloneCgi = false;
        
        // Common interpreters that need script as argument
        if (interpreter.find("php") == std::string::npos &&
            interpreter.find("python") == std::string::npos &&
            interpreter.find("perl") == std::string::npos &&
            interpreter.find("ruby") == std::string::npos) {
            isStandaloneCgi = true;
        }
        
        // Prepare arguments for execve
        char* argv[3];
        argv[0] = const_cast<char*>(interpreter.c_str());
        
        if (isStandaloneCgi) {
            // Standalone CGI - no script argument needed
            argv[1] = NULL;
        } else {
            // Interpreter-based CGI - pass script as argument
            argv[1] = const_cast<char*>(scriptName.c_str());
            argv[2] = NULL;
        }
        
        // Execute CGI
        execve(interpreter.c_str(), argv, env);
        
        // If execve returns, it failed
        std::cerr << "CGI: execve failed for " << interpreter << std::endl;
        _exit(1);
    }
    
    // Parent process
    freeEnvironment(env);
    
    // Close unused pipe ends
    close(inputPipe[0]);   // Close read end of input pipe
    close(outputPipe[1]);  // Close write end of output pipe
    
    // Set pipes to non-blocking
    int flags = fcntl(inputPipe[1], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(inputPipe[1], F_SETFL, flags | O_NONBLOCK);
    }
    
    flags = fcntl(outputPipe[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(outputPipe[0], F_SETFL, flags | O_NONBLOCK);
    }
    
    // Store CGI state in client connection
    client->cgiPid = pid;
    client->cgiInputFd = inputPipe[1];
    client->cgiOutputFd = outputPipe[0];
    client->cgiBody = body;
    client->cgiBodyOffset = 0;
    client->cgiOutputBuffer.clear();
    client->cgiStartTime = std::time(NULL);
    client->state = ClientConnection::CGI_RUNNING;
    
    std::cout << "CGI: Started process " << pid << " for " << scriptFilePath << std::endl;
    
    return true;
}

ssize_t CgiHandler::writeToCgi(ClientConnection* client) {
    if (client->cgiInputFd < 0) {
        return 0;  // Already closed
    }
    
    if (client->cgiBodyOffset >= client->cgiBody.size()) {
        // All data written, close input pipe to signal EOF
        close(client->cgiInputFd);
        client->cgiInputFd = -1;
        return 0;
    }
    
    size_t remaining = client->cgiBody.size() - client->cgiBodyOffset;
    ssize_t written = write(client->cgiInputFd,
                            client->cgiBody.c_str() + client->cgiBodyOffset,
                            remaining);
    
    if (written > 0) {
        client->cgiBodyOffset += written;
        
        // Check if done writing
        if (client->cgiBodyOffset >= client->cgiBody.size()) {
            close(client->cgiInputFd);
            client->cgiInputFd = -1;
        }
    } else if (written < 0) {
        // Error or EAGAIN
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // Would block, try again later
        }
        return -1;  // Real error
    }
    
    return written;
}

ssize_t CgiHandler::readFromCgi(ClientConnection* client) {
    if (client->cgiOutputFd < 0) {
        return 0;  // Already closed
    }
    
    char buffer[1000000];
    ssize_t bytesRead = read(client->cgiOutputFd, buffer, sizeof(buffer));
    
    if (bytesRead > 0) {
        client->cgiOutputBuffer.append(buffer, bytesRead);
    } else if (bytesRead == 0) {
        // EOF - CGI finished output
        close(client->cgiOutputFd);
        client->cgiOutputFd = -1;
        return 0;
    } else {
        // Error or EAGAIN
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // Would block, try again later
        }
        return -1;  // Real error
    }
    
    return bytesRead;
}

void CgiHandler::buildResponse(ClientConnection* client) {
    // Parse CGI output
    // CGI output format: headers\r\n\r\nbody
    // Headers include at minimum: Content-Type
    // May also include: Status, Location, etc.
    
    std::string& output = client->cgiOutputBuffer;
    
    // Find header/body separator
    size_t headerEnd = output.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        headerEnd = output.find("\n\n");
        if (headerEnd == std::string::npos) {
            // No proper header separation, treat as error
            client->responseBuffer = HttpResponse::build500("CGI Error: Invalid output format", NULL);
            return;
        }
    }
    
    size_t separatorLen = (output[headerEnd] == '\r') ? 4 : 2;
    std::string cgiHeaders = output.substr(0, headerEnd);
    std::string body = output.substr(headerEnd + separatorLen);
    
    // Parse CGI headers
    int statusCode = 200;
    std::string statusText = "OK";
    std::string contentType = "text/html";
    std::string location;
    std::string additionalHeaders;
    
    // Parse each CGI header line
    size_t pos = 0;
    while (pos < cgiHeaders.length()) {
        size_t lineEnd = cgiHeaders.find("\r\n", pos);
        if (lineEnd == std::string::npos) {
            lineEnd = cgiHeaders.find("\n", pos);
            if (lineEnd == std::string::npos) {
                lineEnd = cgiHeaders.length();
            }
        }
        
        std::string line = cgiHeaders.substr(pos, lineEnd - pos);
        if (line.empty()) {
            pos = lineEnd + (cgiHeaders[lineEnd] == '\r' ? 2 : 1);
            continue;
        }
        
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string name = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            
            // Trim leading whitespace from value
            size_t valueStart = 0;
            while (valueStart < value.size() && 
                   (value[valueStart] == ' ' || value[valueStart] == '\t')) {
                valueStart++;
            }
            value = value.substr(valueStart);
            
            // Handle special CGI headers
            std::string nameLower = name;
            for (size_t i = 0; i < nameLower.size(); ++i) {
                if (nameLower[i] >= 'A' && nameLower[i] <= 'Z') {
                    nameLower[i] = nameLower[i] + 32;
                }
            }
            
            if (nameLower == "status") {
                // Parse "Status: 200 OK" or "Status: 200"
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
                    statusCode = 302;  // Default to 302 redirect
                    statusText = "Found";
                }
            } else {
                // Pass through other headers
                additionalHeaders += name + ": " + value + "\r\n";
            }
        }
        
        pos = lineEnd + (cgiHeaders[lineEnd] == '\r' ? 2 : 1);
    }
    
    // Build HTTP response
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    response << "Content-Type: " << contentType << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    
    if (!location.empty()) {
        response << "Location: " << location << "\r\n";
    }
    
    response << additionalHeaders;
    response << "\r\n";
    response << body;
    
    client->responseBuffer = response.str();
}

bool CgiHandler::hasTimedOut(ClientConnection* client, int timeoutSeconds) {
    if (client->cgiStartTime == 0) {
        return false;
    }
    
    time_t now = std::time(NULL);
    return (now - client->cgiStartTime) >= timeoutSeconds;
}

void CgiHandler::killCgi(ClientConnection* client) {
    if (client->cgiPid > 0) {
        kill(client->cgiPid, SIGKILL);
        
        // Wait for process to avoid zombie
        int status;
        waitpid(client->cgiPid, &status, WNOHANG);
        
        client->cgiPid = -1;
    }
    
    cleanup(client);
}

void CgiHandler::cleanup(ClientConnection* client) {
    // Close any open pipes
    if (client->cgiInputFd >= 0) {
        close(client->cgiInputFd);
        client->cgiInputFd = -1;
    }
    
    if (client->cgiOutputFd >= 0) {
        close(client->cgiOutputFd);
        client->cgiOutputFd = -1;
    }
    
    // Reset CGI state
    client->cgiBody.clear();
    client->cgiOutputBuffer.clear();
    client->cgiBodyOffset = 0;
    client->cgiStartTime = 0;
}

bool CgiHandler::checkCgiComplete(ClientConnection* client) {
    if (client->cgiPid <= 0) {
        return true;  // Already complete or not started
    }
    
    int status;
    pid_t result = waitpid(client->cgiPid, &status, WNOHANG);
    
    if (result > 0) {
        // Process has exited
        if (WIFEXITED(status)) {
            int exitCode = WEXITSTATUS(status);
            if (exitCode != 0) {
                std::cerr << "CGI: Process exited with code " << exitCode << std::endl;
            }
        } else if (WIFSIGNALED(status)) {
            std::cerr << "CGI: Process killed by signal " << WTERMSIG(status) << std::endl;
        }
        client->cgiPid = -1;
        return true;
    }
    
    return false;  // Still running
}
