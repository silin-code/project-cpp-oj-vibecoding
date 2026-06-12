# OJ 系统规格说明书 (SPEC)

> 最后更新: 2026-06-12（重构项目结构，采用 handler/service/model 分层）

---

## 1. 业务目标与成功标准

| 维度 | 规格 |
|------|------|
| 项目定位 | 仿 LeetCode 的个人在线判题系统，用于学习/面试准备 |
| 目标用户 | 自己 + 少量朋友（1–20 人同时在线） |
| 成功标准 | 用户可注册登录、浏览题目、提交 C++ 代码、实时获得判题结果；管理员可增删题目及测试用例 |
| 部署方式 | 单机部署于云端服务器（systemd 管理进程） |

---

## 2. 功能范围 (MVP)

### 2.1 角色与权限

| 角色 | 权限 |
|------|------|
| **普通用户** | 注册、登录、浏览题目列表、查看题目详情、提交代码、查看提交结果 |
| **管理员** | 普通用户权限 + 题目管理后台（CRUD 题目 + 管理测试用例） |

### 2.2 功能清单

| # | 功能 | 说明 |
|---|------|------|
| F1 | 用户注册/登录 | 用户名 + 密码，无邮箱验证；Session-Cookie 认证 |
| F2 | 题目列表 | 展示所有题目（标题、难度标签、通过率） |
| F3 | 题目详情 | 题目描述、输入/输出格式、示例用例（展示给用户）、代码编辑器 + 提交按钮 |
| F4 | 代码提交 & 判题 | 异步判题，前端轮询获取结果 |
| F5 | 提交结果展示 | 显示编译错误(CE) / 通过(AC) / 错误(WA) / 运行时错误(RE) / 超时(TLE) / 超内存(MLE)；WA 时告知第一个未通过的测试用例编号 |
| F6 | 提交历史 | 用户可查看自己某道题的所有历史提交记录 |
| F7 | 管理后台 | 仅管理员可访问：新增/编辑/删除题目，添加/删除/查看测试用例（分为"示例用例"和"隐藏用例"） |
| F8 | 硬编码 root 账号 | 首次部署时预设一个管理员账号 |

### 2.3 非功能需求

| 维度 | 规格 |
|------|------|
| 响应时间 | API 响应 < 200ms（不含判题时间）；判题结果展示 < 2s（从提交到结果回显） |
| 并发 | 支持 20 人同时在线，5 个判题任务并发 |
| 安全 | 进程级沙箱隔离（seccomp + unshare + rlimit），限制 CPU/内存/时间/系统调用，禁止网络、禁止读文件系统 |
| 存储 | 题目和测试用例存 MySQL，Session 存本地磁盘文件，提交记录存 MySQL |
| 限流 | 同一用户 10 秒内最多提交 1 次；单次提交代码最大 64KB |
| 可维护性 | systemd 管理进程，编译后直接运行；MySQL 依赖系统安装 |

---

## 3. 技术架构

### 3.1 总体架构图

```
┌──────────────────────────────────────────────────────────┐
│                   Browser (原生 HTML+CSS+JS)               │
│            CodeMirror 6 (CDN) 代码编辑器                    │
│                  fetch() REST 调用                          │
└──────────────────────┬───────────────────────────────────┘
                       │ HTTP (REST JSON)
                       ▼
┌──────────────────────────────────────────────────────────┐
│                  C++ Backend (cpp-httplib)                │
│                                                          │
│  ┌──────────┐  ┌────────────────┐  ┌─────────────────┐  │
│  │  Router  │→ │    Handler     │→ │    Service      │  │
│  │ (路由)   │  │ (请求处理)     │  │ (业务逻辑)      │  │
│  │          │  │ auth_handler   │  │ auth_service    │  │
│  │          │  │ problem_handler │  │ problem_service │  │
│  │          │  │ submit_handler │  │ executor_service│  │
│  │          │  │ admin_handler  │  │                 │  │
│  └──────────┘  └────────────────┘  └───────┬─────────┘  │
│                                            │            │
│                      ┌─────────────────────┴──────┐     │
│                      │         Model               │     │
│                      │  problem / test_case / user  │     │
│                      └─────────────────────┬──────┘     │
│                                            │            │
│  ┌─────────────────────────────────────────┴────────┐  │
│  │     DB (Connection Pool)                         │  │
│  └─────────────────────────────────────────┬────────┘  │
│                                            │            │
│  ┌─────────────────────────────────────────┴────────┐  │
│  │      Executor (沙箱判题)                          │  │
│  │  fork / seccomp / unshare / rlimit / chroot       │  │
│  └──────────────────────────────────────────────────┘  │
└──────────────────────┬──────────────────────────────────┘
                       │
          ┌────────────┴────────────┐
          │     MySQL (持久化)       │
          │  题目 / 测试用例 / 提交   │
          └─────────────────────────┘
```

### 3.2 数据流 - 判题流程

```
用户提交代码
    │
    ▼
POST /api/submit ──→ 返回 submission_id (HTTP 202)
    │
    ▼
后端将任务放入内存队列
    │
    ▼
Worker 取出任务:
  1. 将代码写入临时文件
  2. fork 子进程, 应用 seccomp / unshare / rlimit
  3. 编译 (g++)
  4. 逐组运行测试用例 (stdin → stdout)
  5. 比对期望输出
  6. 记录结果至 MySQL
    │
    ▼
前端轮询 GET /api/submissions/{id}
    │
    ▼
状态机: PENDING → JUDGING → AC / WA / CE / RE / TLE / MLE
```

---

## 4. API 设计

### 4.1 认证

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/register` | 注册（username, password） |
| POST | `/api/login` | 登录，设置 Cookie |
| POST | `/api/logout` | 登出 |
| GET  | `/api/me` | 获取当前用户信息 |

### 4.2 题目

| 方法 | 路径 | 说明 | 权限 |
|------|------|------|------|
| GET  | `/api/problems` | 题目列表（含通过率） | 任意 |
| GET  | `/api/problems/{id}` | 题目详情（含示例用例） | 任意 |
| POST | `/api/problems` | 新增题目 | 管理员 |
| PUT  | `/api/problems/{id}` | 编辑题目 | 管理员 |
| DELETE | `/api/problems/{id}` | 删除题目 | 管理员 |
| GET  | `/api/problems/{id}/testcases` | 获取测试用例列表 | 管理员 |
| POST | `/api/problems/{id}/testcases` | 新增测试用例 | 管理员 |
| DELETE | `/api/problems/{id}/testcases/{tc_id}` | 删除测试用例 | 管理员 |

### 4.3 提交

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/submit` | 提交代码（body: problem_id, code）→ 202 + submission_id |
| GET  | `/api/submissions/{id}` | 查询提交结果 |
| GET  | `/api/submissions?problem_id=X` | 查询用户某题的所有提交 |

### 4.4 公共常量

| 常量 | 值 |
|------|----|
| 时间限制（默认） | 2s |
| 内存限制（默认） | 256 MB |
| 提交限流窗口 | 10s |
| 单次代码最大长度 | 64 KB |

---

## 5. 数据库设计 (MySQL)

### 5.1 表结构

```sql
CREATE TABLE users (
    id          INT PRIMARY KEY AUTO_INCREMENT,
    username    VARCHAR(64) UNIQUE NOT NULL,
    password    VARCHAR(256) NOT NULL,   -- bcrypt hash
    role        ENUM('user','admin') DEFAULT 'user',
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE problems (
    id          INT PRIMARY KEY AUTO_INCREMENT,
    title       VARCHAR(256) NOT NULL,
    description TEXT NOT NULL,
    input_desc  TEXT,
    output_desc TEXT,
    difficulty  ENUM('easy','medium','hard') DEFAULT 'easy',
    time_limit  INT DEFAULT 2,          -- 秒
    memory_limit INT DEFAULT 256,       -- MB
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

CREATE TABLE testcases (
    id          INT PRIMARY KEY AUTO_INCREMENT,
    problem_id  INT NOT NULL,
    input       TEXT NOT NULL,
    expected    TEXT NOT NULL,
    is_sample   BOOLEAN DEFAULT FALSE,   -- TRUE=示例用例, FALSE=隐藏用例
    sort_order  INT DEFAULT 0,
    FOREIGN KEY (problem_id) REFERENCES problems(id) ON DELETE CASCADE
);

CREATE TABLE submissions (
    id          INT PRIMARY KEY AUTO_INCREMENT,
    user_id     INT NOT NULL,
    problem_id  INT NOT NULL,
    code        TEXT NOT NULL,
    status      ENUM('PENDING','JUDGING','AC','WA','CE','RE','TLE','MLE') DEFAULT 'PENDING',
    failed_case INT DEFAULT NULL,        -- 第一个未通过的测试用例编号 (1-based)
    error_msg   TEXT,                    -- 编译错误详情
    time_used   INT DEFAULT NULL,        -- ms
    memory_used INT DEFAULT NULL,        -- KB
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id),
    FOREIGN KEY (problem_id) REFERENCES problems(id)
);
```

### 5.2 Session 存储

- 格式：文件 `/var/oj/sessions/{session_id}.json`
- 内容：`{ "user_id": int, "username": str, "role": str, "expires_at": timestamp }`
- 清理策略：启动时 + 每 30 分钟扫描，删除过期文件；如果总文件大小超过阈值（如 100MB），全量清理

---

## 6. 前端页面结构

| 页面 | 文件 | 路由(前端) | 说明 |
|------|------|-----------|------|
| 登录 | `login.html` | `/login` | 登录表单 |
| 注册 | `register.html` | `/register` | 注册表单 |
| 题目列表 | `index.html` | `/` | 题目表格 + 通过率 + 提交历史入口 |
| 题目详情 | `problem.html` | `/problem?id=X` | 题目描述 + 示例 + CodeMirror 编辑器 + 提交按钮 + 结果展示 |
| 管理后台 | `admin.html` | `/admin` | 题目 CRUD + 测试用例管理 |

前端为多页应用（MPA），每个页面独立 HTML，通过 JS 调用 REST API。

---

## 7. 判题沙箱安全策略

| 措施 | 实现方式 |
|------|---------|
| 系统调用过滤 | `seccomp-bpf` 白名单模式（仅允许 read/write/writev/exit_group/mmap/munmap/brk 等必要调用） |
| 命名空间隔离 | `unshare(CLONE_NEWNS \| CLONE_NEWPID \| CLONE_NEWNET)` 隔离挂载、PID、网络 |
| 资源限制 | `setrlimit(RLIMIT_CPU)`, `setrlimit(RLIMIT_AS)`, `setrlimit(RLIMIT_FSIZE)` |
| 文件系统 | `chroot` 到空临时目录，只允许 `/dev/null` 和 `/tmp` |
| 进程超时 | 父进程 `alarm()` / `setitimer` 确保子进程超时被 SIGKILL |

---

## 8. 边缘情况与异常处理

| 场景 | 处理方式 |
|------|---------|
| 重复用户名注册 | 返回 409 Conflict |
| Cookie 过期/无效 | 返回 401 Unauthorized |
| 非管理员访问管理接口 | 返回 403 Forbidden |
| 提交时题目不存在 | 返回 404 |
| 判题进程崩溃/被 OOM Kill | 状态设为 RE (Runtime Error) |
| 判题队列满 | 返回 503 Service Unavailable |
| 代码体积超限 | 返回 413 Payload Too Large |
| 限流期内重复提交 | 返回 429 Too Many Requests |
| MySQL 连接失败 | 返回 500，日志中记录错误 |

---

## 9. 项目结构与 TODO 清单

```
cpp-oj-vibecoding/
├── CMakeLists.txt               -- CMake 构建配置
├── README.md                    -- 项目说明
├── SPEC.md                      -- 本规格文档
├── .gitignore
├── config/
│   └── config.json              -- 配置文件（端口、数据库、Session 路径等）
├── database/
│   └── init.sql                 -- 数据库建表 + root 账号初始化 SQL
├── third_party/
│   ├── httplib.h                -- cpp-httplib (header-only)
│   └── json.hpp                 -- nlohmann-json (header-only)
├── src/
│   ├── main.cc                  -- 程序入口，启动 server + 注册路由
│   ├── server/
│   │   ├── server.cc            -- HTTP server 封装
│   │   └── router.h             -- 路由声明（Route 结构体 + 注册函数）
│   ├── handler/
│   │   ├── auth_handler.cc      -- 注册/登录/登出/me API
│   │   ├── problem_handler.cc   -- 题目 CRUD API
│   │   ├── submit_handler.cc    -- 代码提交 + 查询结果 API
│   │   └── admin_handler.cc     -- 管理接口（测试用例管理）
│   ├── service/
│   │   ├── auth_service.cc      -- 密码哈希 / Session 管理 / 限流
│   │   ├── problem_service.cc   -- 题目业务逻辑 + 通过率计算
│   │   └── executor_service.cc  -- 判题队列 + 沙箱调用 + 结果比对
│   ├── model/
│   │   ├── problem.h            -- Problem 结构体
│   │   ├── test_case.h          -- TestCase 结构体
│   │   └── user.h               -- User 结构体
│   ├── db/
│   │   ├── connection_pool.cc   -- MySQL 连接池
│   │   └── connection_pool.h    -- 连接池头文件
│   └── utils/
│       ├── logger.cc            -- 日志工具
│       ├── logger.h
│       ├── config.cc            -- 配置加载（JSON 解析）
│       └── config.h
├── public/
│   ├── index.html               -- 题目列表页
│   ├── login.html               -- 登录页
│   ├── register.html            -- 注册页
│   ├── problem.html             -- 题目详情页（编辑器 + 提交 + 结果）
│   ├── admin.html               -- 管理后台页
│   ├── css/
│   │   └── style.css            -- 全局样式
│   └── js/
│       ├── api.js               -- fetch 封装 + Cookie 自动携带
│       ├── auth.js              -- 登录状态管理（/me 检查）
│       ├── problem.js           -- 题目列表渲染逻辑
│       ├── problem_detail.js    -- 题目详情 + CodeMirror 集成
│       ├── submit.js            -- 提交 + 轮询结果逻辑
│       └── admin.js             -- 管理后台 CRUD 逻辑
└── tests/
    ├── unit/
    │   ├── problem_test.cc      -- 题目服务单元测试
    │   └── executor_test.cc     -- 判题沙箱单元测试
    └── integration/
        └── api_test.cc          -- API 集成测试
```

### TODO 清单

分 7 个阶段、共 22 个大任务，**详细子任务和进度详见 [`TASK.md`](TASK.md)**。

| 阶段 | 内容 | 任务数 |
|------|------|--------|
| 一 | 环境准备（依赖安装 + 数据库初始化） | 5 |
| 二 | 项目骨架（CMakeLists.txt + config + third_party） | 4 |
| 三 | 基础设施（utils + db + model） | 6 |
| 四 | 后端 Service 层（auth / problem / executor） | 3 |
| 五 | 后端 Handler + Server 层 | 5 |
| 六 | 前端（5 页面 + CSS + JS） | 9 |
| 七 | 测试 & 部署 | 5 |

---

## 10. 验收标准

| # | 验收项 |
|---|--------|
| A1 | 用户可注册新账号，重复用户名拒绝 |
| A2 | 用户可登录/登出，Session Cookie 正常工作 |
| A3 | 未登录用户跳转登录页 |
| A4 | 非管理员访问 `/api/admin/*` 返回 403 |
| A5 | 管理员可新增/编辑/删除题目 |
| A6 | 管理员可为题目添加示例用例和隐藏用例 |
| A7 | 用户可看到题目列表并通过率 |
| A8 | 用户可看到题目详情 + 示例用例 |
| A9 | 用户可在 CodeMirror 编辑器中编写 C++ 代码并提交 |
| A10 | 提交后前端显示"PENDING"→"JUDGING"→最终状态 |
| A11 | WA 时显示第一个未通过的测试用例编号 |
| A12 | CE 时显示编译错误信息 |
| A13 | 同一用户 10s 内重复提交返回 429 |
| A14 | 代码超过 64KB 返回 413 |
| A15 | 执行 start.sh 后系统可正常访问 |
| A16 | Session 文件自动清理机制正常运作 |
