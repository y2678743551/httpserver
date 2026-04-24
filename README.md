
```markdown
# 🚀 C++  HTTP + WebSocket 服务器

一个从零实现的、基于 epoll 的非阻塞 HTTP/1.1 和 WebSocket 服务器，支持静态页面、REST API、实时聊天消息广播，并经过 5 万并发连接压测。

## 📚 技术栈

- **C++17**：现代 C++ 特性（智能指针、lambda、STL）
- **epoll + 边缘触发(ET) + 非阻塞 socket**：高性能事件驱动模型
- **HTTP/1.1**：支持 Keep-Alive、静态文件、JSON API
- **WebSocket (RFC 6455)**：握手、文本帧、Ping/Pong、关闭帧
- **MySQL C API**：用户登录验证（可选，可自行扩展）
- **OpenSSL**：SHA1 + Base64 计算 WebSocket Accept key
- **nlohmann/json**：JSON 解析与序列化
- **前端**：原生 HTML/CSS/JS，单页应用(SPA) + localStorage 持久化

## ✨ 主要特性

- ✅ 完全手写 epoll 事件驱动，支持 **5 万并发连接**，QPS ~4400
- ✅ HTTP 请求解析器（支持 GET/POST、Header、Body）
- ✅ 非阻塞写缓冲区 + EPOLLOUT 管理，避免数据发送阻塞
- ✅ WebSocket 协议完整实现：
  - 握手升级（101 Switching Protocols）
  - 文本帧收发、分片自动组装
  - Ping/Pong 心跳保活
  - 关闭帧礼貌关闭
- ✅ 静态文件服务（HTML/CSS）
- ✅ REST API 示例：`/api/submit` 登录接口（JSON 交互）
- ✅ 前端单页应用：
  - 登录页面与聊天室页面无刷新切换
  - localStorage 保存登录状态，页面刷新自动重连 WebSocket
  - 实时消息展示
- ✅ 优雅退出（信号捕获 + 资源释放）
- ✅ 高并发压测报告（wrk 工具）

## 🔧 编译与运行

### 依赖安装（Ubuntu 22.04）

```bash
sudo apt update
sudo apt install g++ make libmysqlclient-dev libssl-dev libboost-all-dev
# nlohmann/json 是 header-only 库，可手动下载或通过包管理器安装
sudo apt install nlohmann-json3-dev
```

编译

```bash
make
```

运行

```bash
./serve
```

默认监听 127.0.0.1:8080。在浏览器中访问 http://127.0.0.1:8080 即可打开聊天室登录页。

压测

```bash
wrk -t 4 -c 10000 -d 30s http://127.0.0.1:8080/
```

📁 项目结构

```
.
├── Makefile
├── .gitignore
├── main.cpp                     # 入口，epoll 事件循环
├── http_parser.hpp              # HTTP 解析器        
├── websocket_parser.hpp         # WebSocket 解析器
├── epoll_manager.hpp            # epoll 封装
├── sql_util.hpp                 # 将来封装成SQL工具
├── file_util.hpp                # 读取静态文件
├── marco_chechker.hpp           # 封装系统调用并检查错误，简化错误处理。
├── addr_util.hpp                # 套接字地址存储结构（兼容 sockaddr/sockaddr_storage）
├── index.html                   # 前端页面
└── style.css                    # 样式
```

🔌 API 接口

1. 登录（HTTP POST /api/submit）

请求体：

```json
{
    "username": "aa",
    "password": "111"
}
```

成功响应：

```json
{
    "status": "OK"
}
```

前端收到 status=OK 后会保存用户名到 localStorage，并切换到聊天室页面，建立 WebSocket 连接。

2. WebSocket 通信（ws://host:port/chat）

连接建立后，客户端应立即发送登录认证消息：

```json
{
    "type": "chat",
    "user": "aa"
}
```

服务器收到后会将该连接与用户绑定。之后聊天消息格式：

客户端发送：

```json
{
    "type": "chat",
    "content": "Hello!",
    "user": "aa"
}
```

服务器广播（发送给所有在线用户）：

```json
{
    "type": "broadcast",
    "from": "aa",
    "content": "Hello!",
    "time": "2025-04-24T12:00:00Z"
}
```

其他帧处理：

· Ping → 自动回复 Pong
· Close → 回复 Close 并关闭连接

📊 性能测试

使用 wrk 压测（单核 CPU，2GB 内存虚拟机）：

```
wrk -t 4 -c 50000 -d 30s --latency http://127.0.0.1:8080/
```

结果：

· 总请求数：807574
· QPS：26835.68
· 平均延迟：1.03s
· 99% 延迟：1.18s
· 无连接错误

## 压测结果

![wrk压测结果](/docs/wrk.png)

📝 许可证

MIT License

🙏 致谢

· nlohmann/json
· OpenSSL
· RFC 6455