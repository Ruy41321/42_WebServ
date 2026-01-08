#!/usr/bin/env python3
"""
Error Test CGI Script for WebServ - Python
This script tests error handling by causing various errors
"""

import os
import sys

# Print CGI header
print("Content-Type: text/html")
print()

# UNCOMMENT ONE OF THE FOLLOWING TO TEST DIFFERENT SCENARIOS:

# 1. Division by zero
# result = 1 / 0

# 2. Undefined name error
# print(undefined_variable)

# 3. File not found
# with open('/nonexistent/file.txt', 'r') as f:
#     content = f.read()

# 4. Invalid header test (missing blank line) - uncomment these and comment the print above
# sys.stdout.write("Invalid-Header: test")  # No newline after header
# sys.exit(1)

# 5. Working error page for testing
print("<!DOCTYPE html>")
print("<html>")
print("<head><title>CGI Error Test - Python</title></head>")
print("<body>")
print("<h1>Python CGI Error Test Script</h1>")
print("<p>This script is working correctly.</p>")
print("<p>To test error handling:</p>")
print("<ol>")
print("<li>Edit this file and uncomment one of the error scenarios</li>")
print("<li>Reload the page to see how the server handles the error</li>")
print("</ol>")

# Display some environment info
print("<h2>Environment Check</h2>")
print(f"<p>Python version: {sys.version}</p>")
print(f"<p>Working directory: {os.getcwd()}</p>")

print("</body>")
print("</html>")
