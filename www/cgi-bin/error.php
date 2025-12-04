<?php
/**
 * Error Test CGI Script for WebServ
 * This script tests error handling by causing various errors
 */

// Uncomment one of the following to test different error scenarios:

// 1. Parse error (syntax error) - uncomment to test
// This will cause a parse error
// echo "Missing semicolon"

// 2. Runtime error - division by zero
// header("Content-Type: text/html");
// echo "<h1>Division by zero test</h1>";
// $result = 1 / 0;

// 3. Undefined variable warning (converted to error with strict settings)
// header("Content-Type: text/html");
// echo $undefined_variable;

// 4. Invalid CGI header (missing blank line) - this should cause the server to return 500
// Note: This simulates a malformed CGI response
// print "Invalid header without proper line ending";
// exit;

// 5. Working error page for testing
header("Content-Type: text/html");
echo "<!DOCTYPE html>\n";
echo "<html>\n";
echo "<head><title>CGI Error Test</title></head>\n";
echo "<body>\n";
echo "<h1>CGI Error Test Script</h1>\n";
echo "<p>This script is working correctly.</p>\n";
echo "<p>To test error handling:</p>\n";
echo "<ol>\n";
echo "<li>Edit this file and uncomment one of the error scenarios</li>\n";
echo "<li>Reload the page to see how the server handles the error</li>\n";
echo "</ol>\n";

// Trigger a warning to test error handling
error_reporting(E_ALL);
$test = array();
// Accessing undefined index
@$value = $test['undefined_key'];  // @ suppresses the warning for this demo

echo "<p>Timestamp: " . date('Y-m-d H:i:s') . "</p>\n";
echo "</body>\n";
echo "</html>\n";
?>
