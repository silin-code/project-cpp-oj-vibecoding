# OJ System — 仿 LeetCode 在线判题系统

基于 C++ 构建的轻量级在线判题系统，支持用户注册、题目浏览、C++ 代码提交与异步判题，适用于学习与面试准备。

## 功能

| 模块 | 说明 |
|------|------|
| 用户系统 | 注册 / 登录 / 登出，Session-Cookie 认证，密码 bcrypt 哈希 |
| 题目列表 | 展示所有题目（标题、难度、通过率），分页排序 |
| 题目详情 | 题目描述、输入/输出格式、示例测试用例、提交历史 |
| 代码提交 | CodeMirror 5 编辑器（C++ 语法高亮、行号、括号匹配），异步提交 |
| 判题引擎 | 支持 AC / WA / CE / RE / TLE / MLE，失败时展示用例编号及输入/期望输出 |
| 管理后台 | 管理员可增删改题目、管理测试用例（示例 / 隐藏） |
| 安全沙箱 | seccomp-bpf 系统调用过滤、rlimit 资源限制（CPU / 内存 / 输出 / 进程数） |
| 限流 & 校验 | 同用户 10s 内最多提交 1 次，单次代码上限 64KB |

## 技术栈

| 层 | 技术 |
|----|------|
| 后端 | C++17, [cpp-httplib](https://github.com/yhirose/cpp-httplib), MySQL, libseccomp |
| 前端 | 原生 HTML + CSS + JavaScript, CodeMirror 5 (CDN) |
| 构建 | CMake, g++ |
| 部署 | systemd, /var/oj/sessions 文件存储 Session |

## 架构

```
Browser (HTML + JS + CodeMirror)
     │  REST JSON
     ▼
┌─ cpp-httplib Server ─────────────────────────────┐
│  Router → Handler → Service → Model → DB Pool    │
│                                                    │
│  ExecutorService (异步判题队列)                      │
│  ├─ fork() → seccomp → rlimit → exec              │
│  ├─ g++ 编译用户代码                                 │
│  └─ 逐用例运行 → 比对输出                             │
└──────────────────────┬───────────────────────────┘
                       │
                  ┌────┴────┐
                  │  MySQL   │
                  └─────────┘
```

## 快速开始

### 环境要求

- Ubuntu 20.04+ / Debian 11+
- CMake 3.16+, g++ 11+, MySQL 8.0+
- libmysqlclient-dev, libseccomp-dev, libssl-dev

### 1. 安装依赖

```bash
sudo apt update
sudo apt install -y cmake g++ libmysqlclient-dev libseccomp-dev libssl-dev mysql-server
```

### 2. 初始化数据库

```bash
sudo mysql < database/init.sql
```

预设管理员账号：`root` / `******`（见 `database/init.sql`）

### 3. 编译 & 运行

```bash
bash scripts/start.sh
```

默认监听 `0.0.0.0:8080`，打开浏览器访问 `http://localhost:8080`。

### 4. systemd 部署（可选）

```bash
sudo cp scripts/oj-backend.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now oj-backend
```

## 配置

`config/config.json`：

```json
{
    "server":        { "port": 8080, "static_dir": "public" },
    "database":      { "host": "localhost", "port": 3306, "pool_size": 10 },
    "session":       { "dir": "/var/oj/sessions", "cleanup_interval_min": 30 },
    "judge": {
        "worker_threads": 2,
        "max_queue": 5,
        "time_limit_default": 2,
        "memory_limit_default": 256,
        "compile_timeout": 10
    },
    "rate_limit":    { "submit_window_sec": 10, "max_code_size": 65536 }
}
```

## API 概览

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/register` | 注册 |
| POST | `/api/login` | 登录 |
| POST | `/api/logout` | 登出 |
| GET  | `/api/me` | 当前用户信息 |
| GET  | `/api/problems` | 题目列表 |
| GET  | `/api/problems/{id}` | 题目详情（含示例用例） |
| POST | `/api/problems` | 新增题目（管理员） |
| PUT  | `/api/problems/{id}` | 编辑题目（管理员） |
| DELETE | `/api/problems/{id}` | 删除题目（管理员） |
| POST | `/api/submit` | 提交代码 → 202 + submission_id |
| GET  | `/api/submissions/{id}` | 查询判题结果 |
| GET  | `/api/submissions?problem_id=X` | 提交历史 |
| GET  | `/api/problems/{id}/testcases` | 测试用例列表（管理员） |
| POST | `/api/problems/{id}/testcases` | 新增测试用例（管理员） |
| DELETE | `/api/problems/{id}/testcases/{tc_id}` | 删除测试用例（管理员） |

## 项目结构

```
cpp-oj-vibecoding/
├── CMakeLists.txt
├── README.md
├── SPEC.md
├── config/
│   └── config.json
├── database/
│   └── init.sql
├── third_party/
│   ├── httplib.h              # cpp-httplib (header-only)
│   └── json.hpp               # nlohmann-json (header-only)
├── src/
│   ├── main.cc
│   ├── server/
│   │   ├── server.cc
│   │   └── router.h
│   ├── handler/
│   │   ├── auth_handler.cc     # 注册/登录/登出/me
│   │   ├── problem_handler.cc  # 题目 CRUD
│   │   ├── submit_handler.cc   # 代码提交 & 结果查询
│   │   └── admin_handler.cc    # 测试用例管理
│   ├── service/
│   │   ├── auth_service.cc     # 密码哈希/Session/限流
│   │   ├── problem_service.cc  # 题目业务逻辑
│   │   └── executor_service.cc # 判题队列+沙箱
│   ├── model/
│   │   ├── problem.h
│   │   ├── test_case.h
│   │   └── user.h
│   ├── db/
│   │   └── connection_pool.cc  # MySQL 连接池
│   └── utils/
│       ├── config.cc           # JSON 配置加载
│       └── logger.cc           # 日志工具
├── public/
│   ├── index.html              # 题目列表
│   ├── login.html              # 登录
│   ├── register.html           # 注册
│   ├── problem.html            # 题目详情 + 编辑器 + 结果
│   ├── admin.html              # 管理后台
│   ├── css/style.css           # 暗色主题
│   └── js/
│       ├── api.js              # fetch 封装
│       ├── auth.js             # 登录状态检查
│       ├── problem.js          # 题目列表渲染
│       ├── problem_detail.js   # 题目详情加载
│       └── submit.js           # 提交 & 轮询逻辑
├── scripts/
│   ├── start.sh
│   └── oj-backend.service
└── tests/
    └── integration/
        ├── auth_api_test.cc
        ├── problem_api_test.cc
        ├── admin_api_test.cc
        └── submit_api_test.cc
```

## 判题状态

| 状态 | 含义 |
|------|------|
| `AC` (Accepted) | 通过所有测试用例 |
| `WA` (Wrong Answer) | 输出与期望不符 |
| `CE` (Compilation Error) | 编译失败 |
| `RE` (Runtime Error) | 运行时错误（段错误、除零等） |
| `TLE` (Time Limit Exceeded) | 运行超时 |
| `MLE` (Memory Limit Exceeded) | 内存超限 |

状态流转：`PENDING → JUDGING → AC / WA / CE / RE / TLE / MLE`

## 默认限制

| 项目 | 值 |
|------|----|
| 时间限制 | 每题可配置，默认 2s |
| 内存限制 | 每题可配置，默认 256MB |
| 代码长度 | 最大 64KB |
| 提交频率 | 每 10s 每次 |
| 判题队列 | 最大 5 个等待任务 |
| 并发 Worker | 2 个判题线程 |
