#!/bin/bash

# Test script for NGINX TCP RTT Analysis Module

set -e

echo "Testing NGINX TCP RTT Analysis Module..."

# Check if we're in the nginx source directory
if [ ! -f "configure" ]; then
    echo "Error: configure script not found. Please run this from nginx source directory."
    exit 1
fi

# Auto-configure if needed
if [ ! -f "Makefile" ]; then
    echo "Running auto-configure..."
    ./auto/configure \
        --add-module=$(pwd)/ngx_tcp_rtt_module \
        --with-stream \
        --with-stream_ssl_module \
        --with-http_ssl_module \
        --with-debug \
        --prefix=$(pwd)/nginx-test
fi

# Compile
echo "Compiling NGINX with TCP RTT module..."
make -j$(nproc)

echo "Compilation successful!"

# Check if the module is properly linked
echo "Checking for module symbols..."
if objobjs/nginx -l | grep -q "tcp_rtt"; then
    echo "✓ TCP RTT module symbols found"
else
    echo "⚠ TCP RTT module symbols not found (may be normal for dynamic modules)"
fi

echo ""
echo "To test the module:"
echo "1. Run: sudo make install"
echo "2. Start nginx with: sudo $(pwd)/nginx-test/sbin/nginx -c $(pwd)/ngx_tcp_rtt_module/nginx-test.conf"
echo "3. Test with: curl -k https://localhost:8000/rtt-info"
echo "4. Check logs in: $(pwd)/nginx-test/logs/"

echo ""
echo "Configuration directives added:"
echo "- Stream: tcp_rtt on|off"
echo "- HTTP: tls_rtt on|off, tcp_rtt_read on|off"
echo "- Variables: \$tcp_rtt, \$tls_rtt"