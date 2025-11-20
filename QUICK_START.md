# Quick Start Guide - Nginx RTT Module

## 快速开始指南 (5分钟上手)

本指南帮助你在 5 分钟内快速部署和测试 Nginx RTT 模块。

This guide helps you deploy and test the Nginx RTT module in 5 minutes.

---

## 📦 一键安装 (One-Click Installation)

### Linux (Ubuntu/Debian)

```bash
#!/bin/bash
set -e

# 1. 下载 nginx 源码
cd /tmp
wget http://nginx.org/download/nginx-1.29.4.tar.gz
tar xzf nginx-1.29.4.tar.gz
cd nginx-1.29.4

# 2. 应用 patch
curl -O https://[your-patch-url]/0001-Add-PROXY-Protocol-v2-RTT-support-and-TCP-TLS-RTT-mo.patch
git init
git apply 0001-Add-PROXY-Protocol-v2-RTT-support-and-TCP-TLS-RTT-mo.patch

# 3. 安装依赖
sudo apt-get update
sudo apt-get install -y build-essential libpcre3-dev libssl-dev zlib1g-dev

# 4. 编译安装
./auto/configure \
    --prefix=/usr/local/nginx-rtt \
    --with-stream \
    --with-stream_ssl_module \
    --with-http_ssl_module

make -j$(nproc)
sudo make install

echo "✅ 安装完成！Nginx RTT 模块已安装到 /usr/local/nginx-rtt"
```

### macOS

```bash
#!/bin/bash
set -e

# 1. 安装依赖
brew install pcre openssl

# 2. 下载并解压
cd /tmp
curl -O http://nginx.org/download/nginx-1.29.4.tar.gz
tar xzf nginx-1.29.4.tar.gz
cd nginx-1.29.4

# 3. 应用 patch
curl -O https://[your-patch-url]/0001-Add-PROXY-Protocol-v2-RTT-support-and-TCP-TLS-RTT-mo.patch
git init
git apply 0001-Add-PROXY-Protocol-v2-RTT-support-and-TCP-TLS-RTT-mo.patch

# 4. 编译安装
./auto/configure \
    --prefix=/usr/local/nginx-rtt \
    --with-stream \
    --with-stream_ssl_module \
    --with-http_ssl_module \
    --with-cc-opt="-I/usr/local/opt/openssl/include" \
    --with-ld-opt="-L/usr/local/opt/openssl/lib"

make -j$(sysctl -n hw.ncpu)
sudo make install

echo "✅ 安装完成！"
```

---

## ⚡ 快速测试 (Quick Test)

### 测试 1: HTTP TCP RTT (30秒)

```bash
# 1. 创建配置文件
cat > /tmp/test-http-rtt.conf << 'EOF'
worker_processes 1;
daemon off;
error_log /dev/stderr info;

events {
    worker_connections 1024;
}

http {
    server {
        listen 8080;
        
        location / {
            add_header X-TCP-RTT $tcp_rtt always;
            return 200 "TCP RTT: $tcp_rtt microseconds\n";
        }
    }
}
EOF

# 2. 启动 nginx
sudo /usr/local/nginx-rtt/sbin/nginx -c /tmp/test-http-rtt.conf &
NGINX_PID=$!

# 3. 测试
sleep 2
echo "=== HTTP 响应 ==="
curl http://127.0.0.1:8080/

echo ""
echo "=== 响应头 ==="
curl -I http://127.0.0.1:8080/ 2>&1 | grep X-TCP-RTT

# 4. 清理
sudo kill $NGINX_PID 2>/dev/null || true
```

**预期输出**:
```
=== HTTP 响应 ===
TCP RTT: 85 microseconds

=== 响应头 ===
X-TCP-RTT: 85
```

### 测试 2: HTTPS TLS RTT (1分钟)

```bash
# 1. 生成测试证书
mkdir -p /tmp/nginx-ssl
openssl req -x509 -nodes -days 1 -newkey rsa:2048 \
    -keyout /tmp/nginx-ssl/test.key \
    -out /tmp/nginx-ssl/test.crt \
    -subj "/CN=localhost" 2>/dev/null

# 2. 创建配置
cat > /tmp/test-https-rtt.conf << 'EOF'
worker_processes 1;
daemon off;
error_log /dev/stderr info;

events {
    worker_connections 1024;
}

http {
    server {
        listen 8443 ssl;
        
        ssl_certificate /tmp/nginx-ssl/test.crt;
        ssl_certificate_key /tmp/nginx-ssl/test.key;
        
        location / {
            add_header X-TCP-RTT $tcp_rtt always;
            add_header X-TLS-RTT $tls_handshake_rtt always;
            return 200 "TCP RTT: $tcp_rtt\nTLS RTT: $tls_handshake_rtt\n";
        }
    }
}
EOF

# 3. 启动和测试
sudo /usr/local/nginx-rtt/sbin/nginx -c /tmp/test-https-rtt.conf &
NGINX_PID=$!
sleep 2

echo "=== HTTPS 响应 ==="
curl -k https://127.0.0.1:8443/

echo ""
echo "=== 响应头 ==="
curl -k -I https://127.0.0.1:8443/ 2>&1 | grep "X-.*-RTT"

# 4. 清理
sudo kill $NGINX_PID 2>/dev/null || true
rm -rf /tmp/nginx-ssl
```

**预期输出**:
```
=== HTTPS 响应 ===
TCP RTT: 92
TLS RTT: 1234

=== 响应头 ===
X-TCP-RTT: 92
X-TLS-RTT: 1234
```

### 测试 3: Stream TCP RTT (30秒)

```bash
# 1. 创建配置
cat > /tmp/test-stream-rtt.conf << 'EOF'
worker_processes 1;
daemon off;
error_log /dev/stderr info;

events {
    worker_connections 1024;
}

stream {
    log_format rtt '$remote_addr - tcp_rtt=$tcp_rtt';
    
    server {
        listen 9000;
        access_log /dev/stderr rtt;
        return "Stream TCP RTT Test\n";
    }
}
EOF

# 2. 启动
sudo /usr/local/nginx-rtt/sbin/nginx -c /tmp/test-stream-rtt.conf &
NGINX_PID=$!
sleep 2

# 3. 测试
echo "=== Stream 响应 ==="
echo "test" | nc 127.0.0.1 9000

# 4. 清理
sudo kill $NGINX_PID 2>/dev/null || true
```

**预期输出** (在 stderr 日志中):
```
127.0.0.1 - tcp_rtt=78
```

---

## 🎯 生产环境配置模板 (Production Config Template)

### 完整示例

```nginx
# /usr/local/nginx-rtt/conf/nginx.conf

user nginx;
worker_processes auto;
error_log /var/log/nginx/error.log warn;
pid /var/run/nginx.pid;

events {
    worker_connections 10240;
    use epoll;
}

# Stream 配置 - 用于 TCP 代理
stream {
    log_format stream_rtt '$remote_addr [$time_local] '
                          'tcp_rtt=$tcp_rtt '
                          'bytes_sent=$bytes_sent '
                          'bytes_received=$bytes_received '
                          'session_time=$session_time';
    
    access_log /var/log/nginx/stream_access.log stream_rtt;
    
    upstream backend_tcp {
        server backend1.example.com:8080;
        server backend2.example.com:8080;
    }
    
    server {
        listen 443;
        proxy_pass backend_tcp;
        proxy_timeout 60s;
        
        # 可选：转发 PROXY Protocol v2
        # proxy_protocol on;
    }
}

# HTTP 配置
http {
    include mime.types;
    default_type application/octet-stream;
    
    log_format main '$remote_addr - $remote_user [$time_local] '
                    '"$request" $status $body_bytes_sent '
                    '"$http_referer" "$http_user_agent" '
                    'tcp_rtt=$tcp_rtt tls_rtt=$tls_handshake_rtt';
    
    access_log /var/log/nginx/access.log main;
    
    sendfile on;
    tcp_nopush on;
    keepalive_timeout 65;
    
    # HTTPS 服务器
    server {
        listen 443 ssl http2;
        server_name example.com;
        
        ssl_certificate /etc/nginx/ssl/example.com.crt;
        ssl_certificate_key /etc/nginx/ssl/example.com.key;
        ssl_protocols TLSv1.2 TLSv1.3;
        ssl_ciphers HIGH:!aNULL:!MD5;
        
        # 在响应头中暴露 RTT 信息（仅用于调试）
        # add_header X-TCP-RTT $tcp_rtt always;
        # add_header X-TLS-RTT $tls_handshake_rtt always;
        
        location / {
            proxy_pass http://backend;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            
            # 可选：转发 PROXY Protocol v2
            # proxy_protocol on;
        }
        
        # RTT 监控端点
        location /rtt-status {
            access_log off;
            return 200 "tcp_rtt=$tcp_rtt\ntls_rtt=$tls_handshake_rtt\n";
        }
    }
    
    upstream backend {
        server backend1.example.com:8080;
        server backend2.example.com:8080;
    }
}
```

---

## 📊 监控集成 (Monitoring Integration)

### Prometheus Exporter 配置

```nginx
http {
    server {
        listen 9113;
        
        location /metrics {
            access_log off;
            
            # 暴露 RTT 指标（需要自定义 exporter）
            return 200 '# HELP nginx_tcp_rtt TCP Round-Trip Time
# TYPE nginx_tcp_rtt gauge
nginx_tcp_rtt{instance="$hostname"} $tcp_rtt
# HELP nginx_tls_rtt TLS Handshake Time
# TYPE nginx_tls_rtt gauge
nginx_tls_rtt{instance="$hostname"} $tls_handshake_rtt
';
        }
    }
}
```

### Grafana Dashboard JSON

```json
{
  "dashboard": {
    "title": "Nginx RTT Metrics",
    "panels": [
      {
        "title": "TCP RTT",
        "targets": [
          {
            "expr": "nginx_tcp_rtt"
          }
        ]
      },
      {
        "title": "TLS Handshake RTT",
        "targets": [
          {
            "expr": "nginx_tls_rtt"
          }
        ]
      }
    ]
  }
}
```

---

## 🔧 故障排除 (Troubleshooting)

### 问题 1: tcp_rtt 为空

```bash
# 检查平台支持
uname -s  # 应该是 Linux 或 Darwin (macOS)

# 查看错误日志
sudo tail -f /var/log/nginx/error.log | grep -i rtt

# 确认是 TCP 连接
netstat -an | grep ESTABLISHED
```

### 问题 2: 编译失败

```bash
# 检查依赖
dpkg -l | grep -E 'libpcre|libssl|zlib'

# 清理重新编译
make clean
./auto/configure [options]
make
```

### 问题 3: nginx 启动失败

```bash
# 检查配置语法
sudo /usr/local/nginx-rtt/sbin/nginx -t

# 查看详细错误
sudo /usr/local/nginx-rtt/sbin/nginx -c /path/to/nginx.conf
```

---

## 📚 下一步 (Next Steps)

1. **详细文档**: 阅读 `RTT_MODULE_README.md` 了解完整功能
2. **测试指南**: 参考 `TEST_GUIDE.md` 进行全面测试
3. **生产部署**: 查看 `IMPLEMENTATION_SUMMARY.md` 了解最佳实践
4. **性能调优**: 根据实际负载调整配置参数

---

## 🎓 常见用例 (Common Use Cases)

### 用例 1: API 延迟监控

```nginx
location /api/ {
    access_log /var/log/nginx/api.log main;
    
    # 如果 TCP RTT 过高，记录警告
    if ($tcp_rtt > 1000) {
        access_log /var/log/nginx/high_latency.log;
    }
    
    proxy_pass http://api_backend;
}
```

### 用例 2: 地理位置感知路由

```nginx
# 基于 RTT 的简单路由（需要自定义逻辑）
map $tcp_rtt $backend_pool {
    ~^[0-9]|[1-4][0-9]{2}$  "fast_backend";   # 0-499 微秒
    default                  "slow_backend";   # >= 500 微秒
}

upstream fast_backend {
    server fast1.example.com;
}

upstream slow_backend {
    server slow1.example.com;
}
```

### 用例 3: TLS 性能分析

```nginx
server {
    listen 443 ssl;
    
    # 记录所有 TLS 连接的握手时间
    if ($tls_handshake_rtt > 5000) {
        access_log /var/log/nginx/slow_tls.log;
    }
}
```

---

## ✅ 验证清单 (Verification Checklist)

完成安装和配置后，请验证以下内容：

- [ ] Nginx 编译成功，包含 RTT 模块
- [ ] HTTP 服务器可以输出 `$tcp_rtt` 变量
- [ ] HTTPS 服务器可以输出 `$tls_handshake_rtt` 变量
- [ ] Stream 服务器可以记录 TCP RTT 到日志
- [ ] RTT 值在合理范围内（本地 10-200μs，远程更高）
- [ ] 日志格式正确包含 RTT 信息
- [ ] 响应头（如果配置）正确显示 RTT

---

## 🆘 获取帮助 (Getting Help)

如果遇到问题：

1. **查看文档**:
   - `RTT_MODULE_README.md` - 完整技术文档
   - `TEST_GUIDE.md` - 详细测试指南
   - `IMPLEMENTATION_SUMMARY.md` - 实现细节

2. **检查日志**:
   ```bash
   sudo tail -f /var/log/nginx/error.log
   ```

3. **启用调试**:
   ```nginx
   error_log /var/log/nginx/error.log debug;
   ```

4. **提交 Issue**: 
   - GitHub Issues: [项目地址]
   - 包含：nginx 版本、OS 版本、配置文件、错误日志

---

## 🚀 开始使用！

现在你已经准备好使用 Nginx RTT 模块了！

```bash
# 启动 nginx
sudo /usr/local/nginx-rtt/sbin/nginx

# 测试
curl http://localhost:8080/

# 查看日志
tail -f /var/log/nginx/access.log
```

祝你使用愉快！Happy coding! 🎉
