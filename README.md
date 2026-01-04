# 🌐 WebServ - HTTP/1.1 Server in C++98

A fully functional HTTP/1.1 web server implementation written in C++98, compliant with the 42 school curriculum. This project features non-blocking I/O, multiple server configurations, CGI support, file uploads, and comprehensive error handling.

---

## 📋 Table of Contents

- [Features](#-features)
- [Requirements](#-requirements)
- [Building](#-building)
- [Usage](#-usage)
- [Configuration](#-configuration)
- [Testing](#-testing)
- [Project Structure](#-project-structure)
- [Technical Details](#-technical-details)
- [Contributors](#-contributors)

---

## ✨ Features

### Core Functionality
- ✅ **Non-blocking I/O** with `epoll` for efficient connection handling
- ✅ **HTTP/1.1 Protocol** support with persistent connections
- ✅ **Multiple HTTP Methods**: GET, POST, DELETE, HEAD
- ✅ **Multiple Server Blocks** listening on different ports
- ✅ **Static Website Serving** with directory listing
- ✅ **File Upload** support with configurable size limits
- ✅ **CGI Execution** (PHP, Python) with proper environment variables
- ✅ **HTTP Redirections** (301, 302)
- ✅ **Custom Error Pages** (404, 500, etc.)
- ✅ **Chunked Transfer Encoding** support
- ✅ **Request Timeout** handling
- ✅ **Graceful Shutdown** with proper resource cleanup

### Advanced Features
- 🔒 **Memory Leak Free** - verified with Valgrind
- 📊 **Comprehensive Test Suite** covering all functionalities
- 🎯 **NGINX-style Configuration** file format
- 🚀 **High Performance** with concurrent request handling
- 📝 **Detailed Logging** for debugging and monitoring

---

## 📦 Requirements

- **C++ Compiler**: g++ with C++98 support
- **Operating System**: Linux
- **Tools**: make
- **Optional**: 
  - `php-cgi` for PHP CGI support
  - `python3` for Python CGI support
  - `valgrind` for memory leak testing

---

## 🔨 Building

### Compile the server

```bash
make
```

This creates the `webserv` executable.

### Clean build artifacts

```bash
make clean      # Remove object files
make fclean     # Remove object files and executable
make re         # Rebuild from scratch
```

---

## 🚀 Usage

### Start the server

```bash
./webserv [configuration_file]
```

**Default configuration:**
```bash
./webserv config/default.conf
```

### Alternative run methods

```bash
make toggle_redirection     # Toggle output redirection to log file
make run                    # Run with default config
```

### Stop the server

Press `Ctrl+C` or send `SIGTERM`:
```bash
pkill webserv
```

---

## ⚙️ Configuration

The configuration file uses an NGINX-inspired syntax. See `config/default.conf` for a complete example.

### Basic Server Block

```nginx
server {
    listen 127.0.0.1:8080;          # Interface:port
    root ./www;                      # Document root
    index index.html;                # Default index file
    autoindex on;                    # Enable directory listing
    client_max_body_size 1048576;   # Max request body size (bytes)
    
    error_page 404 /errors/404.html;
    error_page 500 /errors/50x.html;
    
    location / {
        allow_methods GET POST HEAD;
    }
}
```

### Configuration Options

#### Server Directives
- `listen`: Interface and port to bind (e.g., `127.0.0.1:8080`, `0.0.0.0:8082`)
- `root`: Root directory for serving files
- `index`: Default file to serve for directories
- `autoindex`: Enable/disable directory listing (`on`/`off`)
- `client_max_body_size`: Maximum request body size in bytes (0 = unlimited)
- `error_page`: Custom error pages for status codes

#### Location Directives
- `location`: URL path to configure
- `allow_methods`: Permitted HTTP methods (GET, POST, DELETE, HEAD)
- `return`: HTTP redirection (e.g., `return 301 /new-path`)
- `root`: Override root directory for this location
- `upload_store`: Directory for file uploads
- `cgi_path`: Path to CGI interpreter(s)
- `cgi_ext`: File extensions to handle as CGI

### Example Configurations

#### File Upload Location
```nginx
location /uploads {
    root ./www/uploads;
    allow_methods GET POST PUT DELETE;
    upload_store ./www/uploads;
    client_max_body_size 10485760;  # 10MB limit
}
```

#### CGI Configuration
```nginx
location /cgi-bin {
    root ./www/cgi-bin;
    allow_methods GET POST;
    cgi_path /usr/bin/php-cgi /usr/bin/python3;
    cgi_ext .php .py;
}
```

#### HTTP Redirection
```nginx
location /old-page {
    return 301 /new-page;
}
```

---

## 🧪 Testing

### Automated Test Suite

Run all tests:
```bash
make test
```

This executes:
- Basic HTTP functionality tests
- HTTP/1.1 compliance tests
- Body size limit tests
- Multi-server configuration tests
- Configuration parsing tests
- File upload tests
- CGI execution tests

### Individual Test Scripts

```bash
./test/test_server.sh            # Basic server functionality
./test/test_http11.sh            # HTTP/1.1 features
./test/test_body_size_limit.sh   # Request size limits
./test/test_multiserver.sh       # Multiple server blocks
./test/test_config_errors.sh     # Configuration validation
./test/test_uploads.sh           # File upload functionality
./test/test_cgi.sh               # CGI execution
```

### Memory Leak Testing

Run server with Valgrind:
```bash
make test_valgrind
```

This performs:
- Memory leak detection
- File descriptor leak detection
- Invalid memory access checks
- CGI process leak verification

### Manual Testing

#### Using curl

```bash
# GET request
curl http://127.0.0.1:8080/

# POST request with data
curl -X POST -d "key=value" http://127.0.0.1:8080/

# File upload
curl -X POST -F "file=@test.txt" http://127.0.0.1:8080/uploads/

# DELETE request
curl -X DELETE http://127.0.0.1:8080/uploads/test.txt

# CGI script
curl http://127.0.0.1:8080/cgi-bin/test.php?name=test
```

#### Using a web browser

Open in your browser:
- Main page: http://127.0.0.1:8080/
- Directory listing: http://127.0.0.1:8080/testdir/
- CGI test: http://127.0.0.1:8080/cgi-bin/test.php

### Subject Tester

Run the official 42 tester (requires server running on port 8084):
```bash
make subject_test (make run before to start it)
```

---

## 📁 Project Structure

```
.
├── Makefile                 # Build configuration
├── README.md               # This file
├── include/                # Header files
│   ├── WebServer.hpp       # Main server class
│   ├── Config.hpp          # Configuration parser
│   ├── HttpRequest.hpp     # HTTP request parser
│   ├── HttpResponse.hpp    # HTTP response builder
│   ├── ClientConnection.hpp # Client connection handler
│   ├── ConnectionManager.hpp # Connection pool manager
│   ├── CgiHandler.hpp      # CGI execution handler
│   └── StringUtils.hpp     # Utility functions
├── src/                    # Source files
│   ├── main.cpp
│   ├── WebServer.cpp
│   ├── Config.cpp
│   ├── HttpResponse.cpp
│   ├── ClientConnection.cpp
│   ├── ConnectionManager.cpp
│   ├── CgiHandler.cpp
│   ├── StringUtils.cpp
│   └── request/            # HTTP request handling (refactored)
│       ├── HttpRequest.cpp
│       ├── HttpRequestHandlers.cpp
│       └── HttpRequestHelpers.cpp
├── config/                 # Configuration files
│   ├── default.conf        # Default server configuration
│   └── duplicate_test.conf # Test configuration
├── www/                    # Web content
│   ├── index.html          # Main page
│   ├── cgi-bin/            # CGI scripts
│   ├── errors/             # Error pages
│   ├── uploads/            # Upload directory
│   └── ...
├── test/                   # Test scripts
│   ├── test_server.sh
│   ├── test_valgrind.sh
│   └── ...
└── subject/                # Project subject files
    ├── en.subject.pdf
    ├── en.new_subject.pdf
    └── ...
```

---

## 🔧 Technical Details

### Architecture

The server uses an **event-driven architecture** with non-blocking I/O:

1. **WebServer**: Main server class managing multiple server blocks
2. **ConnectionManager**: Manages client connections and socket events
3. **ClientConnection**: Handles individual client state and request/response cycle
4. **HttpRequest**: Parses incoming HTTP requests
5. **HttpResponse**: Builds HTTP responses
6. **CgiHandler**: Executes CGI scripts with proper environment setup
7. **Config**: Parses NGINX-style configuration files

### Non-blocking I/O

All socket operations use `epoll` to monitor file descriptors:
- **Read events**: Incoming data from clients, CGI output
- **Write events**: Outgoing data to clients, CGI input
- **Timeout handling**: Closes inactive connections

### HTTP/1.1 Features

- **Persistent Connections**: Keep-Alive support
- **Chunked Transfer Encoding**: Properly un-chunks requests
- **Content-Length**: Accurate body size calculation
- **Multiple Methods**: GET, POST, DELETE, HEAD, PUT
- **Status Codes**: Accurate HTTP response codes (200, 201, 204, 301, 302, 400, 404, 405, 413, 500, 501, 505)

### CGI Implementation

- **Environment Variables**: Sets all required CGI variables (REQUEST_METHOD, QUERY_STRING, CONTENT_TYPE, etc.)
- **Process Management**: Proper fork/exec with pipe communication
- **Timeout Handling**: Prevents infinite CGI execution
- **Working Directory**: Runs CGI in correct directory for relative paths
- **EOF Detection**: Handles CGI output without Content-Length

### Memory Management

- **Zero Leaks**: Verified with Valgrind
- **RAII Pattern**: Resource cleanup in destructors
- **Smart Pointer Usage**: Where applicable in C++98
- **FD Management**: Proper close() for all file descriptors

---

## 👥 Contributors

This project was developed as part of the 42 school curriculum by:

- **Luigi Pennisi** ([lpennisi](https://github.com/Ruy41321))
- **Matteo Masitto** ([mmasitto](https://github.com/maizitto))

---

## 📚 Resources

- [HTTP/1.1 RFC 2616](https://tools.ietf.org/html/rfc2616)
- [HTTP/1.0 RFC 1945](https://tools.ietf.org/html/rfc1945)
- [CGI Specification](https://tools.ietf.org/html/rfc3CGI)
- [NGINX Configuration](https://nginx.org/en/docs/)

---

## 📄 License

This project is part of the 42 school curriculum and is subject to its academic policies.

---

## 🎯 Project Status

✅ **All mandatory features implemented and tested**
✅ **Memory leak free (Valgrind verified)**
✅ **Comprehensive test suite passing**
✅ **Ready for evaluation**

---

*Last updated: January 4, 2026*
