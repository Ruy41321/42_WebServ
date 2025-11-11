# 42_WebServ

A simple HTTP server written in C++ 98.

## Building

To build the project, run:

```bash
make
```

This will create the `webserv` executable.

## Usage

Run the server with a configuration file:

```bash
./webserv [configuration file]
```

Example:

```bash
./webserv config/default.conf
```

## Configuration File

The configuration file uses a simple key=value format. See `config/default.conf` for an example.

Available options:
- `port`: Server port (default: 8080)
- `host`: Server host address (default: 127.0.0.1)
- `root`: Root directory for serving files (default: ./www)
- `index`: Default index file (default: index.html)

## Testing

Once the server is running, you can test it with curl:

```bash
curl http://127.0.0.1:8080/
```

Or open http://127.0.0.1:8080/ in a web browser.

## Cleaning

To clean build artifacts:

```bash
make clean   # Remove object files
make fclean  # Remove object files and executable
make re      # Rebuild from scratch
```