<?php
/**
 * Test CGI Script for WebServ
 * This script demonstrates basic CGI functionality with GET and POST methods
 */

// Set content type header
header("Content-Type: text/html; charset=UTF-8");

// Get request method
$method = $_SERVER['REQUEST_METHOD'] ?? 'UNKNOWN';

// Start HTML output
echo "<!DOCTYPE html>\n";
echo "<html>\n";
echo "<head><title>CGI Test - PHP</title></head>\n";
echo "<body>\n";
echo "<h1>PHP CGI Test Page</h1>\n";

// Display request method
echo "<h2>Request Method: " . htmlspecialchars($method) . "</h2>\n";

// Display environment variables
echo "<h2>Environment Variables</h2>\n";
echo "<table border='1'>\n";
echo "<tr><th>Variable</th><th>Value</th></tr>\n";

$importantVars = array(
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
);

foreach ($importantVars as $var) {
    $value = isset($_SERVER[$var]) ? htmlspecialchars($_SERVER[$var]) : '<i>not set</i>';
    echo "<tr><td>" . htmlspecialchars($var) . "</td><td>" . $value . "</td></tr>\n";
}
echo "</table>\n";

// Display GET parameters
if (!empty($_GET)) {
    echo "<h2>GET Parameters</h2>\n";
    echo "<table border='1'>\n";
    echo "<tr><th>Key</th><th>Value</th></tr>\n";
    foreach ($_GET as $key => $value) {
        echo "<tr><td>" . htmlspecialchars($key) . "</td><td>" . htmlspecialchars($value) . "</td></tr>\n";
    }
    echo "</table>\n";
}

// Display POST data
if ($method === 'POST') {
    echo "<h2>POST Data</h2>\n";
    
    // Raw POST data
    $rawInput = file_get_contents('php://input');
    if (!empty($rawInput)) {
        echo "<h3>Raw Input:</h3>\n";
        echo "<pre>" . htmlspecialchars($rawInput) . "</pre>\n";
    }
    
    // Parsed POST data
    if (!empty($_POST)) {
        echo "<h3>Parsed POST Variables:</h3>\n";
        echo "<table border='1'>\n";
        echo "<tr><th>Key</th><th>Value</th></tr>\n";
        foreach ($_POST as $key => $value) {
            echo "<tr><td>" . htmlspecialchars($key) . "</td><td>" . htmlspecialchars($value) . "</td></tr>\n";
        }
        echo "</table>\n";
    }
}

// Test form for POST
echo "<h2>Test POST Form</h2>\n";
echo "<form method='POST' action='" . htmlspecialchars($_SERVER['SCRIPT_NAME'] ?? '/cgi-bin/test.php') . "'>\n";
echo "<label>Name: <input type='text' name='name' value='test'></label><br>\n";
echo "<label>Message: <input type='text' name='message' value='Hello CGI'></label><br>\n";
echo "<input type='submit' value='Submit POST'>\n";
echo "</form>\n";

// Test link for GET
echo "<h2>Test GET Link</h2>\n";
$scriptName = $_SERVER['SCRIPT_NAME'] ?? '/cgi-bin/test.php';
echo "<a href='" . htmlspecialchars($scriptName) . "?param1=value1&param2=value2'>Test GET with parameters</a>\n";

echo "<hr>\n";
echo "<p>PHP Version: " . phpversion() . "</p>\n";
echo "<p>Timestamp: " . date('Y-m-d H:i:s') . "</p>\n";

echo "</body>\n";
echo "</html>\n";
?>
