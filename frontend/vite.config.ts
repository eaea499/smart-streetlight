import { defineConfig, loadEnv } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, ".", "");

  return {
    // The public console is mounted below the personal site, not at its root.
    base: env.VITE_BASE_PATH || (mode === "production" ? "/esp32/" : "/"),
    plugins: [react()],
    server: {
      port: 5173,
      strictPort: false,
    },
  };
});
