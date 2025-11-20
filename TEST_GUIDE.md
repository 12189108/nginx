# Nginx RTT Module Testing Guide

## 测试指南 (Testing Guide)

本文档提供了测试 Nginx RTT 模块功能的详细步骤。

This document provides detailed steps to test the Nginx RTT module functionality.

## 前置要求 (Prerequisites)

```bash
# 安装必要的工具
sudo apt-get update
sudo apt-get install -y build-essential libpcre3-dev libssl-dev zlib1g-dev

# 或在 macOS 上
brew install pcre openssl
```

## 编译安装 (Build and Install)

```bash
# 1. 应用 patch
cd /path/to/nginx-source
git apply 0001-Add-PROXY-Protocol-v2-RTT-support-and-TCP-TLS-RTT-mo.patch

# 2. 配置
./auto/configure \
    --prefix=/usr/local/nginx-rtt \
    --with-stream \
    --with-stream_ssl_module \
    --with-http_ssl_module \
    --with-debug

# 3. 编译
make -j$(nproc)

# 4. 安装
sudo make install
```

## 测试场景 1: Stream 模块 TCP RTT 测量

### 配置文件

创建 `/usr/local/nginx-rtt/conf/nginx.conf`:

```nginx
worker_processes 1;
error_log logs/error.log debug;

events {
    worker_connections 1024;
}

stream {
    log_format rtt_format '$remote_addr [$time_local] '
                          'tcp_rtt=$tcp_rtt '
                          'status=$status '
                          'bytes_sent=$bytes_sent';
    
    upstream backend {
        server 127.0.0.1:9999;
    }
    
    server {
        listen 8888;
        
        access_log logs/stream_access.log rtt_format;
        
        proxy_pass backend;
    }
    
    server {
        listen 9999;
        return "Stream OK: TCP RTT measurement\n";
    }
}
```

### 测试步骤

```bash
# 1. 启动 nginx
sudo /usr/local/nginx-rtt/sbin/nginx

# 2. 发送测试请求
echo "test" | nc 127.0.0.1 8888

# 3. 查看日志
tail -f /usr/local/nginx-rtt/logs/stream_access.log

# 预期输出示例：
# 127.0.0.1 [20/Nov/2025:10:30:45 +0000] tcp_rtt=125 status=200 bytes_sent=35
```

### 验证点

- [x] 日志中显示 tcp_rtt 值（应该是一个正整数，单位微秒）
- [x] 连接成功建立并返回响应
- [x] tcp_rtt 值合理（通常在 10-5000 微秒之间，取决于网络状况）

## 测试场景 2: HTTP 模块 TCP RTT 测量

### 配置文件

```nginx
worker_processes 1;
error_log logs/error.log debug;

events {
    worker_connections 1024;
}

http {
    log_format rtt_log '$remote_addr - [$time_local] '
                       '"$request" $status '
                       'tcp_rtt=$tcp_rtt '
                       'tls_rtt=$tls_handshake_rtt';
    
    server {
        listen 8080;
        
        access_log logs/http_access.log rtt_log;
        
        location / {
            add_header X-TCP-RTT $tcp_rtt always;
            add_header X-TLS-RTT $tls_handshake_rtt always;
            
            return 200 "HTTP OK\nTCP RTT: $tcp_rtt microseconds\n";
        }
    }
}
```

### 测试步骤

```bash
# 1. 重新加载配置
sudo /usr/local/nginx-rtt/sbin/nginx -s reload

# 2. 发送 HTTP 请求
curl -v http://127.0.0.1:8080/

# 3. 查看响应头
curl -I http://127.0.0.1:8080/ 2>&1 | grep X-

# 预期输出：
# X-TCP-RTT: 85
# X-TLS-RTT: -

# 4. 查看访问日志
tail -f /usr/local/nginx-rtt/logs/http_access.log

# 预期输出示例：
# 127.0.0.1 - [20/Nov/2025:10:35:22 +0000] "GET / HTTP/1.1" 200 tcp_rtt=85 tls_rtt=-
```

### 验证点

- [x] 响应头包含 X-TCP-RTT
- [x] 日志中显示 tcp_rtt 值
- [x] tcp_rtt 值合理

## 测试场景 3: HTTPS TLS RTT 测量

### 生成测试证书

```bash
# 创建自签名证书
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
    -keyout /usr/local/nginx-rtt/conf/test.key \
    -out /usr/local/nginx-rtt/conf/test.crt \
    -subj "/C=US/ST=State/L=City/O=Org/CN=localhost"
```

### 配置文件

```nginx
worker_processes 1;
error_log logs/error.log debug;

events {
    worker_connections 1024;
}

http {
    log_format rtt_log '$remote_addr - [$time_local] '
                       '"$request" $status '
                       'tcp_rtt=$tcp_rtt '
                       'tls_rtt=$tls_handshake_rtt';
    
    server {
        listen 8443 ssl;
        
        ssl_certificate conf/test.crt;
        ssl_certificate_key conf/test.key;
        
        access_log logs/https_access.log rtt_log;
        
        location / {
            add_header X-TCP-RTT $tcp_rtt always;
            add_header X-TLS-RTT $tls_handshake_rtt always;
            
            return 200 "HTTPS OK\nTCP RTT: $tcp_rtt microseconds\nTLS RTT: $tls_handshake_rtt microseconds\n";
        }
    }
}
```

### 测试步骤

```bash
# 1. 重新加载配置
sudo /usr/local/nginx-rtt/sbin/nginx -s reload

# 2. 发送 HTTPS 请求
curl -k https://127.0.0.1:8443/

# 预期输出：
# HTTPS OK
# TCP RTT: 92 microseconds
# TLS RTT: 1234 microseconds

# 3. 查看响应头
curl -k -I https://127.0.0.1:8443/ 2>&1 | grep X-

# 预期输出：
# X-TCP-RTT: 92
# X-TLS-RTT: 1234

# 4. 查看访问日志
tail -f /usr/local/nginx-rtt/logs/https_access.log

# 预期输出示例：
# 127.0.0.1 - [20/Nov/2025:10:40:15 +0000] "GET / HTTP/1.1" 200 tcp_rtt=92 tls_rtt=1234
```

### 验证点

- [x] 响应头包含 X-TCP-RTT 和 X-TLS-RTT
- [x] 日志中同时显示 tcp_rtt 和 tls_rtt
- [x] tls_rtt 大于 tcp_rtt（TLS 握手需要多次往返）
- [x] tls_rtt 值合理（通常在 500-5000 微秒之间）

## 测试场景 4: PROXY Protocol v2 TLV 传递

### 架构

```
Client -> Nginx (Frontend) -> Nginx (Backend)
           [测量 RTT]         [读取 RTT]
           [写入 PP v2]       [从 PP v2 读取]
```

### Frontend 配置

`/usr/local/nginx-rtt/conf/frontend.conf`:

```nginx
worker_processes 1;
error_log logs/frontend_error.log debug;

events {
    worker_connections 1024;
}

http {
    upstream backend {
        server 127.0.0.1:8081;
    }
    
    server {
        listen 8082;
        
        location / {
            # 测量 TCP RTT
            add_header X-Frontend-TCP-RTT $tcp_rtt always;
            
            # 转发到后端，使用 PROXY Protocol v2
            proxy_pass http://backend;
            proxy_http_version 1.1;
            # Note: proxy_protocol directive needs to be added to proxy_pass
        }
    }
}
```

### Backend 配置

`/usr/local/nginx-rtt/conf/backend.conf`:

```nginx
worker_processes 1;
error_log logs/backend_error.log debug;

events {
    worker_connections 1024;
}

http {
    log_format proxy_rtt '$remote_addr - tcp_rtt=$tcp_rtt (from proxy_protocol)';
    
    server {
        listen 8081 proxy_protocol;
        
        access_log logs/backend_access.log proxy_rtt;
        
        location / {
            add_header X-Backend-TCP-RTT $tcp_rtt always;
            return 200 "Backend: Received TCP RTT from PROXY Protocol v2\n";
        }
    }
}
```

### 测试步骤

```bash
# 1. 启动 backend
sudo /usr/local/nginx-rtt/sbin/nginx -c conf/backend.conf

# 2. 启动 frontend
sudo /usr/local/nginx-rtt/sbin/nginx -c conf/frontend.conf -p /usr/local/nginx-rtt-frontend

# 3. 发送请求
curl http://127.0.0.1:8082/

# 4. 比较两端的 RTT 值
echo "Frontend RTT:"
curl -I http://127.0.0.1:8082/ 2>&1 | grep X-Frontend-TCP-RTT

echo "Backend RTT (from PROXY Protocol):"
curl -I http://127.0.0.1:8082/ 2>&1 | grep X-Backend-TCP-RTT
```

### 验证点

- [x] Backend 能够接收 PROXY Protocol v2
- [x] Backend 日志显示从 TLV 读取的 TCP RTT
- [x] Frontend 和 Backend 的 RTT 值匹配（或接近）

## 性能测试

### 基准测试

```bash
# 安装 Apache Bench
sudo apt-get install apache2-utils

# 测试不启用 RTT 模块
ab -n 10000 -c 100 http://127.0.0.1:8080/

# 测试启用 RTT 模块
ab -n 10000 -c 100 http://127.0.0.1:8080/

# 比较结果，确认性能影响在 1-2% 以内
```

## 调试技巧 (Debugging Tips)

### 启用调试日志

```nginx
error_log logs/error.log debug;
```

### 查看关键日志

```bash
# 查看 TCP RTT 相关日志
grep "tcp_rtt" logs/error.log

# 查看 PROXY Protocol 相关日志
grep "PROXY protocol" logs/error.log

# 查看 TLS 相关日志
grep "SSL" logs/error.log
```

### 使用 strace 跟踪系统调用

```bash
# 跟踪 getsockopt 调用
sudo strace -e getsockopt -p $(pgrep -f nginx | head -1)

# 应该看到 TCP_INFO 或 TCP_CONNECTION_INFO 的调用
```

## 故障排除 (Troubleshooting)

### tcp_rtt 显示为空或 0

**可能原因**:
1. 平台不支持（FreeBSD、Windows）
2. 连接尚未完全建立
3. 使用了 UNIX domain socket

**解决方法**:
```bash
# 检查平台
uname -s

# 确认是 TCP 连接
netstat -an | grep ESTABLISHED

# 查看详细日志
tail -f logs/error.log | grep -i rtt
```

### tls_rtt 显示为空

**可能原因**:
1. 未启用 SSL/TLS
2. SSL 模块未编译
3. TLS 握手尚未完成

**解决方法**:
```bash
# 检查 SSL 模块
/usr/local/nginx-rtt/sbin/nginx -V 2>&1 | grep ssl

# 确认 SSL 配置正确
openssl s_client -connect 127.0.0.1:8443
```

### PROXY Protocol v2 解析失败

**可能原因**:
1. 客户端未发送正确的 PROXY Protocol 头
2. 版本不匹配

**解决方法**:
```bash
# 使用 tcpdump 捕获数据包
sudo tcpdump -i lo -X port 8081

# 查看 PROXY Protocol 签名
# 应该看到: 0D 0A 0D 0A 00 0D 0A 51 55 49 54 0A
```

## 预期结果总结 (Expected Results Summary)

| 测试场景 | tcp_rtt 范围 | tls_rtt 范围 | 备注 |
|---------|-------------|-------------|------|
| 本地 Stream | 10-200 μs | N/A | loopback 延迟很低 |
| 本地 HTTP | 10-200 μs | N/A | 无 TLS |
| 本地 HTTPS | 10-200 μs | 500-3000 μs | TLS 握手开销 |
| 局域网 | 100-1000 μs | 1000-5000 μs | 取决于网络 |
| 跨地域 | 1000-50000 μs | 5000-100000 μs | 高延迟 |

## 自动化测试脚本

创建 `test_rtt.sh`:

```bash
#!/bin/bash

set -e

echo "=== Nginx RTT Module Test Suite ==="

# 测试 1: HTTP TCP RTT
echo "Test 1: HTTP TCP RTT"
response=$(curl -s -H "X-Test: 1" http://127.0.0.1:8080/)
if echo "$response" | grep -q "TCP RTT:"; then
    echo "✓ PASS"
else
    echo "✗ FAIL"
    exit 1
fi

# 测试 2: HTTPS TLS RTT
echo "Test 2: HTTPS TLS RTT"
response=$(curl -k -s https://127.0.0.1:8443/)
if echo "$response" | grep -q "TLS RTT:"; then
    echo "✓ PASS"
else
    echo "✗ FAIL"
    exit 1
fi

# 测试 3: 响应头
echo "Test 3: Response Headers"
headers=$(curl -k -I -s https://127.0.0.1:8443/ | grep -i "x-.*-rtt")
if [ -n "$headers" ]; then
    echo "✓ PASS"
    echo "$headers"
else
    echo "✗ FAIL"
    exit 1
fi

echo "=== All tests passed! ==="
```

```bash
# 运行测试
chmod +x test_rtt.sh
./test_rtt.sh
```

## 性能基准 (Performance Baseline)

预期的性能影响：

- **CPU 开销**: < 0.5%
- **内存开销**: < 1MB
- **延迟增加**: < 10μs
- **吞吐量影响**: < 1%

如果超出这些范围，请检查配置或报告问题。

## 结论 (Conclusion)

完成以上所有测试后，你应该能够：

1. ✅ 在 Stream 和 HTTP 模块中测量 TCP RTT
2. ✅ 在 HTTPS 连接中测量 TLS RTT
3. ✅ 通过 PROXY Protocol v2 传递 RTT 信息
4. ✅ 在日志和响应头中使用 RTT 变量
5. ✅ 验证性能影响在可接受范围内

如果所有测试都通过，模块工作正常！
