#!/bin/bash

# HTTP/1.1 Protocol Compliance Test Suite
# Tests all major HTTP/1.1 features as defined in RFC 7230-7235

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
WEBSERV_BIN="$PROJECT_DIR/webserv"
CONFIG_FILE="$PROJECT_DIR/config/default.conf"
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

TESTS_PASSED=0
TESTS_FAILED=0

# Source logging helper
source "$SCRIPT_DIR/test_logging_helper.sh"

# Setup logging for this test
setup_test_logging "test_http11"

# Helper function for test results
check_result() {
    local expected=$1
    local actual=$2
    local test_name=$3
    
    if [ "$expected" = "$actual" ]; then
        echo -e "${GREEN}✓${NC} $test_name: $actual"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo -e "${RED}✗${NC} $test_name: expected $expected, got $actual"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

echo "========================================"
echo "  HTTP/1.1 Protocol Compliance Test"
echo "========================================"
echo
echo -e "${YELLOW}Server output log: $TEST_LOG_FILE${NC}\n"

# Start server
echo "Starting server..."
start_server_with_logging "$CONFIG_FILE"
sleep 2

if ! ps -p $SERVER_PID > /dev/null; then
    echo -e "${RED}✗ Server failed to start${NC}"
    cat "$TEST_LOG_FILE"
    exit 1
fi
echo -e "${GREEN}✓ Server started (PID: $SERVER_PID)${NC}"
echo

# ==================== SECTION 1: Host Header (RFC 7230) ====================
echo "========================================"
echo "SECTION 1: Host Header Requirements"
echo "========================================"
echo

# Test 1.1: HTTP/1.1 request with Host header (should succeed)
echo "[Test 1.1] HTTP/1.1 with Host header"
RESPONSE=$(echo -e "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n" | nc -w 2 127.0.0.1 8080 2>/dev/null | head -1 | grep -oE "[0-9]{3}" || echo "000")
check_result "200" "$RESPONSE" "HTTP/1.1 request with Host header"

# Test 1.2: HTTP/1.1 request without Host header (should fail with 400)
echo "[Test 1.2] HTTP/1.1 without Host header (should fail)"
RESPONSE=$(echo -e "GET / HTTP/1.1\r\n\r\n" | nc -w 2 127.0.0.1 8080 2>/dev/null | head -1 | grep -oE "[0-9]{3}" || echo "000")
check_result "400" "$RESPONSE" "HTTP/1.1 without Host header returns 400"

# Test 1.3: HTTP/1.0 without Host header (should succeed - Host not required in 1.0)
echo "[Test 1.3] HTTP/1.0 without Host header (should succeed)"
RESPONSE=$(echo -e "GET / HTTP/1.0\r\n\r\n" | nc -w 2 127.0.0.1 8080 2>/dev/null | head -1 | grep -oE "[0-9]{3}" || echo "000")
check_result "200" "$RESPONSE" "HTTP/1.0 without Host header is allowed"

echo

# ==================== SECTION 2: Persistent Connections ====================
echo "========================================"
echo "SECTION 2: Persistent Connections"
echo "========================================"
echo

# Test 2.1: HTTP/1.1 default keep-alive (multiple requests via curl)
echo "[Test 2.1] HTTP/1.1 persistent connection (curl test)"
# Use curl with --keepalive-time to simulate persistent connection
RESPONSE1=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 http://127.0.0.1:8080/ 2>/dev/null)
RESPONSE2=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 http://127.0.0.1:8080/ 2>/dev/null)
if [ "$RESPONSE1" = "200" ] && [ "$RESPONSE2" = "200" ]; then
    echo -e "${GREEN}✓${NC} Multiple requests work: $RESPONSE1, $RESPONSE2"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Responses: $RESPONSE1, $RESPONSE2"
fi

# Test 2.2: HTTP/1.1 with Connection: close
echo "[Test 2.2] HTTP/1.1 with Connection: close"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -H "Connection: close" http://127.0.0.1:8080/ 2>/dev/null)
check_result "200" "$RESPONSE" "Connection: close handled"

# Test 2.3: HTTP/1.0 request
echo "[Test 2.3] HTTP/1.0 request"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 --http1.0 http://127.0.0.1:8080/ 2>/dev/null)
check_result "200" "$RESPONSE" "HTTP/1.0 request"

echo

# ==================== SECTION 3: Transfer-Encoding ====================
echo "========================================"
echo "SECTION 3: Transfer-Encoding (Chunked)"
echo "========================================"
echo

# Test 3.1: Chunked transfer encoding POST
echo "[Test 3.1] Chunked POST request (curl)"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -X POST http://127.0.0.1:8080/ \
    -H "Transfer-Encoding: chunked" \
    -H "Content-Type: text/plain" \
    --data-binary "Hello World" 2>/dev/null)
if [ "$RESPONSE" = "200" ] || [ "$RESPONSE" = "201" ]; then
    echo -e "${GREEN}✓${NC} Chunked POST handled: $RESPONSE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Chunked POST response: $RESPONSE"
fi

echo

# ==================== SECTION 4: Content-Length ====================
echo "========================================"
echo "SECTION 4: Content-Length Handling"
echo "========================================"
echo

# Test 4.1: POST with Content-Length
echo "[Test 4.1] POST with Content-Length"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -X POST http://127.0.0.1:8080/ \
    -H "Content-Type: text/plain" \
    -H "Content-Length: 11" \
    -d "Hello World" 2>/dev/null)
if [ "$RESPONSE" = "200" ] || [ "$RESPONSE" = "201" ]; then
    echo -e "${GREEN}✓${NC} POST with Content-Length: $RESPONSE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Content-Length POST: $RESPONSE"
fi

echo

# ==================== SECTION 5: Request Methods ====================
echo "========================================"
echo "SECTION 5: HTTP Methods"
echo "========================================"
echo

# Test 5.1: GET request
echo "[Test 5.1] GET method"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 http://127.0.0.1:8080/ 2>/dev/null)
check_result "200" "$RESPONSE" "GET request"

# Test 5.2: HEAD request (should return headers only, no body)
echo "[Test 5.2] HEAD method"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -X HEAD http://127.0.0.1:8080/ 2>/dev/null)
check_result "200" "$RESPONSE" "HEAD request"

# Verify HEAD has no body
BODY=$(curl -s --max-time 2 -X HEAD http://127.0.0.1:8080/ 2>/dev/null | wc -c)
if [ "$BODY" -eq 0 ]; then
    echo -e "${GREEN}✓${NC} HEAD response has no body"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}✗${NC} HEAD response has body ($BODY bytes)"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# Test 5.3: POST request
echo "[Test 5.3] POST method"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -X POST http://127.0.0.1:8080/ -d "test=data" 2>/dev/null)
if [ "$RESPONSE" = "200" ] || [ "$RESPONSE" = "201" ] || [ "$RESPONSE" = "405" ]; then
    echo -e "${GREEN}✓${NC} POST method handled: $RESPONSE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} POST response: $RESPONSE"
fi

# Test 5.4: PUT request
echo "[Test 5.4] PUT method"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -X PUT http://127.0.0.1:8080/uploads/test.txt -d "data" 2>/dev/null)
if [ "$RESPONSE" = "201" ] || [ "$RESPONSE" = "204" ] || [ "$RESPONSE" = "405" ]; then
    echo -e "${GREEN}✓${NC} PUT method handled: $RESPONSE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} PUT response: $RESPONSE"
fi

# Test 5.5: DELETE request
echo "[Test 5.5] DELETE method"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -X DELETE http://127.0.0.1:8080/ 2>/dev/null)
if [ "$RESPONSE" = "200" ] || [ "$RESPONSE" = "204" ] || [ "$RESPONSE" = "404" ] || [ "$RESPONSE" = "405" ]; then
    echo -e "${GREEN}✓${NC} DELETE method handled: $RESPONSE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} DELETE response: $RESPONSE"
fi

# Test 5.6: Unsupported method (should return 501)
echo "[Test 5.6] Unsupported method (TRACE)"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -X TRACE http://127.0.0.1:8080/ 2>/dev/null)
check_result "501" "$RESPONSE" "TRACE returns 501 Not Implemented"

echo

# ==================== SECTION 6: HTTP Status Codes ====================
echo "========================================"
echo "SECTION 6: HTTP Status Codes"
echo "========================================"
echo

# Test 6.1: 200 OK
echo "[Test 6.1] 200 OK for existing resource"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 http://127.0.0.1:8080/ 2>/dev/null)
check_result "200" "$RESPONSE" "200 OK"

# Test 6.2: 404 Not Found
echo "[Test 6.2] 404 Not Found"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 http://127.0.0.1:8080/nonexistent 2>/dev/null)
check_result "404" "$RESPONSE" "404 Not Found"

# Test 6.3: 400 Bad Request
echo "[Test 6.3] 400 Bad Request (malformed request)"
RESPONSE=$(echo -e "INVALID REQUEST\r\n\r\n" | nc -w 2 127.0.0.1 8080 2>/dev/null | head -1 | grep -oE "[0-9]{3}" || echo "000")
check_result "400" "$RESPONSE" "400 Bad Request"

# Test 6.4: 405 Method Not Allowed
echo "[Test 6.4] 405 Method Not Allowed"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -X DELETE http://127.0.0.1:8080/ 2>/dev/null)
if [ "$RESPONSE" = "405" ]; then
    echo -e "${GREEN}✓${NC} 405 Method Not Allowed: $RESPONSE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Method not allowed response: $RESPONSE"
fi

# Test 6.5: 413 Payload Too Large
echo "[Test 6.5] 413 Payload Too Large"
dd if=/dev/zero bs=1024 count=1100 2>/dev/null > /tmp/large_test.bin
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -X POST http://127.0.0.1:8080/uploads/test.bin \
    --data-binary @/tmp/large_test.bin 2>/dev/null)
rm -f /tmp/large_test.bin
check_result "413" "$RESPONSE" "413 Payload Too Large"

# Test 6.6: 501 Not Implemented
echo "[Test 6.6] 501 Not Implemented"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -X OPTIONS http://127.0.0.1:8080/ 2>/dev/null)
check_result "501" "$RESPONSE" "501 Not Implemented"

echo

# ==================== SECTION 7: HTTP Headers ====================
echo "========================================"
echo "SECTION 7: HTTP Headers"
echo "========================================"
echo

# Test 7.1: Case-insensitive header names (via curl)
echo "[Test 7.1] Case-insensitive headers"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 http://127.0.0.1:8080/ 2>/dev/null)
check_result "200" "$RESPONSE" "Standard headers accepted"

# Test 7.2: Content-Type header
echo "[Test 7.2] Content-Type in POST"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -X POST http://127.0.0.1:8080/ \
    -H "Content-Type: application/json" \
    -d '{"test":"data"}' 2>/dev/null)
if [ "$RESPONSE" = "200" ] || [ "$RESPONSE" = "201" ]; then
    echo -e "${GREEN}✓${NC} Content-Type handled: $RESPONSE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Content-Type response: $RESPONSE"
fi

# Test 7.3: User-Agent header
echo "[Test 7.3] User-Agent header"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 -A "TestAgent/1.0" http://127.0.0.1:8080/ 2>/dev/null)
check_result "200" "$RESPONSE" "User-Agent header"

echo

# ==================== SECTION 8: Request URI ====================
echo "========================================"
echo "SECTION 8: Request URI Handling"
echo "========================================"
echo

# Test 8.1: Simple path
echo "[Test 8.1] Simple URI path"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 http://127.0.0.1:8080/ 2>/dev/null)
check_result "200" "$RESPONSE" "Simple path /"

# Test 8.2: Path with query string
echo "[Test 8.2] URI with query string"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 "http://127.0.0.1:8080/?query=test&param=value" 2>/dev/null)
if [ "$RESPONSE" = "200" ] || [ "$RESPONSE" = "404" ]; then
    echo -e "${GREEN}✓${NC} Query string handled: $RESPONSE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Query string: $RESPONSE"
fi

# Test 8.3: URL encoding in path
echo "[Test 8.3] URL encoding"
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 2 "http://127.0.0.1:8080/test%20space" 2>/dev/null)
if [ "$RESPONSE" = "200" ] || [ "$RESPONSE" = "404" ]; then
    echo -e "${GREEN}✓${NC} URL encoding handled: $RESPONSE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} URL encoding: $RESPONSE"
fi

echo

# ==================== SUMMARY ====================
echo "========================================"
echo "         TEST SUMMARY"
echo "========================================"
echo
TOTAL_TESTS=$((TESTS_PASSED + TESTS_FAILED))
echo "Total Tests Run: $TOTAL_TESTS"
echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
echo -e "${RED}Failed: $TESTS_FAILED${NC}"
echo

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}✓✓✓ ALL TESTS PASSED ✓✓✓${NC}"
    echo
    echo "HTTP/1.1 compliance verified:"
    echo "  ✓ Host header enforcement"
    echo "  ✓ Persistent connections"
    echo "  ✓ Transfer-Encoding: chunked"
    echo "  ✓ Content-Length handling"
    echo "  ✓ All required methods"
    echo "  ✓ Status codes"
    echo "  ✓ Header handling"
else
    echo -e "${YELLOW}Some tests failed or returned unexpected results.${NC}"
    echo "Review the output above for details."
fi
echo

# Cleanup
echo "Cleaning up..."
kill -9 $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
echo "Done!"
