# 智能路灯双通道访问 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为智能路灯控制台增加公开只读访客通道和登录保护的管理员通道，同时保留 MQTT、SSE 和现有设备控制能力。

**Architecture:** 后端引入 Spring Security 会话认证和 CSRF 防护；所有 GET 数据接口对访客开放，控制和视觉启停接口只允许 ADMIN。前端用路径区分 `/esp32/`、`/esp32/guest`、`/esp32/admin`，通道选择、登录页和现有仪表盘共用一套 React 应用。

**Tech Stack:** Spring Boot 3.5、Spring Security、Java 17、React、TypeScript、Vite、Vitest/JUnit、Nginx 同源反向代理。

---

### Task 1: 后端认证基础与权限测试

**Files:**
- Modify: `D:/esp/smart_streetlight/backend/pom.xml`
- Create: `D:/esp/smart_streetlight/backend/src/main/java/com/smartstreetlight/config/AuthProperties.java`
- Create: `D:/esp/smart_streetlight/backend/src/main/java/com/smartstreetlight/config/SecurityConfig.java`
- Create: `D:/esp/smart_streetlight/backend/src/main/java/com/smartstreetlight/controller/AuthController.java`
- Create: `D:/esp/smart_streetlight/backend/src/test/java/com/smartstreetlight/SecurityIntegrationTest.java`

- [ ] **Step 1: Add the Spring Security dependency.**

Add `spring-boot-starter-security` beside the existing Spring Boot starters in `backend/pom.xml`.

- [ ] **Step 2: Write failing security tests.**

Use `MockMvc` to assert that GET `/api/devices` is public, POST `/api/devices/GLG-A-001/command` rejects an anonymous request, invalid login is rejected, and valid login creates a session. Mock MQTT and device services so tests do not require a broker.

- [ ] **Step 3: Add server-only authentication properties.**

Bind `app.auth.demo-username` and `app.auth.demo-password` from environment variables. Use a password encoder when creating the in-memory ADMIN user; do not put the demo password in Java source.

- [ ] **Step 4: Add session security configuration.**

Permit `/api/auth/**`, `OPTIONS`, and `GET /api/**`; require `ROLE_ADMIN` for non-GET API operations. Configure form-free JSON login, session cookies with HttpOnly/Secure settings, logout, and a cookie-backed CSRF token endpoint.

- [ ] **Step 5: Add authentication endpoints.**

Implement `POST /api/auth/login`, `POST /api/auth/logout`, `GET /api/auth/session`, and `GET /api/auth/csrf`. Return only username/role state; never return a password or MQTT configuration.

- [ ] **Step 6: Run the backend tests and package.**

Run `mvn test` from `backend`; the new tests must pass and existing tests must remain green.

### Task 2: Frontend API authentication and route modes

**Files:**
- Modify: `D:/esp/smart_streetlight/frontend/src/api.ts`
- Modify: `D:/esp/smart_streetlight/frontend/src/main.tsx`
- Modify: `D:/esp/smart_streetlight/frontend/src/App.tsx`
- Modify: `D:/esp/smart_streetlight/frontend/src/styles.css`

- [ ] **Step 1: Add API auth types and requests.**

Add session, login, logout, and CSRF request helpers. Ensure same-origin requests include cookies, and attach the CSRF token to state-changing requests.

- [ ] **Step 2: Add path-based mode resolution.**

Resolve `selection`, `guest`, and `admin` from `window.location.pathname`. Keep `/esp32/` as the channel chooser and preserve the existing dashboard at the guest/admin paths.

- [ ] **Step 3: Add the channel chooser and admin login UI.**

Create a clean card-based chooser and a compact login panel. Show the public demo account hint on the admin page, but keep infrastructure credentials out of the frontend.

- [ ] **Step 4: Gate the dashboard UI by role.**

Guest mode keeps status, telemetry, SSE, events, faults, and read-only camera information. Only an authenticated ADMIN renders command and vision start/stop controls. Add logout and session-expired feedback to admin mode.

- [ ] **Step 5: Run frontend type checking and production build.**

Run `npm run build` from `frontend` and verify output paths remain under `/esp32/`.

### Task 3: Integration verification and deployment package

**Files:**
- Modify: `D:/esp/smart_streetlight/frontend/.env.production.example` if auth-related values need documenting
- Modify: `D:/esp/smart_streetlight/backend/README.md`
- Modify: `D:/esp/smart_streetlight/README.md`
- Create: `D:/esp/smart_streetlight/release/smart-streetlight-access-*.tar.gz`

- [ ] **Step 1: Verify guest and admin API behavior locally.**

Start the backend with a demo account environment and use `curl` to confirm public GET access, anonymous `403`/`401` for commands, successful login, and authenticated command access.

- [ ] **Step 2: Build the production frontend and backend JAR.**

Run `npm run build`, `mvn test`, and `mvn package -DskipTests`. Confirm the built frontend uses `/esp32-api` and `/esp32/` paths.

- [ ] **Step 3: Create a deployment archive without secrets.**

Include the frontend `dist` files and backend JAR only. Keep demo credentials in the server environment file, never in the archive or Git history.

- [ ] **Step 4: Commit source changes and document deployment.**

Commit implementation and documentation separately from the generated deployment archive. Deployment instructions must update `/opt/smart-streetlight/backend`, preserve `/var/www/eaea499.cn/` outside `/esp32/`, and restart only `smart-streetlight.service`.
