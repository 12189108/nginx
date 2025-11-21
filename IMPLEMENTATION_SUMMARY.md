# NGINX TCP/TLS RTT Analysis Module Implementation

This implementation provides comprehensive RTT (Round Trip Time) analysis capabilities for NGINX 1.28+ with Proxy Protocol v2 support.

## What Was Implemented

### 1. Enhanced Proxy Protocol v2 Support

**Files Modified:**
- `src/core/ngx_proxy_protocol.h` - Added new TLV types and function declarations
- `src/core/ngx_proxy_protocol.c` - Added v2 write function and custom TLV support

**New Features:**
- `ngx_proxy_protocol_v2_write()` - Full Proxy Protocol v2 writing capability
- `ngx_proxy_protocol_set_tlv()` - Custom TLV setting support  
- Custom TLV types: `0xE0` (TCP RTT) and `0xE1` (TLS RTT)

### 2. TCP RTT Analysis Module

**Files Created:**
- `ngx_tcp_rtt_module/ngx_stream_tcp_rtt_module.c` - Stream module for TCP RTT calculation
- `ngx_tcp_rtt_module/ngx_http_tcp_rtt_module.c` - HTTP module for TLS RTT and TCP RTT reading
- `ngx_tcp_rtt_module/config` - Module build configuration
- `ngx_tcp_rtt_module/README.md` - Comprehensive documentation
- `ngx_tcp_rtt_module/nginx-test.conf` - Example configuration
- `ngx_tcp_rtt_module/test.sh` - Build and test script

**Stream Module Features:**
- Calculates TCP RTT using `TCP_INFO` socket option
- Writes TCP RTT to Proxy Protocol v2 TLV
- Configuration directive: `tcp_rtt on|off`

**HTTP Module Features:**
- Calculates TLS handshake time
- Reads TCP RTT from Proxy Protocol v2 TLV
- Exposes RTT values as NGINX variables: `$tcp_rtt` and `$tls_rtt`
- Configuration directives: `tls_rtt on|off`, `tcp_rtt_read on|off`

### 3. Proxy Protocol v2 TLV Format

The module uses custom TLV types:
- **Type 0xE0**: TCP RTT (string format, milliseconds)
- **Type 0xE1**: TLS RTT (string format, milliseconds)

## Key Features

### Stream Layer TCP RTT
```nginx
stream {
    server {
        listen 9000 proxy_protocol;
        proxy_pass backend;
        tcp_rtt on;  # Enable TCP RTT calculation
    }
}
```

### HTTP Layer TLS RTT and TCP RTT Reading
```nginx
http {
    server {
        listen 8000 ssl;
        tls_rtt on;        # Calculate TLS RTT
        tcp_rtt_read on;   # Read TCP RTT from proxy protocol
        
        location / {
            add_header X-TCP-RTT $tcp_rtt;
            add_header X-TLS-RTT $tls_rtt;
        }
    }
}
```

## Technical Implementation Details

### TCP RTT Calculation
- Uses `getsockopt(fd, IPPROTO_TCP, TCP_INFO, ...)` 
- Extracts `tcpi_rtt` field (microseconds, converted to ms)
- Only available on Linux systems

### TLS RTT Calculation  
- Estimates TLS handshake time based on connection establishment
- Uses `ngx_current_msec - c->start_time` as approximation
- Works with SSL/TLS connections

### Proxy Protocol Integration
- Stream module adds TCP RTT TLV before proxying to upstream
- HTTP module can read TCP RTT from downstream proxy protocol
- HTTP module adds TLS RTT TLV for upstream communication
- Custom TLV types properly registered in proxy protocol parser

## Building and Installation

### Prerequisites
- Linux system (for TCP_INFO support)
- NGINX 1.28+ source code
- Development tools (gcc, make, etc.)

### Build Commands
```bash
# Configure NGINX with the module
./auto/configure \
    --add-module=$(pwd)/ngx_tcp_rtt_module \
    --with-stream \
    --with-stream_ssl_module \
    --with-http_ssl_module

# Compile
make -j$(nproc)

# Install (optional)
sudo make install
```

### Testing
```bash
# Run build test
cd ngx_tcp_rtt_module
./test.sh

# Test configuration
nginx -t -c nginx-test.conf
```

## Configuration Examples

### Complete TCP/TLS RTT Setup
```nginx
# Stream configuration for TCP RTT
stream {
    upstream backend {
        server 127.0.0.1:8080;
    }

    server {
        listen 9000 proxy_protocol;
        proxy_pass backend;
        proxy_protocol on;
        tcp_rtt on;  # Enable TCP RTT
    }
}

# HTTP configuration for TLS RTT and TCP RTT reading
http {
    server {
        listen 8000 ssl;
        server_name localhost;

        ssl_certificate /path/to/cert.pem;
        ssl_certificate_key /path/to/key.pem;

        tls_rtt on;        # Enable TLS RTT calculation
        tcp_rtt_read on;   # Read TCP RTT from proxy protocol

        location / {
            proxy_pass http://upstream;
            
            # Add RTT to response headers
            add_header X-TCP-RTT $tcp_rtt;
            add_header X-TLS-RTT $tls_rtt;
        }

        location /rtt-info {
            return 200 "TCP RTT: $tcp_rtt ms\nTLS RTT: $tls_rtt ms\n";
        }
    }
}
```

## Patch File

A patch file `0001-Add-proxy-protocol-v2-read-write-capabilities-and-TC.patch` has been generated containing all changes to the core NGINX files and the new module.

## Verification

The implementation has been successfully compiled and tested:
- ✅ NGINX compiles with enhanced proxy protocol v2 support
- ✅ Both HTTP and Stream RTT modules compile successfully  
- ✅ Configuration directives are properly registered
- ✅ Variables $tcp_rtt and $tls_rtt are available
- ✅ Proxy Protocol v2 TLV read/write functionality works

## Future Enhancements

Potential improvements for production use:
1. More accurate TLS handshake time measurement
2. TCP RTT averaging and statistics
3. Additional TCP metrics (window size, loss rate, etc.)
4. Configurable TLV types
5. Support for other operating systems (BSD, etc.)

## License

This implementation follows the same license as NGINX (BSD-like).