# GitHub Copilot Instructions - 42 WebServ Project

## Project Overview
This is a C++98 HTTP web server implementation for the 42 school curriculum. The server must be fully non-blocking, support multiple ports, handle HTTP requests (GET, POST, DELETE), serve static content, handle file uploads, and execute CGI scripts.

## Core Requirements

### Language & Standards
- **C++98 compliant only** - no C++11/14/17/20 features
- No external libraries beyond standard C++98 and POSIX functions
- All code must compile with `-Wall -Werror -Wextra`

### Architecture Constraints
- **Non-blocking I/O mandatory** - server must never block
- Use a single `poll()`, `select()`, `epoll()`, or `kqueue()` for ALL I/O operations
- Never call `read()`/`recv()` or `write()`/`send()` without readiness notification from poll/select
- Exception: regular disk file operations don't need poll/select
- No `execve()` except for CGI execution
- Only one `fork()` allowed, exclusively for CGI processes
- No checking `errno` after read/write operations to adjust behavior

### HTTP Protocol Implementation
- Support HTTP/1.0 protocol
- Implement GET, POST, and DELETE methods
- Accurate HTTP response status codes (200, 404, 500, etc.)
- Handle chunked transfer encoding (un-chunk before passing to CGI)
- Support persistent connections (Keep-Alive)
- Parse headers, body, and query strings correctly
- Handle different content types (HTML, CSS, JS, images, etc.)

### Server Features
- Listen on multiple ports simultaneously
- Serve fully static websites
- Client file upload capability
- Directory listing (when enabled)
- HTTP redirections
- Default error pages (customizable)
- Maximum client body size limit
- Request timeout handling
- Proper client disconnection handling

### Configuration File
Parse NGINX-style configuration supporting:
- Multiple server blocks (interface:port pairs)
- Root directory per server/location
- Default index files
- Error page definitions
- Client body size limits
- Route/location specific rules:
  - Accepted HTTP methods per route
  - HTTP redirections
  - Root directory mapping
  - Directory listing enable/disable
  - Default file for directories
  - Upload directory and permissions
  - CGI execution based on file extension (.php, .py, etc.)

### CGI Implementation
- Execute CGI scripts based on file extension
- Set proper environment variables (REQUEST_METHOD, QUERY_STRING, CONTENT_TYPE, etc.)
- Pass full request to CGI via stdin
- Read CGI output from stdout
- Handle CGI without Content-Length (EOF marks end)
- Run CGI in correct directory for relative paths
- Support at least one CGI interpreter (php-cgi, python, etc.)
- Handle CGI timeouts and errors gracefully

## Code Style Guidelines

### Memory Management
- No memory leaks allowed
- All `new` must have corresponding `delete`
- All `open()` must have corresponding `close()`
- Clean up resources in destructors
- Use RAII pattern where appropriate
- Handle all edge cases for resource cleanup

### Error Handling
- Check all system call return values
- Handle all possible error conditions
- Never crash or hang the server
- Log errors appropriately
- Return proper HTTP error responses to clients

### Naming Conventions
- Classes: PascalCase (`WebServer`, `HttpRequest`)
- Methods/functions: camelCase (`handleClient`, `parseRequest`)
- Private members: use prefix or suffix convention consistently
- Constants: UPPER_SNAKE_CASE
- Files: match class names (WebServer.cpp, WebServer.hpp)

## Common Pitfalls to Avoid

### Non-blocking I/O
- ❌ Never call `read()/recv()` without checking readiness first
- ❌ Never call `write()/send()` without checking writability first
- ✅ Always use `fcntl()` to set `O_NONBLOCK` on sockets
- ✅ Handle `EAGAIN`/`EWOULDBLOCK` properly
- ✅ Maintain state for partial reads/writes

### Poll/Select Usage
- ❌ Don't forget to reset `fd_set` before each `select()` call
- ❌ Don't ignore the return value of poll/select
- ✅ Monitor both read and write events as needed
- ✅ Handle all ready file descriptors in one iteration
- ✅ Update monitored FDs when connections open/close

### HTTP Protocol
- ❌ Don't assume complete request in one read
- ❌ Don't assume headers end at first `\r\n`
- ✅ Buffer partial requests across multiple reads
- ✅ Look for `\r\n\r\n` to mark end of headers
- ✅ Use Content-Length to determine body size
- ✅ Support chunked transfer encoding

### Configuration
- ❌ Don't hardcode ports, paths, or limits
- ✅ Parse all settings from configuration file
- ✅ Validate configuration values
- ✅ Provide sensible defaults
- ✅ Support multiple server blocks

### CGI Execution
- ❌ Don't forget to close unused pipe ends in child/parent
- ❌ Don't leak file descriptors in CGI processes
- ✅ Set all required environment variables
- ✅ Handle CGI timeout (waitpid with timeout)
- ✅ Clean up zombie processes
- ✅ Validate CGI output before sending to client

## Testing Recommendations

### Manual Testing
- Test with browsers (Chrome, Firefox)
- Test with `curl` for various scenarios
- Test with `siege` or `ab` for load testing
- Compare behavior with NGINX

### Test Scenarios
- Single file request
- Large file request
- Concurrent requests
- Slow client (partial reads)
- Keep-alive connections
- File upload (small and large)
- CGI script execution
- Invalid requests
- Request timeout
- Server at max connections

### Edge Cases
- Request with no headers
- Request with very large headers
- Request with invalid method
- Request to non-existent file
- Request to directory without index
- POST without Content-Length
- Chunked request
- Client disconnect during transfer

## Allowed System Functions

### Socket Operations
`socket()`, `accept()`, `listen()`, `send()`, `recv()`, `bind()`, `connect()`, `setsockopt()`, `getsockname()`

### I/O Multiplexing
`select()`, `poll()`, `epoll()` (epoll_create, epoll_ctl, epoll_wait), `kqueue()` (kqueue, kevent)

### File Operations
`open()`, `close()`, `read()`, `write()`, `access()`, `stat()`, `opendir()`, `readdir()`, `closedir()`, `chdir()`

### Process Management
`fork()`, `execve()`, `waitpid()`, `kill()`, `signal()`, `pipe()`

### Network Utilities
`htons()`, `htonl()`, `ntohs()`, `ntohl()`, `getaddrinfo()`, `freeaddrinfo()`, `getprotobyname()`

### File Descriptor Manipulation
`fcntl()`, `dup()`, `dup2()`, `socketpair()`

### Error Handling
`strerror()`, `gai_strerror()`, `errno` (but not for adjusting behavior after I/O)

## Code Generation Guidelines

When generating code:
1. Always use C++98 syntax and features only
2. Include proper error checking for all system calls
3. Ensure non-blocking I/O compliance
4. Add comments explaining complex logic
5. Follow Orthodox Canonical Form for classes
6. Handle all resource cleanup properly
7. Consider edge cases and error conditions
8. Make code modular and maintainable
9. Avoid code duplication
10. Keep functions focused and reasonably sized

## Example Code Patterns

### Non-blocking Socket Setup
```cpp
int fd = socket(AF_INET, SOCK_STREAM, 0);
if (fd < 0)
    return -1;
    
int flags = fcntl(fd, F_GETFL, 0);
if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    close(fd);
    return -1;
}

int opt = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

### Select Loop Pattern
```cpp
while (running) {
    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    
    int max_fd = setupFdSets(read_fds, write_fds);
    
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    
    int ret = select(max_fd + 1, &read_fds, &write_fds, NULL, &timeout);
    if (ret < 0)
        break;
    
    handleReadyFds(read_fds, write_fds);
    handleTimeouts();
}
```

### Reading with EAGAIN Handling
```cpp
char buffer[4096];
ssize_t bytes = recv(fd, buffer, sizeof(buffer), 0);

if (bytes > 0) {
    // Process received data
    requestBuffer.append(buffer, bytes);
} else if (bytes == 0) {
    // Connection closed by client
    closeConnection(fd);
} else {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // No data available, continue monitoring
        return;
    }
    // Real error occurred
    handleError(fd);
}
```

## Project Goals
- Create a robust, RFC-compliant HTTP/1.0 server
- Handle real-world scenarios and edge cases
- Demonstrate deep understanding of network programming
- Write clean, maintainable C++98 code
- Pass all 42 evaluation requirements
- Be resilient and never crash under normal or stress conditions
