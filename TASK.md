# OJ System - 任务进度

> 对应 SPEC.md §9 TODO 清单。`- [x]` 已完成，`- [/]` 进行中，`- [ ]` 未开始。

---

## 阶段一：环境准备

- [x] 1.1 安装构建工具: `cmake`, `make`, `g++`
- [x] 1.2 安装开发库: `libmysqlclient-dev`, `libssl-dev`, `libseccomp-dev`, `libgtest-dev`
- [x] 1.3 确认 MySQL 服务运行中
- [x] 1.4 创建数据库 `oj_system` + 执行 `database/init.sql` 建表
- [x] 1.5 插入 root 管理员账号（密码 silin_10086）

## 阶段二：项目骨架

- [x] 2.1 根目录 `CMakeLists.txt`
- [x] 2.2 `config/config.json`
- [x] 2.3 `.gitignore`
- [x] 2.4 `third_party/` 下载 cpp-httplib + nlohmann-json

## 阶段三：基础设施层

- [x] 3.1 `src/utils/logger.hpp` + `logger.cc`（日志级别, 文件输出）
- [x] 3.2 `src/utils/config.hpp` + `config.cc`（读取 config.json, 提供 getter）
- [x] 3.3 `src/db/connection_pool.hpp` + `connection_pool.cc`（MySQL 连接池）
- [x] 3.4 `src/model/problem.hpp`
- [x] 3.5 `src/model/test_case.hpp`
- [x] 3.6 `src/model/user.hpp`

## 阶段四：后端 Service 层

> 每个 Service 实现后跟一个对应的集成测试（标记 4.N.t），需 MySQL 运行中方可执行。

- [x] 4.1 `src/service/auth_service.hpp` + `auth_service.cc`
  - [x] 注册: 用户名唯一性检查, bcrypt 哈希存密码
  - [x] 登录: 验证密码, 生成 Session ID, 写 Session 文件到 `/var/oj/sessions/`
  - [x] 鉴权: 根据 Cookie Session ID 读取 Session 文件, 返回 user_id/role
  - [x] 限流: 内存中记录每用户最后提交时间, 10 秒窗口
  - [x] Session 清理: 启动 + 每 30 分钟扫描过期文件
  - [x] 4.1.t `test/auth_test/auth_test.cc`（注册/登录/鉴权/限流/登出集成测试, 12 tests passed）
- [x] 4.2 `src/service/problem_service.hpp` + `problem_service.cc`
  - [x] CRUD 题目（增删改查 + 列表含通过率统计）
  - [x] 查测试用例（按 problem_id + 区分 sample/hidden）
  - [x] 4.2.t `test/problem_test/problem_test.cc`（增删改查+测试用例, 9 tests passed）
- [ ] 4.3 `src/service/executor_service.hpp` + `executor_service.cc`
  - 任务队列 + Worker 线程
  - 沙箱: fork → seccomp → unshare(NS) → setrlimit → chroot → exec
  - 编译用户代码 (g++), 捕获编译错误 → CE
  - 逐组运行测试用例 (stdin → stdout), 比对期望输出
  - 记录结果到 MySQL, 状态机: PENDING → JUDGING → AC/WA/CE/RE/TLE/MLE
  - [ ] 4.3.t `tests/unit/executor_test.cc` + `tests/integration/executor_test.cc`

## 阶段五：后端 Handler + Server 层

> 每个 Handler 实现后跟对应的 API 测试。

- [ ] 5.1 `src/handler/auth_handler.hpp` + `auth_handler.cc`
  - `POST /api/register` → auth_service 注册
  - `POST /api/login` → auth_service 登录, set-cookie
  - `POST /api/logout` → 删除 Session 文件
  - `GET /api/me` → 返回当前用户信息
  - [ ] 5.1.t `tests/integration/auth_api_test.cc`
- [ ] 5.2 `src/handler/problem_handler.hpp` + `problem_handler.cc`
  - `GET /api/problems` → 题目列表
  - `GET /api/problems/{id}` → 题目详情（含示例用例）
  - `POST /api/problems` → 新增（需 admin）
  - `PUT /api/problems/{id}` → 编辑（需 admin）
  - `DELETE /api/problems/{id}` → 删除（需 admin）
  - [ ] 5.2.t `tests/integration/problem_api_test.cc`
- [ ] 5.3 `src/handler/submit_handler.hpp` + `submit_handler.cc`
  - `POST /api/submit` → 校验代码大小/限流/入队, 返回 202 + submission_id
  - `GET /api/submissions/{id}` → 查询判题结果
  - `GET /api/submissions?problem_id=X` → 用户某题提交历史
  - [ ] 5.3.t `tests/integration/submit_api_test.cc`
- [ ] 5.4 `src/handler/admin_handler.hpp` + `admin_handler.cc`
  - `GET /api/problems/{id}/testcases` → 测试用例列表
  - `POST /api/problems/{id}/testcases` → 新增测试用例
  - `DELETE /api/problems/{id}/testcases/{tc_id}` → 删除
  - [ ] 5.4.t `tests/integration/admin_api_test.cc`
- [ ] 5.5 `src/server/router.h` + `src/server/server.cc` + `src/main.cc`
  - 定义路由表（method + path → handler）
  - 封装 httplib::Server 启动
  - main.cc 入口: 加载配置 → 初始化连接池 → 注册路由 → 启动 server
  - [ ] 5.5.t 端到端启动测试

## 阶段六：前端

- [ ] 6.1 `public/css/style.css`（全局样式, 暗色主题）
- [ ] 6.2 `public/js/api.js`（fetch 封装, 自动携带 Cookie, 统一错误处理）
- [ ] 6.3 `public/js/auth.js`（页面加载时检查 /api/me, 未登录跳转 /login）
- [ ] 6.4 `public/login.html` + 登录逻辑（内联在 login.html）
- [ ] 6.5 `public/register.html` + 注册逻辑
- [ ] 6.6 `public/index.html` + `public/js/problem.js`（题目列表渲染）
- [ ] 6.7 `public/problem.html` + `public/js/problem_detail.js`（题目详情 + CodeMirror）
- [ ] 6.8 `public/problem.html` + `public/js/submit.js`（提交按钮 + 轮询结果）
- [ ] 6.9 `public/admin.html` + `public/js/admin.js`（题目 CRUD + 测试用例管理）

## 阶段七：部署

- [ ] 7.1 `scripts/start.sh`（编译 + 运行后端）
- [ ] 7.2 systemd service 文件（开机自启, 日志收集）
