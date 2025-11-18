#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

#include <string>
#include <sstream>

class HttpResponse {
public:
    static std::string build404();
    static std::string build501();
    static std::string build411();
    static std::string build200(const std::string& contentType, const std::string& body);
    static std::string buildFileResponse(const std::string& fullPath);
    static std::string getContentType(const std::string& path);
};

#endif
