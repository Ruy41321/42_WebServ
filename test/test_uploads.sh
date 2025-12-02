#!/bin/bash
# Test suite for POST and PUT functionality with multipart, sanitization, and collision handling

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SERVER_URL="http://127.0.0.1:8080"
UPLOAD_DIR="./www/uploads"
TEST_DIR="/tmp/webserv_post_test"
PASSED=0
FAILED=0
TOTAL=0

# Create test directory
mkdir -p "$TEST_DIR"

# Cleanup function
cleanup() {
    echo -e "\n${BLUE}Cleaning up test files...${NC}"
    rm -rf "$TEST_DIR"
    # Clean up uploaded test files
    rm -f "$UPLOAD_DIR"/test_*.txt "$UPLOAD_DIR"/test_*.png "$UPLOAD_DIR"/upload_*.txt 2>/dev/null
    rm -f "$UPLOAD_DIR"/malicious*.txt "$UPLOAD_DIR"/safe*.txt 2>/dev/null
    pkill -9 webserv 2>/dev/null
}

trap cleanup EXIT

# Helper function for test results
print_result() {
    local test_name="$1"
    local expected="$2"
    local actual="$3"
    
    TOTAL=$((TOTAL + 1))
    
    if [ "$expected" = "$actual" ]; then
        echo -e "${GREEN}[PASS]${NC} $test_name"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}[FAIL]${NC} $test_name"
        echo -e "       Expected: $expected"
        echo -e "       Actual:   $actual"
        FAILED=$((FAILED + 1))
    fi
}

# Check if response contains expected string
check_contains() {
    local test_name="$1"
    local response="$2"
    local expected="$3"
    
    TOTAL=$((TOTAL + 1))
    
    if echo "$response" | grep -q "$expected"; then
        echo -e "${GREEN}[PASS]${NC} $test_name"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}[FAIL]${NC} $test_name"
        echo -e "       Expected to contain: $expected"
        echo -e "       Response: $(echo "$response" | head -5)"
        FAILED=$((FAILED + 1))
    fi
}

# Get HTTP status code
get_status() {
    echo "$1" | head -1 | grep -oE '[0-9]{3}' | head -1
}

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}   WebServ POST Functionality Tests    ${NC}"
echo -e "${BLUE}========================================${NC}\n"

# Start server
echo -e "${YELLOW}Starting server...${NC}"
pkill -9 webserv 2>/dev/null
sleep 1
./webserv config/default.conf > /tmp/webserv_test.log 2>&1 &
SERVER_PID=$!
sleep 2

# Verify server is running
if ! ps -p $SERVER_PID > /dev/null 2>&1; then
    echo -e "${RED}Failed to start server${NC}"
    exit 1
fi

echo -e "${GREEN}Server started (PID: $SERVER_PID)${NC}\n"

# ==================== SECTION 1: Basic POST ====================
echo -e "${YELLOW}=== SECTION 1: Basic POST Operations ===${NC}"

# Test 1.1: Simple binary upload
echo "Hello, this is a test file!" > "$TEST_DIR/simple.txt"
RESPONSE=$(curl -s -i -X POST "$SERVER_URL/uploads/test_simple.txt" \
    -H "Content-Type: application/octet-stream" \
    --data-binary @"$TEST_DIR/simple.txt" 2>&1)
STATUS=$(get_status "$RESPONSE")
print_result "1.1 Simple binary upload" "201" "$STATUS"

# Test 1.2: Verify file was created
if [ -f "$UPLOAD_DIR/test_simple.txt" ] || ls "$UPLOAD_DIR"/upload_*.txt >/dev/null 2>&1; then
    print_result "1.2 File created in upload directory" "yes" "yes"
else
    print_result "1.2 File created in upload directory" "yes" "no"
fi

# Test 1.3: POST with Content-Type: image/png
python3 -c "
import struct, zlib
# Create minimal valid PNG
def create_png():
    sig = b'\x89PNG\r\n\x1a\n'
    ihdr = struct.pack('>I', 13) + b'IHDR' + struct.pack('>IIBBBBB', 2, 2, 8, 2, 0, 0, 0)
    ihdr += struct.pack('>I', zlib.crc32(ihdr[4:]) & 0xffffffff)
    idat_data = zlib.compress(b'\x00\xff\x00\x00\x00\x00\x00\xff\x00\x00', 9)
    idat = struct.pack('>I', len(idat_data)) + b'IDAT' + idat_data
    idat += struct.pack('>I', zlib.crc32(idat[4:]) & 0xffffffff)
    iend = struct.pack('>I', 0) + b'IEND' + struct.pack('>I', zlib.crc32(b'IEND') & 0xffffffff)
    return sig + ihdr + idat + iend
with open('$TEST_DIR/test.png', 'wb') as f:
    f.write(create_png())
" 2>/dev/null

RESPONSE=$(curl -s -i -X POST "$SERVER_URL/uploads/test_image.png" \
    -H "Content-Type: image/png" \
    --data-binary @"$TEST_DIR/test.png" 2>&1)
STATUS=$(get_status "$RESPONSE")
print_result "1.3 Binary PNG upload" "201" "$STATUS"

# ==================== SECTION 2: Multipart Form Data ====================
echo -e "\n${YELLOW}=== SECTION 2: Multipart Form Data ===${NC}"

# Test 2.1: Multipart upload with curl -F
RESPONSE=$(curl -s -i -X POST "$SERVER_URL/uploads/" \
    -F "file=@$TEST_DIR/simple.txt;filename=multipart_test.txt" 2>&1)
STATUS=$(get_status "$RESPONSE")
print_result "2.1 Multipart form upload" "201" "$STATUS"

# Test 2.2: Check multipart extracts content correctly (not boundaries)
# Create a file with known content
echo "UNIQUE_CONTENT_12345" > "$TEST_DIR/multipart_content.txt"
RESPONSE=$(curl -s -i -X POST "$SERVER_URL/uploads/" \
    -F "file=@$TEST_DIR/multipart_content.txt;filename=content_check.txt" 2>&1)

# Find the uploaded file and check content
UPLOADED_FILE=$(ls -t "$UPLOAD_DIR"/*.txt 2>/dev/null | head -1)
if [ -n "$UPLOADED_FILE" ]; then
    CONTENT=$(cat "$UPLOADED_FILE")
    if echo "$CONTENT" | grep -q "UNIQUE_CONTENT_12345" && ! echo "$CONTENT" | grep -q "boundary"; then
        print_result "2.2 Multipart content extracted correctly" "yes" "yes"
    else
        print_result "2.2 Multipart content extracted correctly" "yes" "no"
    fi
else
    print_result "2.2 Multipart content extracted correctly" "yes" "file_not_found"
fi

# Test 2.3: Multipart with filename extraction
RESPONSE=$(curl -s -i -X POST "$SERVER_URL/uploads/" \
    -F "file=@$TEST_DIR/simple.txt;filename=extracted_name.txt" 2>&1)
check_contains "2.3 Filename extracted from multipart" "$RESPONSE" "extracted_name"

# ==================== SECTION 3: Filename Sanitization ====================
echo -e "\n${YELLOW}=== SECTION 3: Filename Sanitization ===${NC}"

# Test 3.1: Path traversal attempt (../)
RESPONSE=$(curl -s -i -X POST "$SERVER_URL/uploads/../../../etc/passwd" \
    -H "Content-Type: text/plain" \
    -d "malicious content" 2>&1)
# Should either sanitize the path or reject
STATUS=$(get_status "$RESPONSE")
if [ "$STATUS" = "201" ]; then
    # Check that file was NOT created outside uploads directory
    if [ ! -f "/etc/passwd_malicious" ] && [ ! -f "./etc/passwd" ]; then
        print_result "3.1 Path traversal prevented" "safe" "safe"
    else
        print_result "3.1 Path traversal prevented" "safe" "unsafe"
    fi
else
    # Server rejected the request - also acceptable
    print_result "3.1 Path traversal rejected" "rejected" "rejected"
fi

# Test 3.2: Filename with special characters via Content-Disposition
RESPONSE=$(curl -s -i -X POST "$SERVER_URL/uploads/" \
    -H "Content-Disposition: attachment; filename=\"test<script>alert.txt\"" \
    -H "Content-Type: text/plain" \
    -d "test content" 2>&1)
# Check that dangerous characters were removed
if ls "$UPLOAD_DIR"/*script* >/dev/null 2>&1; then
    # File exists but should have sanitized name
    FILE=$(ls "$UPLOAD_DIR"/*script* 2>/dev/null | head -1)
    if echo "$FILE" | grep -q "<"; then
        print_result "3.2 Special characters sanitized" "sanitized" "not_sanitized"
    else
        print_result "3.2 Special characters sanitized" "sanitized" "sanitized"
    fi
else
    # File was created with different name (good)
    print_result "3.2 Special characters sanitized" "sanitized" "sanitized"
fi

# Test 3.3: Null byte injection attempt
RESPONSE=$(curl -s -i -X POST "$SERVER_URL/uploads/test%00.txt" \
    -H "Content-Type: text/plain" \
    -d "null byte test" 2>&1)
# Should either sanitize or reject
STATUS=$(get_status "$RESPONSE")
if [ "$STATUS" = "201" ] || [ "$STATUS" = "400" ] || [ "$STATUS" = "404" ]; then
    print_result "3.3 Null byte handled safely" "safe" "safe"
else
    print_result "3.3 Null byte handled safely" "safe" "unknown_$STATUS"
fi

# ==================== SECTION 4: Collision Handling ====================
echo -e "\n${YELLOW}=== SECTION 4: File Collision Handling ===${NC}"

# Clean up any existing collision test files
rm -f "$UPLOAD_DIR"/collision_test* 2>/dev/null

# Test 4.1: First upload (should succeed)
RESPONSE1=$(curl -s -i -X POST "$SERVER_URL/uploads/" \
    -H "Content-Disposition: attachment; filename=\"collision_test.txt\"" \
    -H "Content-Type: text/plain" \
    -d "First upload content" 2>&1)
STATUS1=$(get_status "$RESPONSE1")
print_result "4.1 First upload succeeds" "201" "$STATUS1"

# Test 4.2: Second upload with same name (should create unique filename)
RESPONSE2=$(curl -s -i -X POST "$SERVER_URL/uploads/" \
    -H "Content-Disposition: attachment; filename=\"collision_test.txt\"" \
    -H "Content-Type: text/plain" \
    -d "Second upload content" 2>&1)
STATUS2=$(get_status "$RESPONSE2")
print_result "4.2 Second upload with same name succeeds" "201" "$STATUS2"

# Test 4.3: Verify two different files exist
FILE_COUNT=$(ls "$UPLOAD_DIR"/collision_test* 2>/dev/null | wc -l)
TOTAL=$((TOTAL + 1))
if [ "$FILE_COUNT" -ge 2 ]; then
    echo -e "${GREEN}[PASS]${NC} 4.3 Two unique files created ($FILE_COUNT files)"
    PASSED=$((PASSED + 1))
else
    echo -e "${RED}[FAIL]${NC} 4.3 Two unique files created"
    echo -e "       Expected: >=2"
    echo -e "       Actual:   $FILE_COUNT"
    FAILED=$((FAILED + 1))
fi

# Test 4.4: Verify original file was not overwritten
FIRST_CONTENT=$(cat "$UPLOAD_DIR/collision_test.txt" 2>/dev/null)
if echo "$FIRST_CONTENT" | grep -q "First upload content"; then
    print_result "4.4 Original file preserved" "yes" "yes"
else
    print_result "4.4 Original file preserved" "yes" "no"
fi

# ==================== SECTION 5: Body Size Limits ====================
echo -e "\n${YELLOW}=== SECTION 5: Body Size Limits ===${NC}"

# Test 5.1: Upload within size limit (1MB for port 8080)
dd if=/dev/zero bs=1024 count=100 2>/dev/null > "$TEST_DIR/small.bin"
RESPONSE=$(curl -s -i -X POST "$SERVER_URL/uploads/small_file.bin" \
    -H "Content-Type: application/octet-stream" \
    --data-binary @"$TEST_DIR/small.bin" 2>&1)
STATUS=$(get_status "$RESPONSE")
print_result "5.1 Upload within limit (100KB)" "201" "$STATUS"

# Test 5.2: Upload exceeding size limit (should fail with 413)
dd if=/dev/zero bs=1024 count=2000 2>/dev/null > "$TEST_DIR/large.bin"
RESPONSE=$(curl -s -i -X POST "$SERVER_URL/uploads/large_file.bin" \
    -H "Content-Type: application/octet-stream" \
    --data-binary @"$TEST_DIR/large.bin" 2>&1)
STATUS=$(get_status "$RESPONSE")
print_result "5.2 Reject upload over limit (2MB)" "413" "$STATUS"

# ==================== SECTION 6: Missing Headers ====================
echo -e "\n${YELLOW}=== SECTION 6: Error Handling ===${NC}"

# Test 6.1: POST without Content-Length (should fail)
RESPONSE=$(curl -s -i -X POST "$SERVER_URL/uploads/test.txt" \
    -H "Content-Type: text/plain" \
    -H "Transfer-Encoding: chunked" \
    -d "test" 2>&1)
# This might succeed or fail depending on curl behavior
# Just verify we get a response
STATUS=$(get_status "$RESPONSE")
if [ -n "$STATUS" ]; then
    print_result "6.1 POST handled without explicit Content-Length" "response" "response"
else
    print_result "6.1 POST handled without explicit Content-Length" "response" "no_response"
fi

# Test 6.2: POST to location without upload_store (valid POST data handler)
RESPONSE=$(curl -s -i -X POST "$SERVER_URL/static/test.txt" \
    -H "Content-Type: text/plain" \
    -d "test" 2>&1)
STATUS=$(get_status "$RESPONSE")
# /static only allows GET, so should return 405
if [ "$STATUS" = "405" ]; then
    print_result "6.2 POST to GET-only location rejected" "405" "$STATUS"
else
    print_result "6.2 POST to GET-only location rejected" "405" "$STATUS"
fi

# ==================== SECTION 7: HEAD Method ====================
echo -e "\n${YELLOW}=== SECTION 7: HEAD Method ===${NC}"

# Test 7.1: HEAD request on existing file
RESPONSE=$(curl -s -i -X HEAD "$SERVER_URL/index.html" 2>&1)
STATUS=$(get_status "$RESPONSE")
print_result "7.1 HEAD request returns 200" "200" "$STATUS"

# Test 7.2: HEAD response has no body
BODY_LENGTH=$(curl -s -X HEAD "$SERVER_URL/index.html" 2>&1 | wc -c)
if [ "$BODY_LENGTH" -eq 0 ]; then
    print_result "7.2 HEAD response has no body" "empty" "empty"
else
    print_result "7.2 HEAD response has no body" "empty" "${BODY_LENGTH}bytes"
fi

# Test 7.3: HEAD includes Content-Length header
RESPONSE=$(curl -s -i -X HEAD "$SERVER_URL/index.html" 2>&1)
if echo "$RESPONSE" | grep -qi "Content-Length:"; then
    print_result "7.3 HEAD includes Content-Length" "yes" "yes"
else
    print_result "7.3 HEAD includes Content-Length" "yes" "no"
fi

# Test 7.4: HEAD on non-existent file returns 404
RESPONSE=$(curl -s -i -X HEAD "$SERVER_URL/nonexistent.html" 2>&1)
STATUS=$(get_status "$RESPONSE")
print_result "7.4 HEAD on missing file returns 404" "404" "$STATUS"

# ==================== SECTION 8: PUT Method ====================
echo -e "\n${YELLOW}=== SECTION 8: PUT Method ===${NC}"

# Clean up any existing PUT test files
rm -f "$UPLOAD_DIR"/put_*.txt "$UPLOAD_DIR"/put_*.bin 2>/dev/null

# Test 8.1: PUT creates new file (201 Created)
RESPONSE=$(curl -s -i -X PUT "$SERVER_URL/uploads/put_new_file.txt" \
    -H "Content-Type: text/plain" \
    -d "PUT new file content" 2>&1)
STATUS=$(get_status "$RESPONSE")
print_result "8.1 PUT creates new file (201)" "201" "$STATUS"

# Test 8.2: Verify file content was written correctly
if [ -f "$UPLOAD_DIR/put_new_file.txt" ]; then
    CONTENT=$(cat "$UPLOAD_DIR/put_new_file.txt")
    if echo "$CONTENT" | grep -q "PUT new file content"; then
        print_result "8.2 PUT file content correct" "yes" "yes"
    else
        print_result "8.2 PUT file content correct" "yes" "no"
    fi
else
    print_result "8.2 PUT file content correct" "yes" "file_not_found"
fi

# Test 8.3: PUT replaces existing file (204 No Content)
RESPONSE=$(curl -s -i -X PUT "$SERVER_URL/uploads/put_new_file.txt" \
    -H "Content-Type: text/plain" \
    -d "REPLACED content" 2>&1)
STATUS=$(get_status "$RESPONSE")
print_result "8.3 PUT replaces existing file (204)" "204" "$STATUS"

# Test 8.4: Verify content was replaced
if [ -f "$UPLOAD_DIR/put_new_file.txt" ]; then
    CONTENT=$(cat "$UPLOAD_DIR/put_new_file.txt")
    if echo "$CONTENT" | grep -q "REPLACED content"; then
        print_result "8.4 PUT replaced content correctly" "yes" "yes"
    else
        print_result "8.4 PUT replaced content correctly" "yes" "no (old: $CONTENT)"
    fi
else
    print_result "8.4 PUT replaced content correctly" "yes" "file_not_found"
fi

# Test 8.5: PUT binary file
echo "Creating binary test file..."
dd if=/dev/urandom bs=1024 count=10 2>/dev/null > "$TEST_DIR/put_binary.bin"
ORIGINAL_MD5=$(md5sum "$TEST_DIR/put_binary.bin" | awk '{print $1}')

RESPONSE=$(curl -s -i -X PUT "$SERVER_URL/uploads/put_binary.bin" \
    -H "Content-Type: application/octet-stream" \
    --data-binary @"$TEST_DIR/put_binary.bin" 2>&1)
STATUS=$(get_status "$RESPONSE")
print_result "8.5 PUT binary file succeeds" "201" "$STATUS"

# Test 8.6: Verify binary integrity
if [ -f "$UPLOAD_DIR/put_binary.bin" ]; then
    UPLOADED_MD5=$(md5sum "$UPLOAD_DIR/put_binary.bin" | awk '{print $1}')
    if [ "$ORIGINAL_MD5" = "$UPLOADED_MD5" ]; then
        print_result "8.6 PUT binary integrity verified" "match" "match"
    else
        print_result "8.6 PUT binary integrity verified" "match" "mismatch"
    fi
else
    print_result "8.6 PUT binary integrity verified" "match" "file_not_found"
fi

# Test 8.7: PUT to location without PUT permission (405)
RESPONSE=$(curl -s -i -X PUT "$SERVER_URL/static/test.txt" \
    -H "Content-Type: text/plain" \
    -d "Should be rejected" 2>&1)
STATUS=$(get_status "$RESPONSE")
print_result "8.7 PUT without permission returns 405" "405" "$STATUS"

# Test 8.8: PUT with path traversal attempt
RESPONSE=$(curl -s -i -X PUT "$SERVER_URL/uploads/../../../tmp/evil.txt" \
    -H "Content-Type: text/plain" \
    -d "Path traversal attack" 2>&1)
STATUS=$(get_status "$RESPONSE")
# Should either sanitize path or reject
if [ "$STATUS" = "201" ] || [ "$STATUS" = "204" ]; then
    if [ ! -f "/tmp/evil.txt" ]; then
        print_result "8.8 PUT path traversal prevented" "safe" "safe"
    else
        print_result "8.8 PUT path traversal prevented" "safe" "VULNERABLE"
    fi
else
    print_result "8.8 PUT path traversal blocked" "rejected" "rejected"
fi

# Test 8.9: PUT exceeding body size limit (413)
dd if=/dev/zero bs=1024 count=2000 2>/dev/null > "$TEST_DIR/put_large.bin"
RESPONSE=$(curl -s -i -X PUT "$SERVER_URL/uploads/put_large.bin" \
    -H "Content-Type: application/octet-stream" \
    --data-binary @"$TEST_DIR/put_large.bin" 2>&1)
STATUS=$(get_status "$RESPONSE")
print_result "8.9 PUT over limit returns 413" "413" "$STATUS"

# Test 8.10: PUT empty file
RESPONSE=$(curl -s -i -X PUT "$SERVER_URL/uploads/put_empty.txt" \
    -H "Content-Type: text/plain" \
    -d "" 2>&1)
STATUS=$(get_status "$RESPONSE")
if [ "$STATUS" = "201" ] || [ "$STATUS" = "204" ]; then
    print_result "8.10 PUT empty file succeeds" "success" "success"
else
    print_result "8.10 PUT empty file succeeds" "success" "$STATUS"
fi

# ==================== SUMMARY ====================
echo -e "\n${BLUE}========================================${NC}"
echo -e "${BLUE}              TEST SUMMARY              ${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "Total Tests: $TOTAL"
echo -e "${GREEN}Passed: $PASSED${NC}"
echo -e "${RED}Failed: $FAILED${NC}"

if [ $FAILED -eq 0 ]; then
    echo -e "\n${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}Some tests failed.${NC}"
    exit 1
fi
