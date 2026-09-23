# Smart Streetlight Access Design

## Goal

Provide two clear access channels for the public smart streetlight console:

- Guest channel: public, read-only, suitable for HR and project visitors.
- Admin channel: login-protected, with real device and vision controls.

The demo account is intentionally visible on the admin login page so the deployed project can be demonstrated without a separate credential handoff. It is an application demo credential only and must never be reused for the server, MQTT broker, or other infrastructure.

## User Experience

### Channel selection

`/esp32/` shows a concise channel selection page with two cards:

- `访客通道`: opens `/esp32/guest`.
- `管理员通道`: opens `/esp32/admin`.

The existing dashboard remains the visual foundation. No duplicate dashboard implementation is introduced.

### Guest channel

`/esp32/guest` renders the dashboard in read-only mode. It may load:

- MQTT connection status
- Device list and online state
- Telemetry, vision results, event history, and faults
- SSE live updates

It must not render or call command publishing or vision start/stop actions.

### Admin channel

`/esp32/admin` first renders a branded login panel. The panel displays the explicitly public demo credentials configured for this deployment. After successful login, the same dashboard renders in admin mode and enables existing control features.

Logout returns to the admin login panel. Unauthenticated access to admin-only API operations returns `401` or `403` and never publishes an MQTT command.

## Backend Design

Use Spring Security with a server-side session cookie:

- Demo user is configured from server-only environment variables.
- Password is not committed to source, frontend assets, or GitHub.
- Session cookie is `HttpOnly`, `Secure`, and suitable for same-origin HTTPS requests.
- Login, logout, current-session, and CSRF endpoints are under `/api/auth`.
- All `GET /api/**` read endpoints remain available to guests.
- Command publishing and vision start/stop endpoints require the `ADMIN` role.
- Unknown methods and non-read API routes remain protected by default.
- CSRF protection is enabled for state-changing requests.

The existing same-origin `/esp32-api` Nginx proxy remains unchanged. The frontend continues using the relative API base and browser session cookies.

## Frontend Design

Add a small access-mode layer rather than duplicating the dashboard:

- Resolve mode from the `/esp32/`, `/esp32/guest`, and `/esp32/admin` paths.
- Keep dashboard data loading and SSE behavior shared.
- Gate control and vision action components by the resolved authenticated role.
- Add login, logout, authentication error, loading, and expired-session states.
- On a `401` from an admin action, return to the admin login panel with a concise message.
- Keep guest controls absent from the DOM where practical; backend authorization remains the source of truth.

## Verification

Automated backend tests must cover:

- Guest can read device and event data.
- Guest cannot publish a device or batch command.
- Guest cannot start or stop a vision service.
- Valid demo credentials create an authenticated admin session.
- Invalid credentials are rejected.
- An authenticated admin can access protected operations.

Manual deployment verification must cover:

1. `/esp32/` shows both channels.
2. Guest channel loads telemetry and SSE without login.
3. Admin channel requires login and shows controls only after login.
4. Guest direct requests to command endpoints fail with `401` or `403`.
5. Admin control still reaches the device and receives the existing acknowledgement.
6. Logout removes access to protected operations.
7. Restarting the backend preserves the configured demo account and MQTT connection.
