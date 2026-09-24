import { readFile } from "node:fs/promises";

const source = await readFile(new URL("../src/api.ts", import.meta.url), "utf8");

if (!source.includes('path === "/api/auth/login" && response.status === 403')) {
  throw new Error("Login must retry once with a fresh CSRF token after a stale-token 403 response.");
}

if (!source.includes("finally {\n    clearCsrfState();")) {
  throw new Error("Logout must clear the in-memory CSRF state after the server deletes its CSRF cookie.");
}

console.log("Authentication recovery assertions passed.");
