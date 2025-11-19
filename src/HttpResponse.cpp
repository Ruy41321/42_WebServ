#include "../include/HttpResponse.hpp"
#include <fstream>
#include <iostream>

std::string HttpResponse::build404() {
    std::string content = "<html><body><h1>404 Not Found</h1><p>The requested resource was not found.</p></body></html>";
    std::ostringstream oss;
    oss << "HTTP/1.0 404 Not Found\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << content.length() << "\r\n"
        << "\r\n"
        << content;
    return oss.str();
}

std::string HttpResponse::build501() {
    std::string content = "<html><body><h1>501 Not Implemented</h1></body></html>";
    std::ostringstream oss;
    oss << "HTTP/1.0 501 Not Implemented\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << content.length() << "\r\n"
        << "\r\n"
        << content;
    return oss.str();
}

std::string HttpResponse::build411() {
    std::string content = "<html><body><h1>411 Length Required</h1></body></html>";
    std::ostringstream oss;
    oss << "HTTP/1.0 411 Length Required\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << content.length() << "\r\n"
        << "\r\n"
        << content;
    return oss.str();
}

std::string HttpResponse::build201(const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.0 201 Created\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.length() << "\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string HttpResponse::build403(const std::string& message) {
    std::ostringstream bodyStream;
    bodyStream << "<html><body><h1>403 Forbidden</h1><p>" << message << "</p></body></html>";
    std::string body = bodyStream.str();
    
    std::ostringstream oss;
    oss << "HTTP/1.0 403 Forbidden\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.length() << "\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string HttpResponse::build413() {
    std::string body = "<html><body><h1>413 Request Entity Too Large</h1></body></html>";
    std::ostringstream oss;
    oss << "HTTP/1.0 413 Request Entity Too Large\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.length() << "\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string HttpResponse::build500(const std::string& message) {
    std::ostringstream bodyStream;
    bodyStream << "<html><body><h1>500 Internal Server Error</h1><p>" << message << "</p></body></html>";
    std::string body = bodyStream.str();
    
    std::ostringstream oss;
    oss << "HTTP/1.0 500 Internal Server Error\r\n"
        << "Content-Type: text/html\r\n"
        << "Content-Length: " << body.length() << "\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string HttpResponse::build200(const std::string& contentType, const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.0 200 OK\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.length() << "\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string HttpResponse::buildFileResponse(const std::string& fullPath) {
    std::ifstream file(fullPath.c_str(), std::ios::binary);
    
    if (!file.is_open()) {
        return build404();
    }
    
    // Read file content
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    // Get content type
    std::string contentType = getContentType(fullPath);
    
    return build200(contentType, content);
}

std::string HttpResponse::getContentType(const std::string& path) {
    size_t dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos) {
        return "application/octet-stream";
    }
    
    std::string extension = path.substr(dotPos);
    
    if (extension == ".html" || extension == ".htm") {
        return "text/html";
    } else if (extension == ".css") {
        return "text/css";
    } else if (extension == ".js") {
        return "application/javascript";
    } else if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    } else if (extension == ".png") {
        return "image/png";
    } else if (extension == ".gif") {
        return "image/gif";
    } else if (extension == ".txt") {
        return "text/plain";
    } else if (extension == ".json") {
        return "application/json";
    } else if (extension == ".xml") {
        return "application/xml";
    }
    
    return "application/octet-stream";
}
