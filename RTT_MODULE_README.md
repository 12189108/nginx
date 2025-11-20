# Nginx RTT Module

## Overview

This enhancement adds TCP and TLS RTT (Round-Trip Time) measurement capabilities to Nginx, with support for passing these metrics through PROXY Protocol v2 TLV (Type-Length-Value) fields.

## Features

### 1. PROXY Protocol v2 Extensions

- **Read Support**: Parse TCP RTT and TLS RTT from PROXY Protocol v2 TLV fields
- **Write Support**: Include TCP RTT and TLS RTT in outgoing PROXY Protocol v2 headers
- **Custom TLV Types**:
  - `0xE0`: TCP RTT (in microseconds)
  - `0xE1`: TLS RTT (in microseconds)

### 2. Stream Module RTT Support

The `ngx_stream_rtt_module` provides:

- **Variable**: `$tcp_rtt` - TCP round-trip time in microseconds
- **Measurement**: Automatically measures TCP RTT using `TCP_INFO` socket option (Linux) or `TCP_CONNECTION_INFO` (macOS)
- **PROXY Protocol Integration**: TCP RTT is passed through PROXY Protocol v2 TLV when forwarding connections

### 3. HTTP Module RTT Support

The `ngx_http_rtt_module` provides:

- **Variables**:
  - `$tcp_rtt` - TCP round-trip time in microseconds
  - `$tls_handshake_rtt` - TLS handshake time in milliseconds
- **TCP RTT**: Measured using socket options or read from incoming PROXY Protocol v2
- **TLS RTT**: Calculated by measuring SSL/TLS handshake duration
- **PROXY Protocol Integration**: Both RTT values can be forwarded via PROXY Protocol v2

## Building

### Configuration

```bash
./auto/configure \
    --with-stream \
    --with-stream_ssl_module \
    --with-http_ssl_module
```

### Compilation

```bash
make
make install
```

## Usage Examples

### Stream Configuration

```nginx
stream {
    server {
        listen 8000 proxy_protocol;
        
        log_format rtt '$remote_addr - tcp_rtt=$tcp_rtt';
        access_log /var/log/nginx/stream.log rtt;
        
        proxy_pass backend;
        proxy_protocol on;  # Forward with PROXY Protocol v2 + RTT TLV
    }
}
```

### HTTP Configuration

```nginx
http {
    log_format rtt_log '$remote_addr - tcp_rtt=$tcp_rtt tls_rtt=$tls_handshake_rtt';
    
    server {
        listen 443 ssl proxy_protocol;
        
        ssl_certificate /path/to/cert.pem;
        ssl_certificate_key /path/to/key.pem;
        
        access_log /var/log/nginx/access.log rtt_log;
        
        location / {
            # Use RTT values in headers
            add_header X-TCP-RTT $tcp_rtt always;
            add_header X-TLS-RTT $tls_handshake_rtt always;
            
            proxy_pass http://backend;
            proxy_protocol on;  # Forward with RTT information
        }
    }
}
```

## Technical Details

### TCP RTT Measurement

TCP RTT is obtained through platform-specific socket options:

- **Linux**: `getsockopt(TCP_INFO)` provides `tcpi_rtt` (in microseconds)
- **macOS/BSD**: `getsockopt(TCP_CONNECTION_INFO)` provides `tcpi_rttcur` (in microseconds)

The measurement represents the smoothed round-trip time (SRTT) maintained by the TCP stack.

### TLS RTT Measurement

TLS handshake RTT is calculated by:

1. Recording timestamp before SSL/TLS handshake begins
2. Recording timestamp after handshake completes
3. Computing the difference (in milliseconds)

This measures the time taken for the complete TLS handshake, including:
- ClientHello/ServerHello exchange
- Certificate verification
- Key exchange
- Finished messages

### PROXY Protocol v2 TLV Format

RTT values are encoded as custom TLV fields:

```
Type: 1 byte (0xE0 for TCP RTT, 0xE1 for TLS RTT)
Length: 2 bytes (0x0004 - always 4 bytes)
Value: 4 bytes (RTT in microseconds, big-endian uint32)
```

Example TLV encoding for TCP RTT of 1000 microseconds:
```
E0 00 04 00 00 03 E8
```

## Platform Support

### Supported Platforms

- **Linux**: Full support (TCP RTT via TCP_INFO)
- **macOS**: Full support (TCP RTT via TCP_CONNECTION_INFO)
- **FreeBSD/OpenBSD**: Partial support (TLS RTT only)

### Requirements

- Nginx 1.29.4+
- OpenSSL (for TLS features)
- Linux 2.6+ or macOS 10.10+ (for TCP RTT)

## API

### Core Functions

#### ngx_proxy_protocol_v2_write

```c
u_char *ngx_proxy_protocol_v2_write(ngx_connection_t *c, u_char *buf, u_char *last);
```

Writes PROXY Protocol v2 header including TCP and TLS RTT TLVs if available.

#### Proxy Protocol Structure

```c
struct ngx_proxy_protocol_s {
    ngx_str_t    src_addr;
    ngx_str_t    dst_addr;
    in_port_t    src_port;
    in_port_t    dst_port;
    ngx_str_t    tlvs;
    ngx_uint_t   tcp_rtt;    // TCP RTT in microseconds
    ngx_uint_t   tls_rtt;    // TLS RTT in microseconds
};
```

## Performance Considerations

- TCP RTT measurement uses cached kernel data (no additional network traffic)
- TLS RTT measurement has negligible overhead (simple timestamp difference)
- PROXY Protocol v2 overhead: +14 bytes (2 TLVs × 7 bytes each)

## Limitations

- TLS RTT measurement requires `NGX_HTTP_SSL` to be enabled
- TCP RTT measurement not available on all platforms
- RTT values may not be available immediately after connection establishment

## Troubleshooting

### TCP RTT shows 0 or not_found

- Check platform support (Linux/macOS required)
- Ensure connection is established (RTT not available during handshake)
- Verify socket is TCP (not UDP)

### TLS RTT not available

- Confirm SSL/TLS is enabled on the server
- Check that `ssl_certificate` and `ssl_certificate_key` are configured
- Verify client completes TLS handshake

## References

- [PROXY Protocol Specification](https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt)
- [Linux TCP_INFO](https://man7.org/linux/man-pages/man7/tcp.7.html)
- [RFC 8446 - TLS 1.3](https://tools.ietf.org/html/rfc8446)

## License

Copyright (C) Nginx, Inc.

This code follows the nginx license (2-clause BSD-like license).
