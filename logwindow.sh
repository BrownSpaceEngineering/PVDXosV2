#!/usr/bin/expect

# Set the timeout to 1 second to handle connection setup
set timeout 1

# IP address and port of the target system
set ip "localhost"
set port "19021"

# Spawn telnet session
spawn nc $ip $port

# Wait for any output to ensure the connection is ready, then send the config string
expect {
    timeout { send_user "Connection timed out\n"; exit }
    eof { send_user "Connection failed\n"; exit }
    -re . { send "\$\$SEGGER_TELNET_ConfigStr=RTTCh;1\$\$\r" }
}

# Interact with the session after sending the command
interact
