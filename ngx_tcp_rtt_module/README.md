# NGINX TCP RTT Analysis Module

This module provides TCP and TLS RTT (Round Trip Time) analysis capabilities for NGINX with Proxy Protocol v2 support.

## Features

- **Stream Module**: Calculates TCP RTT and writes it to Proxy Protocol v2 TLV
- **HTTP Module**: 
  - Calculates TLS RTT and writes it to Proxy Protocol v2 TLV
  - Reads TCP RTT from Proxy Protocol v2 TLV passed from stream layer
  - Exposes RTT values as NGINX variables

## Configuration Directives

### Stream Module

- `tcp_rtt on|off` - Enable TCP RTT calculation and Proxy Protocol v2 TLV writing

### HTTP Module

- `tls_rtt on|off` - Enable TLS RTT calculation and Proxy Protocol v2 TLV writing
- `tcp_rtt_read on|off` - Enable reading TCP RTT from Proxy Protocol v2 TLV

### Variables

The following variables are available in the HTTP module:

- `$tcp_rtt` - TCP RTT in milliseconds (from Proxy Protocol v2)
- `$tls_rtt` - TLS handshake time in milliseconds

## Example Configuration

```nginx
# Stream configuration for TCP RTT
stream {
    upstream backend {
        server 127.0.0.1:8080;
    }

    server {
        listen 9000;
        proxy_pass backend;
        proxy_protocol on;
        
        # Enable TCP RTT calculation
        tcp_rtt on;
        
        # Use Proxy Protocol v2
        proxy_protocol on;
    }
}

# HTTP configuration for TLS RTT and reading TCP RTT
http {
    server {
        listen 8000;
        server_name localhost;

        # Enable TLS
        ssl_certificate     /path/to/cert.pem;
        ssl_certificate_key /path/to/key.pem;

        # Enable RTT analysis
        tls_rtt on;
        tcp_rtt_read on;

        location / {
            proxy_pass http://backend;
            
            # Add RTT values to headers
            add_header X-TCP-RTT $tcp_rtt;
            add_header X-TLS-RTT $tls_rtt;
        }
    }
}
```

## Building

To build NGINX with this module:

```bash
./configure --add-module=/path/to/ngx_tcp_rtt_module \
           --with-stream \
           --with-stream_ssl_module \
           --with-http_ssl_module
make
make install
```

## Proxy Protocol v2 TLV Format

This module uses custom TLV types for RTT data:

- `0xE0` - TCP RTT (milliseconds, string format)
- `0xE1` - TLS RTT (milliseconds, string format)

## Requirements

- Linux (for TCP_INFO socket option)
- NGINX 1.28+
- Proxy Protocol v2 support

## Notes

- TCP RTT is calculated using the TCP_INFO socket option
- TLS RTT is estimated based on connection establishment time
- Values are passed between stream and HTTP modules via Proxy Protocol v2 TLVs
- All RTT values are in milliseconds

## License

This module follows the same license as NGINX.