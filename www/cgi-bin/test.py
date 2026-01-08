#!/usr/bin/env python3
"""
Test CGI Script for WebServ - Python
This script demonstrates basic CGI functionality with GET and POST methods
"""

import os
import sys
import cgi
import cgitb
from datetime import datetime

# Enable CGI traceback for debugging
cgitb.enable()

# Print CGI header
print("Content-Type: text/html; charset=UTF-8")
print()

# Get request method
method = os.environ.get('REQUEST_METHOD', 'UNKNOWN')

# Start HTML output
print("<!DOCTYPE html>")
print("<html>")
print("<head><title>CGI Test - Python</title></head>")
print("<body>")
print("<h1>Python CGI Test Page</h1>")

# Display request method
print(f"<h2>Request Method: {method}</h2>")

# Display environment variables
print("<h2>Environment Variables</h2>")
print("<table border='1'>")
print("<tr><th>Variable</th><th>Value</th></tr>")

important_vars = [
    'REQUEST_METHOD',
    'QUERY_STRING',
    'CONTENT_TYPE',
    'CONTENT_LENGTH',
    'SCRIPT_NAME',
    'SCRIPT_FILENAME',
    'PATH_INFO',
    'PATH_TRANSLATED',
    'SERVER_NAME',
    'SERVER_PORT',
    'SERVER_PROTOCOL',
    'GATEWAY_INTERFACE',
    'REMOTE_ADDR',
    'HTTP_HOST',
    'HTTP_USER_AGENT'
]

for var in important_vars:
    value = os.environ.get(var, '<i>not set</i>')
    # Escape HTML
    if value != '<i>not set</i>':
        value = value.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
    print(f"<tr><td>{var}</td><td>{value}</td></tr>")

print("</table>")

# Parse form data
form = cgi.FieldStorage()

# Display GET parameters
query_string = os.environ.get('QUERY_STRING', '')
if query_string:
    print("<h2>Query String Parameters</h2>")
    print("<table border='1'>")
    print("<tr><th>Key</th><th>Value</th></tr>")
    for key in form.keys():
        value = form.getvalue(key)
        if value:
            value = str(value).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
            print(f"<tr><td>{key}</td><td>{value}</td></tr>")
    print("</table>")

# Display POST data
if method == 'POST':
    print("<h2>POST Data</h2>")
    
    # Read raw stdin for display
    content_length = os.environ.get('CONTENT_LENGTH', '0')
    try:
        length = int(content_length)
    except ValueError:
        length = 0
    
    print("<h3>Form Fields:</h3>")
    if form.keys():
        print("<table border='1'>")
        print("<tr><th>Key</th><th>Value</th></tr>")
        for key in form.keys():
            value = form.getvalue(key)
            if value:
                value = str(value).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
                print(f"<tr><td>{key}</td><td>{value}</td></tr>")
        print("</table>")
    else:
        print("<p>No form fields found</p>")

# Test form for POST
script_name = os.environ.get('SCRIPT_NAME', '/cgi-bin/test.py')
print("<h2>Test POST Form</h2>")
print(f"<form method='POST' action='{script_name}'>")
print("<label>Name: <input type='text' name='name' value='test'></label><br>")
print("<label>Message: <input type='text' name='message' value='Hello Python CGI'></label><br>")
print("<input type='submit' value='Submit POST'>")
print("</form>")

# Test link for GET
print("<h2>Test GET Link</h2>")
print(f"<a href='{script_name}?param1=value1&param2=value2'>Test GET with parameters</a>")

print("<hr>")
print(f"<p>Python Version: {sys.version}</p>")
print(f"<p>Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>")

print("</body>")
print("</html>")
