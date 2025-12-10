#!/bin/bash

# Test Logging Helper - Provides common logging functionality for all test scripts

# Setup logging for current test script
# Usage: setup_test_logging "test_name"
setup_test_logging() {
    local test_name="$1"
    
    # Validate input
    if [ -z "$test_name" ]; then
        echo "Error: test_name not provided to setup_test_logging"
        return 1
    fi
    
    # Validate PROJECT_DIR is set
    if [ -z "$PROJECT_DIR" ]; then
        echo "Error: PROJECT_DIR not set"
        return 1
    fi
    
    # Create log directory with current date and time precision (to the minute)
    local timestamp=$(date "+%Y%m%d_%H%M")
    TEST_LOG_DIR="${PROJECT_DIR}/.test_logs/${timestamp}"
    mkdir -p "$TEST_LOG_DIR" || {
        echo "Error: Failed to create log directory $TEST_LOG_DIR"
        return 1
    }
    
    # Set log file path for this specific test
    TEST_LOG_FILE="${TEST_LOG_DIR}/${test_name}_log.txt"
    
    # Clear log file if it exists (overwrite if run in same minute)
    : > "$TEST_LOG_FILE" || {
        echo "Error: Failed to initialize log file $TEST_LOG_FILE"
        return 1
    }
    
    export TEST_LOG_DIR
    export TEST_LOG_FILE
}

# Start server with output redirected to test log file
# Usage: start_server_with_logging "config_file"
start_server_with_logging() {
    local config_file="$1"
    
    if [ -z "$TEST_LOG_FILE" ]; then
        echo "Error: TEST_LOG_FILE not set. Call setup_test_logging first."
        return 1
    fi
    
    if [ -z "$WEBSERV_BIN" ]; then
        echo "Error: WEBSERV_BIN not set"
        return 1
    fi
    
    if [ ! -f "$config_file" ]; then
        echo "Error: Config file not found: $config_file"
        return 1
    fi
    
    if [ ! -x "$WEBSERV_BIN" ]; then
        echo "Error: Webserv binary not executable: $WEBSERV_BIN"
        return 1
    fi
    
    "$WEBSERV_BIN" "$config_file" >> "$TEST_LOG_FILE" 2>&1 &
    export SERVER_PID=$!
    
    return 0
}

# Get the path to the current test log file
# Usage: log_file=$(get_test_log_file)
get_test_log_file() {
    echo "$TEST_LOG_FILE"
}

# Get the path to the current test log directory
# Usage: log_dir=$(get_test_log_dir)
get_test_log_dir() {
    echo "$TEST_LOG_DIR"
}
