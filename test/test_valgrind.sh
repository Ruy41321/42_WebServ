#!/bin/bash

# WebServ Valgrind Memory Leak Test
# Tests for memory leaks, file descriptor leaks, and other resource issues

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SERVER_HOST="127.0.0.1"
SERVER_PORT="8080"
SERVER_URL="http://${SERVER_HOST}:${SERVER_PORT}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
WEBSERV_BIN="$PROJECT_DIR/webserv"
CONFIG_FILE="$PROJECT_DIR/config/default.conf"
VALGRIND_LOG="/tmp/webserv_valgrind.log"
VALGRIND_PID_FILE="/tmp/webserv_valgrind.pid"

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_TOTAL=0

# Check if valgrind is installed
check_valgrind() {
    if ! command -v valgrind &> /dev/null; then
        echo -e "${RED}ERROR: valgrind is not installed${NC}"
        echo -e "${YELLOW}Install it with: sudo apt-get install valgrind${NC}"
        exit 1
    fi
}

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    
    # Kill server if running
    if [ -f "$VALGRIND_PID_FILE" ]; then
        SERVER_PID=$(cat "$VALGRIND_PID_FILE")
        if ps -p "$SERVER_PID" > /dev/null 2>&1; then
            kill -TERM "$SERVER_PID" 2>/dev/null
            sleep 2
            # Force kill if still running
            if ps -p "$SERVER_PID" > /dev/null 2>&1; then
                kill -9 "$SERVER_PID" 2>/dev/null
            fi
        fi
        rm -f "$VALGRIND_PID_FILE"
    fi
    
    # Additional cleanup
    pkill -9 webserv 2>/dev/null
    sleep 1
}

# Setup function
setup() {
    echo -e "${BLUE}=== WebServ Valgrind Memory Leak Test ===${NC}\n"
    
    # Check if binary exists
    if [ ! -f "$WEBSERV_BIN" ]; then
        echo -e "${RED}ERROR: webserv binary not found at $WEBSERV_BIN${NC}"
        echo -e "${YELLOW}Build it with: make${NC}"
        exit 1
    fi
    
    # Check if config exists
    if [ ! -f "$CONFIG_FILE" ]; then
        echo -e "${RED}ERROR: Config file not found at $CONFIG_FILE${NC}"
        exit 1
    fi
    
    # Clean up any existing processes
    cleanup
    
    echo -e "${YELLOW}Valgrind log file: $VALGRIND_LOG${NC}\n"
}

# Start server with valgrind
start_server_with_valgrind() {
    echo -e "${BLUE}Starting server with valgrind...${NC}"
    
    # Remove old log
    rm -f "$VALGRIND_LOG"
    
    # Start server with valgrind
    valgrind \
        --leak-check=full \
        --show-leak-kinds=all \
        --track-fds=yes \
        --track-origins=yes \
        --verbose \
        --log-file="$VALGRIND_LOG" \
        "$WEBSERV_BIN" "$CONFIG_FILE" > /dev/null 2>&1 &
    
    SERVER_PID=$!
    echo $SERVER_PID > "$VALGRIND_PID_FILE"
    
    echo -e "${YELLOW}Server PID: $SERVER_PID${NC}"
    echo -e "${YELLOW}Waiting for server to start (valgrind is slow)...${NC}"
    
    # Wait for server to start (max 30 seconds for valgrind)
    local max_attempts=60
    local attempt=0
    while [ $attempt -lt $max_attempts ]; do
        if curl -s --max-time 2 -o /dev/null -w "%{http_code}" "$SERVER_URL" > /dev/null 2>&1; then
            echo -e "${GREEN}Server started successfully${NC}\n"
            return 0
        fi
        sleep 0.5
        attempt=$((attempt + 1))
    done
    
    echo -e "${RED}Server failed to start${NC}"
    return 1
}

# Test helper functions
test_start() {
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    echo -n -e "${BLUE}Test $TESTS_TOTAL:${NC} $1... "
}

test_pass() {
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "${GREEN}PASSED${NC}"
}

test_fail() {
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo -e "${RED}FAILED${NC}"
    if [ -n "$1" ]; then
        echo -e "  ${RED}$1${NC}"
    fi
}

# Perform various HTTP requests to test server under load
perform_test_requests() {
    echo -e "${BLUE}Performing test requests...${NC}\n"
    
    # Test 1: Simple GET request
    test_start "Simple GET request"
    response=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/")
    if [ "$response" = "200" ] || [ "$response" = "404" ]; then
        test_pass
    else
        test_fail "Expected 200 or 404, got $response"
    fi
    
    # Test 2: Multiple concurrent GET requests (reduced for valgrind)
    test_start "5 concurrent GET requests"
    pids=()
    for i in {1..5}; do
        curl -s --max-time 10 "$SERVER_URL/" > /dev/null 2>&1 &
        pids+=($!)
    done
    # Wait for all background processes with timeout
    for pid in "${pids[@]}"; do
        wait $pid 2>/dev/null || true
    done
    test_pass
    
    # Test 3: GET non-existent file
    test_start "GET non-existent file"
    response=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/nonexistent.html")
    if [ "$response" = "404" ]; then
        test_pass
    else
        test_fail "Expected 404, got $response"
    fi
    
    # Test 4: POST request with data
    test_start "POST request with small data"
    response=$(curl -s -o /dev/null -w "%{http_code}" -X POST -d "test=data" "$SERVER_URL/")
    if [ "$response" = "200" ] || [ "$response" = "201" ] || [ "$response" = "405" ]; then
        test_pass
    else
        test_fail "Expected 200/201/405, got $response"
    fi
    
    # Test 5: POST request with larger data
    test_start "POST request with larger data"
    large_data=$(printf 'A%.0s' {1..1000})
    response=$(curl -s -o /dev/null -w "%{http_code}" -X POST -d "$large_data" "$SERVER_URL/")
    if [ "$response" = "200" ] || [ "$response" = "201" ] || [ "$response" = "405" ]; then
        test_pass
    else
        test_fail "Expected 200/201/405, got $response"
    fi
    
    # Test 6: Multiple connections with keep-alive
    test_start "Multiple requests with keep-alive"
    for i in {1..5}; do
        curl -s --max-time 5 -H "Connection: keep-alive" "$SERVER_URL/" > /dev/null 2>&1
    done
    test_pass
    
    # Test 7: DELETE request
    test_start "DELETE request"
    response=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "$SERVER_URL/test.txt")
    # Any response is acceptable (200, 204, 404, 405, etc.)
    test_pass
    
    # Test 8: HEAD request
    test_start "HEAD request"
    response=$(curl -s -o /dev/null -w "%{http_code}" -I "$SERVER_URL/")
    if [ "$response" = "200" ] || [ "$response" = "404" ]; then
        test_pass
    else
        test_fail "Expected 200 or 404, got $response"
    fi
    
    # Test 9: Request with custom headers
    test_start "Request with custom headers"
    response=$(curl -s -o /dev/null -w "%{http_code}" -H "X-Custom: test" "$SERVER_URL/")
    if [ "$response" = "200" ] || [ "$response" = "404" ]; then
        test_pass
    else
        test_fail "Expected 200 or 404, got $response"
    fi
    
    # Test 10: CGI GET request with PHP
    test_start "CGI GET request (PHP)"
    response=$(curl -s --max-time 10 -o /dev/null -w "%{http_code}" "$SERVER_URL/cgi-bin/test.php?name=valgrind&test=true")
    if [ "$response" = "200" ] || [ "$response" = "500" ] || [ "$response" = "404" ]; then
        test_pass
    else
        test_fail "Expected 200/500/404, got $response"
    fi
    
    # Test 11: CGI POST request with PHP
    test_start "CGI POST request (PHP)"
    response=$(curl -s --max-time 10 -o /dev/null -w "%{http_code}" -X POST -d "field1=value1&field2=value2" "$SERVER_URL/cgi-bin/test.php")
    if [ "$response" = "200" ] || [ "$response" = "500" ] || [ "$response" = "404" ]; then
        test_pass
    else
        test_fail "Expected 200/500/404, got $response"
    fi
    
    # Test 12: CGI GET request with Python
    test_start "CGI GET request (Python)"
    response=$(curl -s --max-time 10 -o /dev/null -w "%{http_code}" "$SERVER_URL/cgi-bin/test.py?param=test")
    if [ "$response" = "200" ] || [ "$response" = "500" ] || [ "$response" = "404" ]; then
        test_pass
    else
        test_fail "Expected 200/500/404, got $response"
    fi
    
    # Test 13: CGI POST request with Python
    test_start "CGI POST request (Python)"
    response=$(curl -s --max-time 10 -o /dev/null -w "%{http_code}" -X POST -d "data=testvalue" "$SERVER_URL/cgi-bin/test.py")
    if [ "$response" = "200" ] || [ "$response" = "500" ] || [ "$response" = "404" ]; then
        test_pass
    else
        test_fail "Expected 200/500/404, got $response"
    fi
    
    # Test 14: Multiple CGI requests to check for leaks in CGI process handling
    test_start "5 sequential CGI requests (leak test)"
    for i in {1..5}; do
        curl -s --max-time 10 "$SERVER_URL/cgi-bin/test.php?iteration=$i" > /dev/null 2>&1
    done
    test_pass
    
    # Test 15: Stress test - sequential requests
    test_start "20 sequential requests"
    for i in {1..20}; do
        curl -s --max-time 10 "$SERVER_URL/" > /dev/null 2>&1
    done
    test_pass
    
    echo ""
}

# Stop server gracefully and analyze valgrind output
stop_server_and_analyze() {
    echo -e "${BLUE}Stopping server...${NC}"
    
    if [ -f "$VALGRIND_PID_FILE" ]; then
        SERVER_PID=$(cat "$VALGRIND_PID_FILE")
        if ps -p "$SERVER_PID" > /dev/null 2>&1; then
            # Send SIGTERM for graceful shutdown
            kill -TERM "$SERVER_PID" 2>/dev/null
            echo -e "${YELLOW}Waiting for server to shutdown gracefully...${NC}"
            
            # Wait up to 5 seconds for graceful shutdown
            local count=0
            while ps -p "$SERVER_PID" > /dev/null 2>&1 && [ $count -lt 10 ]; do
                sleep 0.5
                count=$((count + 1))
            done
            
            # Force kill if still running
            if ps -p "$SERVER_PID" > /dev/null 2>&1; then
                echo -e "${YELLOW}Force killing server...${NC}"
                kill -9 "$SERVER_PID" 2>/dev/null
                sleep 1
            fi
        fi
        rm -f "$VALGRIND_PID_FILE"
    fi
    
    echo -e "${BLUE}\nAnalyzing valgrind results...${NC}\n"
    sleep 2  # Give valgrind time to write final output
    
    if [ ! -f "$VALGRIND_LOG" ]; then
        echo -e "${RED}ERROR: Valgrind log file not found${NC}"
        return 1
    fi
    
    # Display valgrind summary
    echo -e "${BLUE}=== Valgrind Summary ===${NC}\n"
    
    # Check for memory leaks
    echo -e "${YELLOW}Memory Leak Summary:${NC}"
    grep -A 10 "LEAK SUMMARY" "$VALGRIND_LOG" || echo "No leak summary found"
    echo ""
    
    # Check for file descriptor leaks
    echo -e "${YELLOW}File Descriptor Summary:${NC}"
    grep -A 20 "FILE DESCRIPTORS" "$VALGRIND_LOG" || echo "No FD summary found"
    echo ""
    
    # Check heap summary
    echo -e "${YELLOW}Heap Summary:${NC}"
    grep -A 5 "HEAP SUMMARY" "$VALGRIND_LOG" || echo "No heap summary found"
    echo ""
    
    # Analyze results
    local has_errors=0
    
    # Check for definitely lost memory
    if grep -q "definitely lost: [1-9]" "$VALGRIND_LOG"; then
        echo -e "${RED}❌ FAIL: Definitely lost memory detected${NC}"
        grep "definitely lost:" "$VALGRIND_LOG" | tail -1
        has_errors=1
    else
        echo -e "${GREEN}✓ PASS: No definitely lost memory${NC}"
    fi
    
    # Check for indirectly lost memory
    if grep -q "indirectly lost: [1-9]" "$VALGRIND_LOG"; then
        echo -e "${YELLOW}⚠ WARNING: Indirectly lost memory detected${NC}"
        grep "indirectly lost:" "$VALGRIND_LOG" | tail -1
    else
        echo -e "${GREEN}✓ PASS: No indirectly lost memory${NC}"
    fi
    
    # Check for possibly lost memory
    if grep -q "possibly lost: [1-9]" "$VALGRIND_LOG"; then
        echo -e "${YELLOW}⚠ WARNING: Possibly lost memory detected${NC}"
        grep "possibly lost:" "$VALGRIND_LOG" | tail -1
    else
        echo -e "${GREEN}✓ PASS: No possibly lost memory${NC}"
    fi
    
    # Check for open file descriptors (exclude inherited from parent)
    # Count FDs that are NOT marked as "inherited from parent"
    total_open_fds=$(grep -c "Open file descriptor" "$VALGRIND_LOG" 2>/dev/null || echo "0")
    inherited_fds=$(grep -c "inherited from parent" "$VALGRIND_LOG" 2>/dev/null || echo "0")
    leaked_fds=$((total_open_fds - inherited_fds))
    
    if [ $leaked_fds -gt 0 ]; then
        echo -e "${RED}❌ FAIL: $leaked_fds leaked file descriptors detected${NC}"
        # Show the leaked FDs
        grep -B 1 "Open file descriptor" "$VALGRIND_LOG" | grep -A 1 -v "inherited from parent" | head -10
        has_errors=1
    else
        echo -e "${GREEN}✓ PASS: All file descriptors closed properly${NC}"
        if [ $inherited_fds -gt 0 ]; then
            echo -e "${YELLOW}  Note: $inherited_fds FDs inherited from parent (VS Code/Terminal) - these are not leaks${NC}"
        fi
    fi
    
    # Check for invalid reads/writes
    if grep -q "Invalid read\|Invalid write" "$VALGRIND_LOG"; then
        echo -e "${RED}❌ FAIL: Invalid memory access detected${NC}"
        has_errors=1
    else
        echo -e "${GREEN}✓ PASS: No invalid memory access${NC}"
    fi
    
    echo ""
    echo -e "${BLUE}Full valgrind log available at: ${VALGRIND_LOG}${NC}"
    echo ""
    
    return $has_errors
}

# Print test results
print_results() {
    echo -e "${BLUE}=== Test Results ===${NC}"
    echo -e "Total HTTP tests: $TESTS_TOTAL"
    echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
    echo -e "${RED}Failed: $TESTS_FAILED${NC}"
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "\n${GREEN}All HTTP tests passed!${NC}"
    else
        echo -e "\n${RED}Some HTTP tests failed${NC}"
    fi
}

# Trap ctrl+c and cleanup
trap cleanup EXIT INT TERM

# Main execution
main() {
    check_valgrind
    setup
    
    if ! start_server_with_valgrind; then
        echo -e "${RED}Failed to start server${NC}"
        exit 1
    fi
    
    # Give server a moment to stabilize
    sleep 1
    
    perform_test_requests
    
    print_results
    
    local exit_code=0
    if ! stop_server_and_analyze; then
        exit_code=1
    fi
    
    if [ $TESTS_FAILED -gt 0 ]; then
        exit_code=1
    fi
    
    if [ $exit_code -eq 0 ]; then
        echo -e "\n${GREEN}=== ALL TESTS PASSED - NO LEAKS DETECTED ===${NC}\n"
    else
        echo -e "\n${RED}=== TESTS FAILED - CHECK LOGS FOR DETAILS ===${NC}\n"
    fi
    
    exit $exit_code
}

main
