import { readFile } from "node:fs/promises";

const source = await readFile(new URL("../src/App.tsx", import.meta.url), "utf8");

for (const marker of [
  'canControl && <div className="camera-admin-tools">',
  'canControl && <div className="camera-internal-details">',
]) {
  if (!source.includes(marker)) {
    throw new Error(`Guest privacy boundary missing: ${marker}`);
  }
}

console.log("Guest privacy boundary assertions passed.");
