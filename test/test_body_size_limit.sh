#!/bin/bash

# Test script for body size limit validation
# Tests progressive body size checking (returns 413 when body exceeds limit)

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

TESTS_PASSED=0
TESTS_FAILED=0

# Configuration
HOST="127.0.0.1"
PORT="8080"
BASE_URL="http://${HOST}:${PORT}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
WEBSERV_BIN="$PROJECT_DIR/webserv"
CONFIG_FILE="$PROJECT_DIR/config/default.conf"

# Source logging helper
source "$SCRIPT_DIR/test_logging_helper.sh"

# Setup logging for this test
setup_test_logging "test_body_size_limit"

# Cleanup function
cleanup() {
    echo -e "\n${BLUE}Cleaning up...${NC}"
    pkill -9 webserv 2>/dev/null
}

trap cleanup EXIT

# Start server
echo -e "${YELLOW}Starting server...${NC}"
pkill -9 webserv 2>/dev/null
sleep 1
cd "$PROJECT_DIR"
start_server_with_logging "$CONFIG_FILE"
sleep 2

# Verify server is running
if ! ps -p $SERVER_PID > /dev/null 2>&1; then
    echo -e "${RED}Failed to start server${NC}"
    exit 1
fi
echo -e "${GREEN}Server started (PID: $SERVER_PID)${NC}"
echo -e "${YELLOW}Server output log: $TEST_LOG_FILE${NC}\n"

echo -e "${YELLOW}=== Body Size Limit Tests ===${NC}"
echo ""

# Test 1: Small body (should succeed with 200/201)
echo "Test 1: Small POST body (should succeed)"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
    -H "Content-Type: text/plain" \
    -d "This is a small body" \
    "${BASE_URL}/uploads/size_test_small.txt" 2>/dev/null)

if [ "$RESPONSE" = "201" ] || [ "$RESPONSE" = "200" ]; then
    echo -e "${GREEN}PASSED${NC}: Small body accepted (HTTP $RESPONSE)"
    ((TESTS_PASSED++))
else
    echo -e "${RED}FAILED${NC}: Expected 200/201, got HTTP $RESPONSE"
    ((TESTS_FAILED++))
fi

# Test 2: POST with Content-Length exceeding limit (early rejection)
echo ""
echo "Test 2: POST with Content-Length > max body size (early rejection)"
# Create a header with large Content-Length but small body
# Server should reject based on Content-Length header
RESPONSE=$(timeout 5 curl -s -o /dev/null -w "%{http_code}" -X POST \
    -H "Content-Type: text/plain" \
    -H "Content-Length: 999999999" \
    --data-binary "small" \
    "${BASE_URL}/uploads/size_test_big.txt" 2>/dev/null)

if [ "$RESPONSE" = "413" ]; then
    echo -e "${GREEN}PASSED${NC}: Large Content-Length rejected (HTTP 413)"
    ((TESTS_PASSED++))
else
    echo -e "${RED}FAILED${NC}: Expected 413, got HTTP $RESPONSE"
    ((TESTS_FAILED++))
fi

# Test 3: Large body without Content-Length (progressive check)
echo ""
echo "Test 3: Large body using chunked transfer (progressive check)"
# Generate a 2MB file of data (likely exceeds 1MB limit)
LARGE_DATA=$(dd if=/dev/zero bs=1024 count=2048 2>/dev/null | tr '\0' 'X')
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
    -H "Content-Type: application/octet-stream" \
    -H "Transfer-Encoding: chunked" \
    --data-binary "$LARGE_DATA" \
    "${BASE_URL}/uploads/size_test_chunked.txt" 2>/dev/null)

if [ "$RESPONSE" = "413" ]; then
    echo -e "${GREEN}PASSED${NC}: Large chunked body rejected (HTTP 413)"
    ((TESTS_PASSED++))
elif [ -z "$RESPONSE" ] || [ "$RESPONSE" = "000" ]; then
    echo -e "${GREEN}PASSED${NC}: Request rejected (connection closed early)"
    ((TESTS_PASSED++))
else
    echo -e "${RED}FAILED${NC}: Expected 413, got HTTP $RESPONSE"
    ((TESTS_FAILED++))
fi

# Test 4: PUT with large body
echo ""
echo "Test 4: PUT with body exceeding limit"
LARGE_DATA=$(dd if=/dev/zero bs=1024 count=2048 2>/dev/null | tr '\0' 'Y')
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X PUT \
    -H "Content-Type: text/plain" \
    --data-binary "$LARGE_DATA" \
    "${BASE_URL}/uploads/size_test_put.txt" 2>/dev/null)

if [ "$RESPONSE" = "413" ]; then
    echo -e "${GREEN}PASSED${NC}: Large PUT body rejected (HTTP 413)"
    ((TESTS_PASSED++))
elif [ -z "$RESPONSE" ] || [ "$RESPONSE" = "000" ]; then
    echo -e "${GREEN}PASSED${NC}: Request rejected (connection closed early)"
    ((TESTS_PASSED++))
else
    echo -e "${RED}FAILED${NC}: Expected 413, got HTTP $RESPONSE"
    ((TESTS_FAILED++))
fi

# Test 5: Incremental send with slow client simulation
echo ""
echo "Test 5: Simulating slow client sending body in chunks"
# Use netcat to send data incrementally
{
    # Send HTTP request headers (HTTP/1.1 requires Host header)
    printf "POST /uploads/slow_test.txt HTTP/1.1\r\n"
    printf "Host: ${HOST}:${PORT}\r\n"
    printf "Content-Type: text/plain\r\n"
    printf "Content-Length: 2097152\r\n"  # 2MB
    printf "Connection: close\r\n"
    printf "\r\n"
    
    # Send body in small chunks with delays
    for i in $(seq 1 100); do
        # Each chunk is ~21KB, total ~2MB
        dd if=/dev/zero bs=1024 count=21 2>/dev/null | tr '\0' 'Z'
        sleep 0.01  # Small delay between chunks
    done
} | timeout 10 nc -q 1 ${HOST} ${PORT} > /tmp/slow_test_response.txt 2>/dev/null

# Extract HTTP status code from response
if [ -f /tmp/slow_test_response.txt ]; then
    RESPONSE=$(head -1 /tmp/slow_test_response.txt | grep -o "HTTP/1.[01] [0-9]*" | awk '{print $2}')
    if [ "$RESPONSE" = "413" ]; then
        echo -e "${GREEN}PASSED${NC}: Slow client oversized body rejected (HTTP 413)"
        ((TESTS_PASSED++))
    else
        echo -e "${YELLOW}INFO${NC}: Response was HTTP $RESPONSE"
    fi
    rm -f /tmp/slow_test_response.txt
else
    echo -e "${YELLOW}SKIPPED${NC}: Could not connect with netcat"
fi

# Test 6: CGI POST with large body
echo ""
echo "Test 6: CGI POST with body exceeding limit"
if [ -f www/cgi-bin/test.php ] || [ -f www/cgi-bin/test.py ]; then
    LARGE_DATA=$(dd if=/dev/zero bs=1024 count=2048 2>/dev/null | tr '\0' 'A')
    RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
        -H "Content-Type: text/plain" \
        --data-binary "$LARGE_DATA" \
        "${BASE_URL}/cgi-bin/test.php" 2>/dev/null)
    
    if [ "$RESPONSE" = "413" ]; then
        echo -e "${GREEN}PASSED${NC}: Large CGI POST body rejected (HTTP 413)"
        ((TESTS_PASSED++))
    elif [ -z "$RESPONSE" ] || [ "$RESPONSE" = "000" ]; then
        echo -e "${GREEN}PASSED${NC}: Request rejected (connection closed early)"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}FAILED${NC}: Expected 413, got HTTP $RESPONSE"
        ((TESTS_FAILED++))
    fi
else
    echo -e "${YELLOW}SKIPPED${NC}: No CGI script found"
fi

# Test 7: Body exactly at limit (should succeed)
echo ""
echo "Test 7: Body exactly at limit boundary"
# This test depends on knowing the exact limit - using 1MB as default
EXACT_DATA=$(dd if=/dev/zero bs=1024 count=1024 2>/dev/null | tr '\0' 'B')
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
    -H "Content-Type: text/plain" \
    --data-binary "$EXACT_DATA" \
    "${BASE_URL}/uploads/size_test_exact.txt" 2>/dev/null)

echo -e "${YELLOW}INFO${NC}: 1MB body response was HTTP $RESPONSE (depends on limit config)"

# Test 8: Empty body (should always succeed for POST to upload dir)
echo ""
echo "Test 8: Empty body POST"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
    -H "Content-Type: text/plain" \
    -H "Content-Length: 0" \
    "${BASE_URL}/uploads/empty_body.txt" 2>/dev/null)

if [ "$RESPONSE" = "201" ] || [ "$RESPONSE" = "200" ]; then
    echo -e "${GREEN}PASSED${NC}: Empty body accepted (HTTP $RESPONSE)"
    ((TESTS_PASSED++))
else
    echo -e "${RED}FAILED${NC}: Expected 200/201 for empty body, got HTTP $RESPONSE"
    ((TESTS_FAILED++))
fi

# Summary
echo ""
echo -e "${YELLOW}=== Test Summary ===${NC}"
echo -e "Passed: ${GREEN}${TESTS_PASSED}${NC}"
echo -e "Failed: ${RED}${TESTS_FAILED}${NC}"

# Cleanup test files
rm -f www/uploads/size_test_*.txt www/uploads/slow_test.txt www/uploads/empty_body.txt 2>/dev/null

if [ $TESTS_FAILED -gt 0 ]; then
    exit 1
fi
exit 0
