import { readFile } from "node:fs/promises";

const source = await readFile(new URL("../public/sw.js", import.meta.url), "utf8");

if (!source.includes('request.mode === "navigate"')) {
  throw new Error("Navigation requests must be fetched from the network before using the cache.");
}

if (!source.includes("smart-streetlight-ui-v4")) {
  throw new Error("The service worker cache version must advance when the app shell changes.");
}

console.log("Service worker cache policy assertions passed.");
