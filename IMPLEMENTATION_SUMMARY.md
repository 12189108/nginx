# Nginx 1.29 PROXY Protocol v2 RTT Implementation Summary

## 概述 (Overview)

本实现为 Nginx 1.29 添加了完整的 PROXY Protocol v2 读写支持，以及 TCP RTT 和 TLS RTT 测量功能。

This implementation adds complete PROXY Protocol v2 read/write support to Nginx 1.29, along with TCP RTT and TLS RTT measurement capabilities.

## 主要功能 (Key Features)

### 1. PROXY Protocol v2 增强 (PROXY Protocol v2 Enhancements)

#### 核心修改 (Core Modifications)

- **文件**: `src/core/ngx_proxy_protocol.h`, `src/core/ngx_proxy_protocol.c`
- **功能**:
  - 增加了 `ngx_proxy_protocol_v2_write()` 函数，用于生成 PROXY Protocol v2 格式的头部
  - 扩展了 `ngx_proxy_protocol_t` 结构体，添加 `tcp_rtt` 和 `tls_rtt` 字段
  - 实现了自定义 TLV 类型的读取和写入：
    - `0xE0`: TCP RTT (微秒)
    - `0xE1`: TLS RTT (微秒)
  - 在 `ngx_proxy_protocol_v2_read()` 中添加了 TLV 解析逻辑

#### 实现细节 (Implementation Details)

```c
// 自定义 TLV 类型定义
#define NGX_PROXY_PROTOCOL_TLV_TCP_RTT    0xE0
#define NGX_PROXY_PROTOCOL_TLV_TLS_RTT    0xE1

// 扩展的数据结构
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

### 2. Stream 模块 - TCP RTT 测量 (Stream Module - TCP RTT Measurement)

#### 新增模块 (New Module)

- **文件**: `src/stream/ngx_stream_rtt_module.c`
- **功能**:
  - 实现了 `$tcp_rtt` 变量，可在配置中使用
  - 通过系统调用获取 TCP RTT：
    - Linux: `getsockopt(TCP_INFO)`，读取 `tcpi_rtt`
    - macOS: `getsockopt(TCP_CONNECTION_INFO)`，读取 `tcpi_rttcur`
  - 在 PREREAD 阶段自动测量和记录 TCP RTT
  - 支持通过 PROXY Protocol v2 传递 RTT 值

#### 使用示例 (Usage Example)

```nginx
stream {
    server {
        listen 8000 proxy_protocol;
        
        # 使用 tcp_rtt 变量记录日志
        log_format rtt '$remote_addr - tcp_rtt=$tcp_rtt';
        access_log /var/log/nginx/stream.log rtt;
        
        proxy_pass backend;
        proxy_protocol on;  # 转发时包含 RTT 信息
    }
}
```

### 3. HTTP 模块 - TCP 和 TLS RTT 测量 (HTTP Module - TCP and TLS RTT Measurement)

#### 新增模块 (New Module)

- **文件**: `src/http/modules/ngx_http_rtt_module.c`
- **功能**:
  - 实现了两个变量：
    - `$tcp_rtt`: TCP 往返时间（微秒）
    - `$tls_handshake_rtt`: TLS 握手时间（毫秒）
  - TCP RTT 测量：
    - 优先使用 PROXY Protocol v2 传递的值
    - 其次通过 socket 选项直接测量
  - TLS RTT 测量：
    - 记录 SSL/TLS 握手开始和结束时间
    - 计算握手耗时
  - 在 REWRITE 阶段执行测量

#### 使用示例 (Usage Example)

```nginx
http {
    log_format rtt_log '$remote_addr - tcp_rtt=$tcp_rtt tls_rtt=$tls_handshake_rtt';
    
    server {
        listen 443 ssl proxy_protocol;
        
        ssl_certificate /path/to/cert.pem;
        ssl_certificate_key /path/to/key.pem;
        
        access_log /var/log/nginx/access.log rtt_log;
        
        location / {
            # 在响应头中包含 RTT 信息
            add_header X-TCP-RTT $tcp_rtt always;
            add_header X-TLS-RTT $tls_handshake_rtt always;
            
            proxy_pass http://backend;
            proxy_protocol on;  # 转发 PROXY Protocol v2 + RTT
        }
    }
}
```

## 构建配置 (Build Configuration)

### 修改的文件 (Modified Files)

- **`auto/modules`**: 添加了模块的自动配置逻辑
  - 在 HTTP 模块部分添加 `ngx_http_rtt_module`
  - 在 STREAM 模块部分添加 `ngx_stream_rtt_module`

### 编译命令 (Build Commands)

```bash
# 配置
./auto/configure \
    --with-stream \
    --with-stream_ssl_module \
    --with-http_ssl_module

# 编译
make

# 安装
sudo make install
```

## 技术实现 (Technical Implementation)

### TCP RTT 测量原理 (TCP RTT Measurement)

1. **Linux 平台**:
   ```c
   struct tcp_info ti;
   socklen_t len = sizeof(struct tcp_info);
   getsockopt(fd, IPPROTO_TCP, TCP_INFO, &ti, &len);
   rtt = ti.tcpi_rtt;  // 微秒
   ```

2. **macOS 平台**:
   ```c
   struct tcp_connection_info ti;
   socklen_t len = sizeof(struct tcp_connection_info);
   getsockopt(fd, IPPROTO_TCP, TCP_CONNECTION_INFO, &ti, &len);
   rtt = ti.tcpi_rttcur;  // 微秒
   ```

### TLS RTT 测量原理 (TLS RTT Measurement)

```c
// 握手开始
ctx->ssl_handshake_start = ngx_current_msec;

// ... SSL/TLS 握手 ...

// 握手结束
ctx->ssl_handshake_end = ngx_current_msec;

// 计算耗时（毫秒转微秒）
tls_rtt = (ctx->ssl_handshake_end - ctx->ssl_handshake_start) * 1000;
```

### PROXY Protocol v2 TLV 编码 (TLV Encoding)

```
┌──────────┬──────────┬──────────────────────┐
│ Type (1) │ Len (2)  │ Value (4)            │
├──────────┼──────────┼──────────────────────┤
│   0xE0   │  0x0004  │ TCP RTT (uint32_be)  │
│   0xE1   │  0x0004  │ TLS RTT (uint32_be)  │
└──────────┴──────────┴──────────────────────┘
```

示例：TCP RTT = 1000 微秒
```
E0 00 04 00 00 03 E8
│  │  │  └─────┬─────┘
│  │  │        └─ 值: 1000 (大端序)
│  │  └─ 长度: 4 字节
│  └─ TLV 长度字段
└─ 类型: TCP RTT
```

## 平台支持 (Platform Support)

| 平台 (Platform) | TCP RTT | TLS RTT | 备注 (Notes) |
|----------------|---------|---------|-------------|
| Linux 2.6+     | ✅      | ✅      | 完全支持 (Full support) |
| macOS 10.10+   | ✅      | ✅      | 完全支持 (Full support) |
| FreeBSD        | ❌      | ✅      | 仅 TLS RTT (TLS RTT only) |
| Windows        | ❌      | ✅      | 仅 TLS RTT (TLS RTT only) |

## 文件清单 (File List)

### 新增文件 (New Files)

1. `src/stream/ngx_stream_rtt_module.c` - Stream RTT 模块
2. `src/http/modules/ngx_http_rtt_module.c` - HTTP RTT 模块
3. `src/stream/ngx_stream_rtt_module_config` - Stream 模块配置
4. `src/http/modules/ngx_http_rtt_module_config` - HTTP 模块配置
5. `conf/rtt_example.conf` - 示例配置文件
6. `RTT_MODULE_README.md` - 详细文档
7. `0001-Add-PROXY-Protocol-v2-RTT-support-and-TCP-TLS-RTT-mo.patch` - Git patch 文件

### 修改文件 (Modified Files)

1. `src/core/ngx_proxy_protocol.h` - 添加 RTT 字段和函数声明
2. `src/core/ngx_proxy_protocol.c` - 实现 v2 写入和 TLV 解析
3. `auto/modules` - 添加模块构建配置

## Patch 应用 (Applying the Patch)

```bash
# 进入 nginx 源码目录
cd /path/to/nginx-1.29.4

# 应用 patch
git apply 0001-Add-PROXY-Protocol-v2-RTT-support-and-TCP-TLS-RTT-mo.patch

# 或使用
patch -p1 < 0001-Add-PROXY-Protocol-v2-RTT-support-and-TCP-TLS-RTT-mo.patch
```

## 性能影响 (Performance Impact)

- **TCP RTT 测量**: 几乎无开销（读取内核缓存的数据）
- **TLS RTT 测量**: 可忽略的开销（简单的时间戳相减）
- **PROXY Protocol v2 开销**: +14 字节（每个 TLV 7 字节 × 2）

## 测试建议 (Testing Recommendations)

### 1. 基本功能测试

```bash
# 测试 stream 模块
echo "test" | nc localhost 8000

# 检查日志中的 tcp_rtt 值
tail /var/log/nginx/stream.log
```

### 2. HTTPS 测试

```bash
# 测试 HTTPS 连接
curl -k https://localhost:443/

# 检查响应头
curl -k -I https://localhost:443/ | grep X-.*-RTT
```

### 3. PROXY Protocol v2 测试

使用支持 PROXY Protocol v2 的工具（如 HAProxy）测试 TLV 传递。

## 已知限制 (Known Limitations)

1. TLS RTT 测量需要启用 `NGX_HTTP_SSL`
2. TCP RTT 在部分平台不可用（FreeBSD、Windows）
3. RTT 值在连接刚建立时可能为 0
4. 不支持 UNIX domain socket 的 RTT 测量

## 未来改进方向 (Future Improvements)

1. 添加更多统计指标（重传次数、拥塞窗口等）
2. 支持更多平台的 TCP RTT 测量
3. 添加 RTT 历史记录和分析功能
4. 支持自定义 TLV 类型
5. 添加更详细的调试日志

## 参考文档 (References)

- [PROXY Protocol v2 Specification](https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt)
- [Linux TCP_INFO](https://man7.org/linux/man-pages/man7/tcp.7.html)
- [Nginx Module Development](http://nginx.org/en/docs/dev/development_guide.html)

## 联系方式 (Contact)

如有问题或建议，请提交 Issue 或 Pull Request。

For questions or suggestions, please submit an Issue or Pull Request.
