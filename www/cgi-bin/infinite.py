#!/usr/bin/env python3
"""
Infinite Loop CGI Script for WebServ - Python
This script tests timeout handling by running an infinite loop

WARNING: This script will run until killed by the server timeout!
"""

import sys
import hashlib
import time

# Print CGI header
print("Content-Type: text/html")
print()

print("<!DOCTYPE html>")
print("<html>")
print("<head><title>CGI Infinite Loop Test - Python</title></head>")
print("<body>")
print("<h1>Starting infinite loop...</h1>")

# Flush output
sys.stdout.flush()

# Infinite loop - server should kill this after timeout
counter = 0
while True:
    counter += 1
    # Do some busy work
    hash_obj = hashlib.md5(str(counter).encode())
    
    # Occasional output
    if counter % 1000000 == 0:
        print(f"<p>Iteration: {counter}</p>")
        sys.stdout.flush()
    
    # Safety limit (remove for true infinite loop testing)
    if counter > 100000000:
        print("<p>Safety limit reached</p>")
        break

print("</body>")
print("</html>")
