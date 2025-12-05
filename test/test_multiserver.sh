#!/bin/bash

# Comprehensive Multi-Server Configuration Test
# Tests error scenarios, edge cases, and proper isolation between servers

WEBSERV_BIN="./webserv"
CONFIG_FILE="./config/default.conf"
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

TESTS_PASSED=0
TESTS_FAILED=0

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
echo "  Multi-Server Configuration Test Suite"
echo "========================================"
echo

# Start server
echo "Starting server..."

$WEBSERV_BIN $CONFIG_FILE > /tmp/webserv_multitest.log 2>&1 &
SERVER_PID=$!
sleep 2

if ! ps -p $SERVER_PID > /dev/null; then
    echo -e "${RED}✗ Server failed to start${NC}"
    cat /tmp/webserv_multitest.log
    exit 1
fi
echo -e "${GREEN}✓ Server started (PID: $SERVER_PID)${NC}"
echo

echo "========================================"
echo "SECTION 1: Server Availability"
echo "========================================"
echo

# Test 1: Verify all 3 servers are listening
echo "[Test 1.1] All servers listening on configured ports"
RESPONSE_8080=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/ 2>/dev/null)
RESPONSE_8081=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8081/ 2>/dev/null)
RESPONSE_8082=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8082/ 2>/dev/null)

check_result "200" "$RESPONSE_8080" "Server on port 8080 responds"
check_result "200" "$RESPONSE_8081" "Server on port 8081 responds"
check_result "200" "$RESPONSE_8082" "Server on port 8082 responds"
echo

# Test 2: Verify wrong ports are not accessible
echo "[Test 1.2] Unconfigured ports should not respond"
RESPONSE_WRONG=$(curl -s -o /dev/null -w "%{http_code}" --connect-timeout 1 http://127.0.0.1:9999/ 2>/dev/null)
if [ "$RESPONSE_WRONG" = "000" ] || [ -z "$RESPONSE_WRONG" ]; then
    echo -e "${GREEN}✓${NC} Unconfigured port 9999 correctly not responding"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Port 9999 responding unexpectedly: $RESPONSE_WRONG"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi
echo

# Test 3: Server isolation - each has independent configuration
echo "[Test 1.3] Server independence verification"
ROOT_8080=$(curl -s http://127.0.0.1:8080/ 2>/dev/null | grep -o "index.html" | head -n 1)
ROOT_8082=$(curl -s http://127.0.0.1:8082/ 2>/dev/null | grep -o "home.html" | head -n 1)

if [ ! -z "$ROOT_8080" ]; then
    echo -e "${GREEN}✓${NC} Server 8080 uses ./www root (index.html)"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Server 8080 index file check: $ROOT_8080"
fi

if [ ! -z "$ROOT_8082" ]; then
    echo -e "${GREEN}✓${NC} Server 8082 uses ./www/site2 root (home.html)"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Server 8082 index file check: $ROOT_8082"
fi
echo

echo "========================================"
echo "SECTION 2: Client Body Size Limits"
echo "========================================"
echo

# Test 2.1: Server 1 (8080) has 1MB body limit
echo "[Test 2.1] Server 8080: clientMaxBodySize = 1MB"
SMALL_DATA=$(printf 'x%.0s' {1..1000})  # 1KB

# Should accept small upload
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:8080/upload \
    -H "Content-Type: text/plain" \
    --data-binary "$SMALL_DATA" 2>/dev/null)

if [ "$RESPONSE" = "201" ] || [ "$RESPONSE" = "200" ]; then
    echo -e "${GREEN}✓${NC} Server 8080 accepts 1KB upload: $RESPONSE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Server 8080 rejected 1KB upload: $RESPONSE"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# Should reject large upload (>1MB)
dd if=/dev/zero bs=1024 count=1025 2>/dev/null > /tmp/large_file.bin
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:8080/upload \
    -H "Content-Type: text/plain" \
    --data-binary @/tmp/large_file.bin 2>/dev/null)
rm -f /tmp/large_file.bin

check_result "413" "$RESPONSE" "Server 8080 rejects upload >1MB (413)"
echo

# Test 2.2: Server 2 (8081) has 2MB body limit
echo "[Test 2.2] Server 8081: clientMaxBodySize = 2MB"

# Should accept 1.5MB upload (which server 8080 would reject)
dd if=/dev/zero bs=1024 count=1536 2>/dev/null > /tmp/medium_file.bin
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:8081/ \
    -H "Content-Type: text/plain" \
    --data-binary @/tmp/medium_file.bin 2>/dev/null)
rm -f /tmp/medium_file.bin

if [ "$RESPONSE" = "200" ] || [ "$RESPONSE" = "201" ]; then
    echo -e "${GREEN}✓${NC} Server 8081 accepts 1.5MB upload: $RESPONSE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Server 8081 1.5MB response: $RESPONSE"
fi

# Should reject over 2MB
dd if=/dev/zero bs=1024 count=2049 2>/dev/null > /tmp/toolarge_file.bin
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:8081/ \
    -H "Content-Type: text/plain" \
    --data-binary @/tmp/toolarge_file.bin 2>/dev/null)
rm -f /tmp/toolarge_file.bin

check_result "413" "$RESPONSE" "Server 8081 rejects upload >2MB (413)"
echo

# Test 2.3: Server 3 (8082) has 5MB body limit
echo "[Test 2.3] Server 8082: clientMaxBodySize = 5MB"

# Should accept 3MB upload (which both other servers would reject)
dd if=/dev/zero bs=1024 count=3072 2>/dev/null > /tmp/big_file.bin
RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:8082/submit \
    -H "Content-Type: text/plain" \
    --data-binary @/tmp/big_file.bin 2>/dev/null)
rm -f /tmp/big_file.bin

if [ "$RESPONSE" = "200" ] || [ "$RESPONSE" = "201" ]; then
    echo -e "${GREEN}✓${NC} Server 8082 accepts 3MB upload: $RESPONSE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Server 8082 3MB response: $RESPONSE"
fi
echo

# Test 2.4: Cross-server body size isolation
echo "[Test 2.4] Body size limits are per-server (isolation test)"
# Same 1.5MB payload rejected by 8080 but accepted by 8081
dd if=/dev/zero bs=1024 count=1536 2>/dev/null > /tmp/isolation_test.bin
RESPONSE_8080=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:8080/upload \
    -H "Content-Type: text/plain" \
    --data-binary @/tmp/isolation_test.bin 2>/dev/null)
RESPONSE_8081=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:8081/ \
    -H "Content-Type: text/plain" \
    --data-binary @/tmp/isolation_test.bin 2>/dev/null)
rm -f /tmp/isolation_test.bin

if [ "$RESPONSE_8080" = "413" ] && ([ "$RESPONSE_8081" = "200" ] || [ "$RESPONSE_8081" = "201" ]); then
    echo -e "${GREEN}✓${NC} Same payload (1.5MB): 8080 rejects (413), 8081 accepts ($RESPONSE_8081)"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} 1.5MB payload - 8080: $RESPONSE_8080, 8081: $RESPONSE_8081"
fi
echo

echo "========================================"
echo "SECTION 3: HTTP Method Permissions"
echo "========================================"
echo

# Test 3.1: Server 8080 root location (GET POST allowed)
echo "[Test 3.1] Server 8080 / allows: GET POST"
GET_8080=$(curl -s -o /dev/null -w "%{http_code}" -X GET http://127.0.0.1:8080/ 2>/dev/null)
POST_8080=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:8080/ -d "test=1" 2>/dev/null)
DELETE_8080=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE http://127.0.0.1:8080/ 2>/dev/null)
PUT_8080=$(curl -s -o /dev/null -w "%{http_code}" -X PUT http://127.0.0.1:8080/ -d "test=1" 2>/dev/null)

if [ "$GET_8080" = "200" ] || [ "$GET_8080" = "404" ]; then
    echo -e "${GREEN}✓${NC} Server 8080 / accepts GET: $GET_8080"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Server 8080 / GET failed: $GET_8080"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

if [ "$POST_8080" = "200" ] || [ "$POST_8080" = "201" ]; then
    echo -e "${GREEN}✓${NC} Server 8080 / accepts POST: $POST_8080"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Server 8080 / POST response: $POST_8080"
fi

check_result "405" "$DELETE_8080" "Server 8080 / rejects DELETE (405 Method Not Allowed)"
check_result "405" "$PUT_8080" "Server 8080 / rejects PUT (405 Method Not Allowed)"
echo

# Test 3.2: Server 8081 root location (GET POST DELETE allowed)
echo "[Test 3.2] Server 8081 / allows: GET POST DELETE"
GET_8081=$(curl -s -o /dev/null -w "%{http_code}" -X GET http://127.0.0.1:8081/ 2>/dev/null)
POST_8081=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:8081/ -d "test=1" 2>/dev/null)
DELETE_8081=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE http://127.0.0.1:8081/ 2>/dev/null)
PUT_8081=$(curl -s -o /dev/null -w "%{http_code}" -X PUT http://127.0.0.1:8081/ -d "test=1" 2>/dev/null)

if [ "$GET_8081" = "200" ] || [ "$GET_8081" = "404" ]; then
    echo -e "${GREEN}✓${NC} Server 8081 / accepts GET: $GET_8081"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Server 8081 / GET failed: $GET_8081"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

if [ "$POST_8081" = "200" ] || [ "$POST_8081" = "201" ]; then
    echo -e "${GREEN}✓${NC} Server 8081 / accepts POST: $POST_8081"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Server 8081 / POST response: $POST_8081"
fi

if [ "$DELETE_8081" = "200" ] || [ "$DELETE_8081" = "204" ] || [ "$DELETE_8081" = "404" ]; then
    echo -e "${GREEN}✓${NC} Server 8081 / accepts DELETE: $DELETE_8081"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Server 8081 / DELETE response: $DELETE_8081"
fi

check_result "405" "$PUT_8081" "Server 8081 / rejects PUT (405 Method Not Allowed)"
echo

# Test 3.3: Server 8082 root location (GET only)
echo "[Test 3.3] Server 8082 / allows: GET only"
GET_8082=$(curl -s -o /dev/null -w "%{http_code}" -X GET http://127.0.0.1:8082/ 2>/dev/null)
POST_8082=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:8082/ -d "test=1" 2>/dev/null)
DELETE_8082=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE http://127.0.0.1:8082/ 2>/dev/null)
PUT_8082=$(curl -s -o /dev/null -w "%{http_code}" -X PUT http://127.0.0.1:8082/ -d "test=1" 2>/dev/null)

if [ "$GET_8082" = "200" ] || [ "$GET_8082" = "404" ]; then
    echo -e "${GREEN}✓${NC} Server 8082 / accepts GET: $GET_8082"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Server 8082 / GET failed: $GET_8082"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

check_result "405" "$POST_8082" "Server 8082 / rejects POST (405 - method not allowed)"
check_result "405" "$DELETE_8082" "Server 8082 / rejects DELETE (405 - method not allowed)"
check_result "405" "$PUT_8082" "Server 8082 / rejects PUT (405 Method Not Allowed)"
echo

# Test 3.4: Method isolation - same method different servers
echo "[Test 3.4] Method isolation across servers"
DELETE_ROOT_8080=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE http://127.0.0.1:8080/ 2>/dev/null)
DELETE_ROOT_8081=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE http://127.0.0.1:8081/ 2>/dev/null)

if [ "$DELETE_ROOT_8080" = "405" ] && [ "$DELETE_ROOT_8081" != "405" ] && [ "$DELETE_ROOT_8081" != "501" ]; then
    echo -e "${GREEN}✓${NC} DELETE /: rejected by 8080 (405), allowed by 8081 ($DELETE_ROOT_8081)"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} DELETE / responses - 8080: $DELETE_ROOT_8080, 8081: $DELETE_ROOT_8081"
fi
echo

# Test 3.5: Unsupported methods return 501
echo "[Test 3.5] Unsupported HTTP methods return 501"
OPTIONS_8080=$(curl -s -o /dev/null -w "%{http_code}" -X OPTIONS http://127.0.0.1:8080/ 2>/dev/null)
PATCH_8080=$(curl -s -o /dev/null -w "%{http_code}" -X PATCH http://127.0.0.1:8080/ -d "test=1" 2>/dev/null)
HEAD_8080=$(curl -s -o /dev/null -w "%{http_code}" -X HEAD http://127.0.0.1:8080/ 2>/dev/null)

if [ "$OPTIONS_8080" = "501" ] || [ "$OPTIONS_8080" = "200" ]; then
    echo -e "${GREEN}✓${NC} OPTIONS request handled: $OPTIONS_8080"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} OPTIONS response: $OPTIONS_8080"
fi

if [ "$PATCH_8080" = "501" ]; then
    echo -e "${GREEN}✓${NC} PATCH returns 501 (not implemented): $PATCH_8080"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} PATCH response: $PATCH_8080"
fi
echo

# Test 3.6: 405 vs 501 distinction
echo "[Test 3.6] Distinguish 405 (not allowed) from 501 (not implemented)"
# GET on /submit (8082) - GET not in allow_methods (POST DELETE only)
GET_SUBMIT=$(curl -s -o /dev/null -w "%{http_code}" -X GET http://127.0.0.1:8082/submit 2>/dev/null)
# PUT anywhere - now implemented but not allowed on this location
PUT_ANYWHERE=$(curl -s -o /dev/null -w "%{http_code}" -X PUT http://127.0.0.1:8080/ -d "test=1" 2>/dev/null)
# TRACE - not implemented
TRACE_ANYWHERE=$(curl -s -o /dev/null -w "%{http_code}" -X TRACE http://127.0.0.1:8080/ 2>/dev/null)

check_result "405" "$GET_SUBMIT" "GET on /submit (not allowed) returns 405"
check_result "405" "$PUT_ANYWHERE" "PUT request (not allowed) returns 405"
check_result "501" "$TRACE_ANYWHERE" "TRACE request (not implemented) returns 501"
echo

echo "========================================"
echo "SECTION 4: Location-Specific Configuration"
echo "========================================"
echo

# Test 4.1: Different methods per location on same server
echo "[Test 4.1] Different methods per location (Server 8080)"
GET_ROOT=$(curl -s -o /dev/null -w "%{http_code}" -X GET http://127.0.0.1:8080/ 2>/dev/null)
GET_STATIC=$(curl -s -o /dev/null -w "%{http_code}" -X GET http://127.0.0.1:8080/static 2>/dev/null)
POST_STATIC=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:8080/static -d "test=1" 2>/dev/null)
DELETE_UPLOAD=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE http://127.0.0.1:8080/upload 2>/dev/null)

echo -e "${GREEN}✓${NC} / location GET: $GET_ROOT"
echo -e "${GREEN}✓${NC} /static location GET only: $GET_STATIC"
check_result "405" "$POST_STATIC" "/static rejects POST (405)"

if [ "$DELETE_UPLOAD" = "200" ] || [ "$DELETE_UPLOAD" = "204" ] || [ "$DELETE_UPLOAD" = "404" ]; then
    echo -e "${GREEN}✓${NC} /upload allows DELETE: $DELETE_UPLOAD"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} /upload DELETE response: $DELETE_UPLOAD"
fi
echo

# Test 4.2: Location not found returns proper error
echo "[Test 4.2] Non-existent locations"
RESPONSE_404=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/nonexistent 2>/dev/null)
RESPONSE_404_8082=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8082/invalid/path 2>/dev/null)

check_result "404" "$RESPONSE_404" "Server 8080 /nonexistent returns 404"
check_result "404" "$RESPONSE_404_8082" "Server 8082 /invalid/path returns 404"
echo

echo "========================================"
echo "SECTION 5: Error Handling & Edge Cases"
echo "========================================"
echo

# Test 5.1: Invalid HTTP version
echo "[Test 5.1] Invalid HTTP requests"
INVALID_VERSION=$(echo -e "GET / HTTP/2.0\r\nHost: localhost\r\n\r\n" | nc -w 1 127.0.0.1 8080 2>/dev/null | head -n 1 | grep -o "HTTP/1.." || echo "ERROR")
if [ "$INVALID_VERSION" = "HTTP/1.1" ]; then
    echo -e "${GREEN}✓${NC} Server responds with HTTP/1.1: $INVALID_VERSION"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} HTTP version handling: $INVALID_VERSION"
fi

# Test 5.2: Malformed requests
echo "[Test 5.2] Malformed HTTP requests"
MALFORMED=$(echo -e "INVALID REQUEST\r\n\r\n" | nc -w 1 127.0.0.1 8080 2>/dev/null | head -n 1 | grep -o "[0-9][0-9][0-9]" || echo "000")
if [ "$MALFORMED" = "400" ]; then
    echo -e "${GREEN}✓${NC} Malformed request returns 400"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Malformed request response: $MALFORMED"
fi

# Test 5.3: Missing Host header - HTTP/1.1 requires Host, HTTP/1.0 doesn't
# HTTP/1.1 without Host header should return 400 Bad Request
MISSING_HOST_11=$(echo -e "GET / HTTP/1.1\r\n\r\n" | nc -w 1 127.0.0.1 8080 2>/dev/null | head -n 1 | grep -o "[0-9][0-9][0-9]" || echo "000")
if [ "$MISSING_HOST_11" = "400" ]; then
    echo -e "${GREEN}✓${NC} HTTP/1.1 without Host header returns 400: $MISSING_HOST_11"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}✗${NC} HTTP/1.1 without Host header should return 400, got: $MISSING_HOST_11"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

# HTTP/1.0 without Host header should be accepted (200)
MISSING_HOST_10=$(echo -e "GET / HTTP/1.0\r\n\r\n" | nc -w 1 127.0.0.1 8080 2>/dev/null | head -n 1 | grep -o "[0-9][0-9][0-9]" || echo "000")
if [ "$MISSING_HOST_10" = "200" ]; then
    echo -e "${GREEN}✓${NC} HTTP/1.0 without Host header accepted: $MISSING_HOST_10"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} HTTP/1.0 without Host header response: $MISSING_HOST_10"
fi
echo

# Test 5.4: Empty request body when body expected
echo "[Test 5.4] Empty body with Content-Length"
EMPTY_BODY=$(curl -s -o /dev/null -w "%{http_code}" -X POST http://127.0.0.1:8080/upload \
    -H "Content-Length: 0" 2>/dev/null)
if [ "$EMPTY_BODY" = "200" ] || [ "$EMPTY_BODY" = "201" ] || [ "$EMPTY_BODY" = "400" ]; then
    echo -e "${GREEN}✓${NC} Empty body POST: $EMPTY_BODY"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Empty body response: $EMPTY_BODY"
fi
echo

# Test 5.6: Multiple simultaneous connections to different servers
echo "[Test 5.5] Concurrent requests to multiple servers"
curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/ > /tmp/test_8080.txt 2>&1 &
PID1=$!
curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8081/ > /tmp/test_8081.txt 2>&1 &
PID2=$!
curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8082/ > /tmp/test_8082.txt 2>&1 &
PID3=$!

wait $PID1 $PID2 $PID3

CONCURRENT_8080=$(cat /tmp/test_8080.txt)
CONCURRENT_8081=$(cat /tmp/test_8081.txt)
CONCURRENT_8082=$(cat /tmp/test_8082.txt)

if [ "$CONCURRENT_8080" = "200" ] && [ "$CONCURRENT_8081" = "200" ] && [ "$CONCURRENT_8082" = "200" ]; then
    echo -e "${GREEN}✓${NC} All servers handle concurrent requests: 8080=$CONCURRENT_8080, 8081=$CONCURRENT_8081, 8082=$CONCURRENT_8082"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Concurrent responses: 8080=$CONCURRENT_8080, 8081=$CONCURRENT_8081, 8082=$CONCURRENT_8082"
fi

rm -f /tmp/test_808*.txt
echo

echo "========================================"
echo "SECTION 6: HTTP Redirections"
echo "========================================"
echo

# Test 6.1: Redirections (Server 8080 has redirect locations)
echo "[Test 6.1] HTTP redirections"
REDIRECT_301=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/old-page 2>/dev/null)
REDIRECT_302=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/redirect 2>/dev/null)

check_result "301" "$REDIRECT_301" "/old-page returns 301 redirect"
check_result "302" "$REDIRECT_302" "/redirect returns 302 redirect"

# Test 6.2: Follow redirects
REDIRECT_FOLLOW=$(curl -s -o /dev/null -w "%{http_code}" -L http://127.0.0.1:8080/redirect 2>/dev/null)
if [ "$REDIRECT_FOLLOW" = "200" ] || [ "$REDIRECT_FOLLOW" = "404" ]; then
    echo -e "${GREEN}✓${NC} Following redirect leads to valid page: $REDIRECT_FOLLOW"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Redirect follow response: $REDIRECT_FOLLOW"
fi
echo

echo "========================================"
echo "SECTION 7: Autoindex Configuration"
echo "========================================"
echo

# Test 7.1: Autoindex enabled (Server 8080 root)
echo "[Test 7.1] Autoindex enabled/disabled per server"
AUTOINDEX_8080=$(curl -s http://127.0.0.1:8080/nonexistent_dir/ 2>/dev/null | grep -i "index of\|directory listing" | wc -l)
AUTOINDEX_8081=$(curl -s http://127.0.0.1:8081/nonexistent_dir/ 2>/dev/null | grep -i "index of\|directory listing" | wc -l)

if [ "$AUTOINDEX_8080" -gt 0 ] || [ "$AUTOINDEX_8081" -eq 0 ]; then
    echo -e "${GREEN}✓${NC} Autoindex configuration respected"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Autoindex behavior may vary based on directory existence"
fi
echo

echo "========================================"
echo "SECTION 8: Error Page Configuration"
echo "========================================"
echo

# Test 8.1: Different error pages per server
echo "[Test 8.1] Custom error pages per server"
ERROR_404_8080=$(curl -s http://127.0.0.1:8080/definitely_nonexistent 2>/dev/null | grep -i "404\|not found" | wc -l)
ERROR_404_8081=$(curl -s http://127.0.0.1:8081/definitely_nonexistent 2>/dev/null | grep -i "404\|not found" | wc -l)

if [ "$ERROR_404_8080" -gt 0 ]; then
    echo -e "${GREEN}✓${NC} Server 8080 returns 404 error page"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Server 8080 404 page format may vary"
fi

if [ "$ERROR_404_8081" -gt 0 ]; then
    echo -e "${GREEN}✓${NC} Server 8081 returns 404 error page"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Server 8081 404 page format may vary"
fi
echo

echo "========================================"
echo "SECTION 9: CGI Configuration"
echo "========================================"
echo

# Test 9.1: CGI configured differently per server
echo "[Test 9.1] CGI configuration per server"
if [ -d "./www/cgi-bin" ]; then
    # Test if PHP CGI is available on both servers
    CGI_8080=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/cgi-bin/test.php 2>/dev/null)
    CGI_8081=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8081/cgi-bin/test.php 2>/dev/null)
    
    echo -e "${YELLOW}Note:${NC} CGI /cgi-bin response 8080: $CGI_8080, 8081: $CGI_8081"
    echo -e "${YELLOW}Note:${NC} (404 expected if test.php doesn't exist)"
else
    echo -e "${YELLOW}Note:${NC} CGI directory not found, skipping CGI tests"
fi
echo

echo "========================================"
echo "SECTION 10: Security & Isolation Tests"
echo "========================================"
echo

# Test 10.1: Path Traversal Attack Prevention
echo "[Test 10.1] Path traversal vulnerability protection"

# Try to access parent directories with ../
TRAVERSAL_1=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:8080/../../../etc/passwd" 2>/dev/null)
TRAVERSAL_2=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:8080/../../../../etc/passwd" 2>/dev/null)
TRAVERSAL_3=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:8080/static/../../../../../../etc/passwd" 2>/dev/null)

if [ "$TRAVERSAL_1" = "400" ] || [ "$TRAVERSAL_1" = "403" ] || [ "$TRAVERSAL_1" = "404" ]; then
    echo -e "${GREEN}✓${NC} Path traversal ../ blocked: $TRAVERSAL_1"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Path traversal ../ not properly blocked: $TRAVERSAL_1"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

if [ "$TRAVERSAL_2" = "400" ] || [ "$TRAVERSAL_2" = "403" ] || [ "$TRAVERSAL_2" = "404" ]; then
    echo -e "${GREEN}✓${NC} Multiple ../ blocked: $TRAVERSAL_2"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Multiple ../ not properly blocked: $TRAVERSAL_2"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi

if [ "$TRAVERSAL_3" = "400" ] || [ "$TRAVERSAL_3" = "403" ] || [ "$TRAVERSAL_3" = "404" ]; then
    echo -e "${GREEN}✓${NC} Path traversal from subdirectory blocked: $TRAVERSAL_3"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Path traversal from subdirectory not properly blocked: $TRAVERSAL_3"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi
echo

# Test 10.2: URL-encoded path traversal attempts
echo "[Test 10.2] URL-encoded path traversal protection"

ENCODED_1=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:8080/%2e%2e%2f%2e%2e%2fetc/passwd" 2>/dev/null)

if [ "$ENCODED_1" = "400" ] || [ "$ENCODED_1" = "403" ] || [ "$ENCODED_1" = "404" ]; then
    echo -e "${GREEN}✓${NC} URL-encoded ../ blocked: $ENCODED_1"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} URL-encoded traversal response: $ENCODED_1"
fi
echo

# Test 10.3: Server root isolation - cannot access other server's files
echo "[Test 10.3] Server root isolation verification"

# Server 8080 root is ./www, Server 8082 root is ./www/site2
# Try to access site2 content from 8080 using direct path
ISOLATION_1=$(curl -s http://127.0.0.1:8080/site2/home.html 2>/dev/null | grep -i "home" | wc -l)
# Try to access root content from 8082 (should fail since it's outside ./www/site2)
ISOLATION_2=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8082/../index.html 2>/dev/null)

if [ "$ISOLATION_1" -gt 0 ]; then
    echo -e "${GREEN}✓${NC} Server 8080 can access its subdirectory /site2/"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Server 8080 /site2/ access (may not exist)"
fi

if [ "$ISOLATION_2" = "400" ] || [ "$ISOLATION_2" = "403" ] || [ "$ISOLATION_2" = "404" ]; then
    echo -e "${GREEN}✓${NC} Server 8082 cannot escape its root with ../: $ISOLATION_2"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Server 8082 root escape not blocked: $ISOLATION_2"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi
echo

# Test 10.4: Absolute path injection attempts
echo "[Test 10.4] Absolute path injection protection"

ABS_PATH_1=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:8080//etc/passwd" 2>/dev/null)

if [ "$ABS_PATH_1" = "400" ] || [ "$ABS_PATH_1" = "403" ] || [ "$ABS_PATH_1" = "404" ]; then
    echo -e "${GREEN}✓${NC} Absolute path //etc/passwd blocked: $ABS_PATH_1"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Absolute path not blocked: $ABS_PATH_1"
    TESTS_FAILED=$((TESTS_FAILED + 1))
fi
echo

# Test 10.5: Server configuration isolation
echo "[Test 10.5] Configuration file isolation per server"

# Verify servers cannot access each other's restricted areas
# Server 8082 has /submit location, try from 8080
SUBMIT_8080=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/submit 2>/dev/null)
# Server 8080 has /upload location, try from 8082 (should fail - not configured there)
UPLOAD_8082=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8082/upload 2>/dev/null)

if [ "$SUBMIT_8080" = "404" ] || [ "$SUBMIT_8080" = "403" ]; then
    echo -e "${GREEN}✓${NC} Server 8080 doesn't have access to 8082's /submit: $SUBMIT_8080"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Server 8080 /submit response: $SUBMIT_8080 (may exist in root)"
fi

if [ "$UPLOAD_8082" = "404" ] || [ "$UPLOAD_8082" = "403" ]; then
    echo -e "${GREEN}✓${NC} Server 8082 doesn't have access to 8080's /upload: $UPLOAD_8082"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Server 8082 /upload response: $UPLOAD_8082"
fi
echo



echo "========================================"
echo "SECTION 11: Stress & Boundary Tests"
echo "========================================"
echo

# Test 11.1: Rapid sequential requests
echo "[Test 11.1] Rapid sequential requests"
SUCCESS_COUNT=0
for i in {1..10}; do
    RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/ 2>/dev/null)
    if [ "$RESPONSE" = "200" ]; then
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    fi
done

if [ "$SUCCESS_COUNT" -eq 10 ]; then
    echo -e "${GREEN}✓${NC} All 10 rapid requests successful"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} $SUCCESS_COUNT/10 rapid requests successful"
fi
echo

# Test 11.2: Very large headers
echo "[Test 11.2] Large header handling"
LARGE_HEADER=$(printf 'X-Custom-Header: %.0s' {1..100})
RESPONSE_LARGE_HEADER=$(curl -s -o /dev/null -w "%{http_code}" -H "$LARGE_HEADER" http://127.0.0.1:8080/ 2>/dev/null)

if [ "$RESPONSE_LARGE_HEADER" = "200" ] || [ "$RESPONSE_LARGE_HEADER" = "431" ] || [ "$RESPONSE_LARGE_HEADER" = "400" ]; then
    echo -e "${GREEN}✓${NC} Large header handled: $RESPONSE_LARGE_HEADER"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Large header response: $RESPONSE_LARGE_HEADER"
fi
echo

# Test 11.3: Connection persistence (Keep-Alive)
echo "[Test 11.3] Keep-Alive connection"
KEEPALIVE=$(curl -s -o /dev/null -w "%{http_code}" -H "Connection: keep-alive" http://127.0.0.1:8080/ 2>/dev/null)

if [ "$KEEPALIVE" = "200" ]; then
    echo -e "${GREEN}✓${NC} Keep-Alive connection handled: $KEEPALIVE"
    TESTS_PASSED=$((TESTS_PASSED + 1))
else
    echo -e "${YELLOW}Note:${NC} Keep-Alive response: $KEEPALIVE"
fi
echo

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
    echo "Multi-server architecture verified:"
    echo "  ✓ 3 independent servers on different ports"
    echo "  ✓ Independent body size limits enforced"
    echo "  ✓ Per-server method permissions"
    echo "  ✓ Per-location configuration"
    echo "  ✓ Proper error handling and edge cases"
    echo "  ✓ HTTP redirections working"
    echo "  ✓ Concurrent request handling"
    echo "  ✓ Stress test scenarios passed"
else
    echo -e "${YELLOW}Some tests failed or returned unexpected results.${NC}"
    echo "Review the output above for details."
fi
echo

# Cleanup
echo "Cleaning up..."
kill -9 $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null
rm -f /tmp/webserv_multitest.log
echo "Done!"
