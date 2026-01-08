<?php
/**
 * Infinite Loop CGI Script for WebServ
 * This script tests timeout handling by running an infinite loop
 * 
 * WARNING: This script will run until killed by the server timeout!
 */

// Set max_execution_time to unlimited (for testing server-side timeout)
set_time_limit(0);

header("Content-Type: text/html");

echo "<!DOCTYPE html>\n";
echo "<html>\n";
echo "<head><title>CGI Infinite Loop Test</title></head>\n";
echo "<body>\n";
echo "<h1>Starting infinite loop...</h1>\n";

// Flush output buffer so client sees something
ob_flush();
flush();

// Infinite loop with sleep - server should kill this after timeout
$counter = 0;
while (true) {
    $counter++;
    sleep(1);  // Sleep 1 second per iteration
    echo "<p>Iteration: $counter seconds</p>\n";
    ob_flush();
    flush();
}

echo "</body>\n";
echo "</html>\n";
?>
