# TinyWebServer

一个基于 C++11 的高性能 HTTP Web 服务器，采用 Reactor 事件驱动模型，支持高并发连接、HTTP 请求解析、静态资源服务、日志记录等功能。

## 技术栈

- **C++11** - 现代 C++ 特性（线程、智能指针、lambda 等）
- **Epoll** - Linux 高性能 I/O 多路复用
- **线程池** - 基于 C++11 thread/condition_variable 实现
- **HTTP/1.1** - 支持 GET/POST 请求解析
- **定时器** - 基于升序链表的超时连接管理

## 功能特性

- ✅ **高并发处理**：Epoll ET 边缘触发 + 线程池，支持数万并发连接
- ✅ **HTTP 请求解析**：完整解析请求行、请求头、请求体
- ✅ **静态资源服务**：支持 HTML/CSS/JS/图片等静态文件访问
- ✅ **动态路由**：支持自定义路由处理函数
- ✅ **日志系统**：分级日志（DEBUG/INFO/WARN/ERROR），支持文件/控制台输出
- ✅ **连接超时管理**：定时器自动清理不活跃连接，防止资源泄漏
- ✅ **配置化**：YAML 配置文件，无需重新编译即可调整参数

## 项目结构

```
TinyWebServer/
├── include/          # 头文件
│   ├── http_conn.h   # HTTP 连接类
│   ├── threadpool.h  # 线程池模板类
│   ├── locker.h      # 互斥锁/信号量封装
│   ├── timer.h       # 定时器管理
│   ├── logger.h      # 日志系统
│   └── router.h      # 路由分发
├── src/              # 源代码
│   ├── main.cpp      # 主程序入口
│   ├── http_conn.cpp # HTTP 连接处理
│   ├── config.cpp    # 配置解析
│   ├── router.cpp    # 路由实现
│   ├── logger.cpp    # 日志实现
│   └── timer.cpp     # 定时器实现
├── root/             # 静态资源目录
│   └── index.html    # 默认首页
├── Makefile          # 编译脚本
├── server.yaml       # 服务器配置文件
└── README.md         # 项目说明
```

## 快速开始

### 编译

```bash
make
```

### 运行

```bash
./server
```

服务器默认监听 **8080** 端口，浏览器访问 `http://localhost:8080` 即可。

### 配置

编辑 `server.yaml` 调整参数：

```yaml
port: 8080              # 监听端口
threads: 8              # 工作线程数
connection_timeout: 60000  # 连接超时（毫秒）
log_level: 1            # 日志级别：0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
console_log: true      # 是否输出到控制台
```

## 核心设计

### 1. Reactor 事件驱动模型

```
主线程（Reactor）
    ├── Epoll 监听事件
    ├── 新连接 → 加入 Epoll
    ├── 可读事件 → 加入线程池任务队列
    └── 可写事件 → 直接发送响应

工作线程池
    ├── 从任务队列取连接
    ├── 解析 HTTP 请求
    └── 生成响应数据
```

### 2. HTTP 连接状态机

```
CHECK_STATE_REQUESTLINE ──→ CHECK_STATE_HEADER ──→ CHECK_STATE_CONTENT
        ↑                                              │
        └──────── 解析完成，生成响应 ←─────────────────┘
```

### 3. 定时器管理

采用**升序链表**管理连接超时：
- 新连接加入链表尾部
- 超时连接自动关闭，释放资源
- 主线程每次循环检查超时节点

## 性能测试

使用 WebBench 进行压力测试：

```bash
# 1000 并发，持续 60 秒
webbench -c 1000 -t 60 http://localhost:8080/
```

**测试结果**（参考）：
- 并发 1000：QPS ≈ 8000+
- 并发 5000：QPS ≈ 12000+
- 内存占用：≈ 50MB（10000 连接）

## 待优化项

- [ ] 支持 HTTPS（OpenSSL）
- [ ] 数据库连接池（MySQL/Redis）
- [ ] 负载均衡（多进程模型）
- [ ] 支持 HTTP/2

## 参考

- 《Linux 高性能服务器编程》
- 《Unix 网络编程》
- [TinyHTTPd](https://github.com/EZLippi/TinyHTTPd)

## 作者

- GitHub: [@xiaofangdai3-ship-it](https://github.com/xiaofangdai3-ship-it)
- Email: xiaofangdai3@gmail.com

## License

MIT License
