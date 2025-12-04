#!/bin/bash
# CGI Test Script for WebServ
# This script tests all CGI functionality

# Don't use set -e as it causes issues with arithmetic in bash
# set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
HOST="127.0.0.1"
PORT="8084" #the port where the server specified for cgi test is running
BASE_URL="http://${HOST}:${PORT}"
CONFIG_FILE="config/default.conf"
SERVER_STARTED=false

# Counters
PASSED=0
FAILED=0

# Helper functions
pass_test() {
    echo -e "${GREEN}[PASS]${NC} $1"
    PASSED=$((PASSED + 1))
}

fail_test() {
    echo -e "${RED}[FAIL]${NC} $1"
    echo "       Expected: $2"
    echo "       Got: $3"
    FAILED=$((FAILED + 1))
}

warn_test() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

# Cleanup function
cleanup() {
    echo -e "\n${BLUE}Cleaning up...${NC}"
    if [ "$SERVER_STARTED" = true ]; then
        echo "Stopping webserv server..."
        pkill -9 webserv 2>/dev/null || true
        sleep 1
    fi
}

trap cleanup EXIT

# Check if php-cgi is installed
check_php_cgi() {
    echo "=== Checking PHP-CGI Installation ==="
    if command -v php-cgi &> /dev/null; then
        echo "php-cgi found at: $(which php-cgi)"
        PHP_CGI_AVAILABLE=true
    else
        warn_test "php-cgi not installed. Install with: sudo apt install php-cgi"
        PHP_CGI_AVAILABLE=false
    fi
    echo ""
}

# Check if python3 is available
check_python() {
    echo "=== Checking Python Installation ==="
    if command -v python3 &> /dev/null; then
        echo "python3 found at: $(which python3)"
        PYTHON_AVAILABLE=true
    else
        warn_test "python3 not found"
        PYTHON_AVAILABLE=false
    fi
    echo ""
}

# Check if server is running
check_server() {
    if curl -s -o /dev/null -w "%{http_code}" "$BASE_URL" 2>/dev/null | grep -q "200\|404"; then
        return 0
    else
        return 1
    fi
}

# Start server if not already running
start_server() {
    echo "=== Starting Server ==="
    
    # Check if server is already running
    if check_server; then
        echo "Server already running, skipping startup"
        return 0
    fi
    
    # Kill any existing webserv processes
    pkill -9 webserv 2>/dev/null || true
    sleep 1
    
    # Start the server
    if [ ! -f "./webserv" ]; then
        echo -e "${RED}Error: webserv executable not found${NC}"
        echo "Please build the project first with: make"
        return 1
    fi
    
    if [ ! -f "$CONFIG_FILE" ]; then
        echo -e "${RED}Error: config file not found: $CONFIG_FILE${NC}"
        return 1
    fi
    
    echo "Starting webserv with config: $CONFIG_FILE"
    ./webserv "$CONFIG_FILE" > /tmp/webserv_cgi_test.log 2>&1 &
    SERVER_PID=$!
    SERVER_STARTED=true
    
    # Wait for server to start
    sleep 2
    
    # Verify server is running
    if ! ps -p $SERVER_PID > /dev/null 2>&1; then
        echo -e "${RED}Failed to start server${NC}"
        cat /tmp/webserv_cgi_test.log
        return 1
    fi
    
    # Wait for server to be responsive
    MAX_ATTEMPTS=10
    ATTEMPT=0
    while [ $ATTEMPT -lt $MAX_ATTEMPTS ]; do
        if check_server 2>/dev/null; then
            echo -e "${GREEN}Server started successfully (PID: $SERVER_PID)${NC}"
            return 0
        fi
        ATTEMPT=$((ATTEMPT + 1))
        sleep 1
    done
    
    echo -e "${RED}Server did not become responsive${NC}"
    return 1
}

# Test PHP CGI GET request
test_php_get() {
    echo "=== Test: PHP CGI GET Request ==="
    
    if [ "$PHP_CGI_AVAILABLE" != "true" ]; then
        warn_test "Skipping PHP tests - php-cgi not installed"
        return
    fi
    
    RESPONSE=$(curl -s "${BASE_URL}/cgi-bin/test.php?name=test&value=123")
    
    if echo "$RESPONSE" | grep -q "PHP CGI Test Page"; then
        pass_test "PHP CGI basic response"
    else
        fail_test "PHP CGI basic response" "Contains 'PHP CGI Test Page'" "$RESPONSE"
    fi
    
    if echo "$RESPONSE" | grep -q "REQUEST_METHOD"; then
        pass_test "PHP CGI environment variables"
    else
        fail_test "PHP CGI environment variables" "Contains 'REQUEST_METHOD'" "Not found"
    fi
    
    if echo "$RESPONSE" | grep -q "GET"; then
        pass_test "PHP CGI REQUEST_METHOD=GET"
    else
        fail_test "PHP CGI REQUEST_METHOD=GET" "Contains 'GET'" "Not found"
    fi
    
    if echo "$RESPONSE" | grep -q "name"; then
        pass_test "PHP CGI query string parsing"
    else
        fail_test "PHP CGI query string parsing" "Contains query params" "Not found"
    fi
    
    echo ""
}

# Test PHP CGI POST request
test_php_post() {
    echo "=== Test: PHP CGI POST Request ==="
    
    if [ "$PHP_CGI_AVAILABLE" != "true" ]; then
        warn_test "Skipping PHP POST test - php-cgi not installed"
        return
    fi
    
    RESPONSE=$(curl -s -X POST -d "username=testuser&message=Hello%20CGI" \
        -H "Content-Type: application/x-www-form-urlencoded" \
        "${BASE_URL}/cgi-bin/test.php")
    
    if echo "$RESPONSE" | grep -q "POST"; then
        pass_test "PHP CGI POST method detected"
    else
        fail_test "PHP CGI POST method" "Contains 'POST'" "Not found"
    fi
    
    if echo "$RESPONSE" | grep -q "POST Data"; then
        pass_test "PHP CGI POST data section"
    else
        fail_test "PHP CGI POST data section" "Contains 'POST Data'" "Not found"
    fi
    
    echo ""
}

# Test Python CGI GET request
test_python_get() {
    echo "=== Test: Python CGI GET Request ==="
    
    if [ "$PYTHON_AVAILABLE" != "true" ]; then
        warn_test "Skipping Python tests - python3 not installed"
        return
    fi
    
    RESPONSE=$(curl -s "${BASE_URL}/cgi-bin/test.py?param1=value1&param2=value2")
    
    if echo "$RESPONSE" | grep -q "Python CGI Test Page"; then
        pass_test "Python CGI basic response"
    else
        fail_test "Python CGI basic response" "Contains 'Python CGI Test Page'" "$RESPONSE"
    fi
    
    if echo "$RESPONSE" | grep -q "REQUEST_METHOD"; then
        pass_test "Python CGI environment variables"
    else
        fail_test "Python CGI environment variables" "Contains 'REQUEST_METHOD'" "Not found"
    fi
    
    echo ""
}

# Test Python CGI POST request
test_python_post() {
    echo "=== Test: Python CGI POST Request ==="
    
    if [ "$PYTHON_AVAILABLE" != "true" ]; then
        warn_test "Skipping Python POST test - python3 not installed"
        return
    fi
    
    RESPONSE=$(curl -s -X POST -d "name=testuser&message=Hello%20Python%20CGI" \
        -H "Content-Type: application/x-www-form-urlencoded" \
        "${BASE_URL}/cgi-bin/test.py")
    
    if echo "$RESPONSE" | grep -q "POST"; then
        pass_test "Python CGI POST method detected"
    else
        fail_test "Python CGI POST method" "Contains 'POST'" "Not found"
    fi
    
    echo ""
}

# Test CGI 404 (script not found)
test_cgi_404() {
    echo "=== Test: CGI Script Not Found ==="
    
    RESPONSE=$(curl -s -w "%{http_code}" -o /tmp/cgi_404.html "${BASE_URL}/cgi-bin/nonexistent.php")
    
    if [ "$RESPONSE" = "404" ]; then
        pass_test "CGI 404 for non-existent script"
    else
        fail_test "CGI 404 for non-existent script" "404" "$RESPONSE"
    fi
    
    echo ""
}

# Test CGI error handling
test_cgi_error() {
    echo "=== Test: CGI Error Handling ==="
    
    if [ "$PHP_CGI_AVAILABLE" != "true" ]; then
        warn_test "Skipping PHP error test - php-cgi not installed"
        return
    fi
    
    # Test the error.php (should work normally)
    HTTP_CODE=$(curl -s -w "%{http_code}" -o /tmp/cgi_error.html "${BASE_URL}/cgi-bin/error.php")
    
    if [ "$HTTP_CODE" = "200" ]; then
        pass_test "CGI error.php returns 200 (working mode)"
    else
        fail_test "CGI error.php" "200" "$HTTP_CODE"
    fi
    
    echo ""
}

# Test CGI timeout (this will take time!)
test_cgi_timeout() {
    echo "=== Test: CGI Timeout (30 second test) ==="
    echo "Starting timeout test... (this will take ~30 seconds)"
    
    if [ "$PHP_CGI_AVAILABLE" != "true" ]; then
        warn_test "Skipping timeout test - php-cgi not installed"
        return
    fi
    
    # Start timing
    START_TIME=$(date +%s)
    
    # Request infinite loop script with timeout
    HTTP_CODE=$(curl -s -w "%{http_code}" -o /tmp/cgi_timeout.html \
        --max-time 60 "${BASE_URL}/cgi-bin/infinite.php")
    
    END_TIME=$(date +%s)
    ELAPSED=$((END_TIME - START_TIME))
    
    echo "Request completed in ${ELAPSED} seconds"
    
    if [ "$HTTP_CODE" = "504" ]; then
        pass_test "CGI timeout returns 504"
    elif [ "$HTTP_CODE" = "500" ]; then
        pass_test "CGI timeout handled (returned 500)"
    elif [ "$ELAPSED" -gt 25 ] && [ "$ELAPSED" -lt 40 ]; then
        pass_test "CGI timeout occurred within expected range"
    else
        fail_test "CGI timeout" "504 or ~30 second timeout" "Code: $HTTP_CODE, Time: ${ELAPSED}s"
    fi
    
    echo ""
}

# Test HTTP headers from CGI
test_cgi_headers() {
    echo "=== Test: CGI HTTP Headers ==="
    
    if [ "$PHP_CGI_AVAILABLE" != "true" ]; then
        warn_test "Skipping header test - php-cgi not installed"
        return
    fi
    
    # Use -i to include headers (not -I which sends HEAD request)
    # CGI location only allows GET and POST, not HEAD
    HEADERS=$(curl -s -i "${BASE_URL}/cgi-bin/test.php" 2>/dev/null || true)
    
    if echo "$HEADERS" | grep -qi "Content-Type"; then
        pass_test "CGI Content-Type header present"
    else
        fail_test "CGI Content-Type header" "Contains 'Content-Type'" "Not found"
    fi
    
    if echo "$HEADERS" | grep -qi "HTTP/1\.[01] 200"; then
        pass_test "CGI returns HTTP 200"
    else
        fail_test "CGI HTTP status" "HTTP 200" "$(echo "$HEADERS" | head -1)"
    fi
    
    echo ""
}

# Main test runner
main() {
    echo "========================================"
    echo "   WebServ CGI Test Suite"
    echo "========================================"
    echo ""
    
    check_php_cgi
    check_python
    
    if ! start_server; then
        echo ""
        echo "Cannot start server. Aborting tests."
        exit 1
    fi
    
    echo ""
    echo "Running tests..."
    echo ""
    
    test_php_get
    test_php_post
    test_python_get
    test_python_post
    test_cgi_404
    test_cgi_error
    test_cgi_headers
    
    # Timeout test is slow, run it last
    read -p "Run timeout test? (takes ~30 seconds) [y/N] " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        test_cgi_timeout
    fi
    
    echo ""
    echo "========================================"
    echo "   Test Results"
    echo "========================================"
    echo -e "Passed: ${GREEN}${PASSED}${NC}"
    echo -e "Failed: ${RED}${FAILED}${NC}"
    echo ""
    
    if [ $FAILED -eq 0 ]; then
        echo -e "${GREEN}All tests passed!${NC}"
        exit 0
    else
        echo -e "${RED}Some tests failed.${NC}"
        exit 1
    fi
}

# Run main
main "$@"
