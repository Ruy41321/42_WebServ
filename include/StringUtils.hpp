#ifndef STRINGUTILS_HPP
#define STRINGUTILS_HPP

#include <string>
#include <vector>

namespace StringUtils {
    std::string trim(const std::string& str);
    std::string toLower(const std::string& str);
    std::vector<std::string> split(const std::string& str, char delimiter);
    std::string intToString(int value);
    std::string sizeToString(size_t value);
}

#endif
