// Serves a Godot web export and runs it in headless Chromium, waiting for the FMOD smoke test result
// printed by demo/test/web/web_smoke.gd.
// Usage: node smoke_test.mjs <export dir> [timeout seconds]
// Requires the "playwright" package. Set CHROMIUM_PATH to use a specific Chromium executable.
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize } from "node:path";
import { chromium } from "playwright";

const exportDir = process.argv[2];
const timeoutMs = (Number(process.argv[3]) || 120) * 1000;
if (!exportDir) {
  console.error("Usage: node smoke_test.mjs <export dir> [timeout seconds]");
  process.exit(1);
}

const mimeTypes = {
  ".html": "text/html",
  ".js": "application/javascript",
  ".wasm": "application/wasm",
  ".pck": "application/octet-stream",
  ".png": "image/png",
  ".svg": "image/svg+xml",
};

const server = createServer(async (req, res) => {
  const path = normalize(decodeURIComponent(new URL(req.url, "http://localhost").pathname)).replace(/^([/\\])+/, "");
  try {
    if (path.startsWith("..")) throw new Error("outside export dir");
    const body = await readFile(join(exportDir, path || "index.html"));
    res.writeHead(200, { "Content-Type": mimeTypes[extname(path)] || "application/octet-stream" });
    res.end(body);
  } catch {
    res.writeHead(404);
    res.end();
  }
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const url = `http://127.0.0.1:${server.address().port}/index.html`;

const browser = await chromium.launch({
  executablePath: process.env.CHROMIUM_PATH || undefined,
  args: ["--autoplay-policy=no-user-gesture-required", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"],
});
const page = await browser.newPage();

let result = null;
const finished = new Promise((resolve) => {
  page.on("console", (message) => {
    const text = message.text();
    console.log(`[browser ${message.type()}] ${text}`);
    if (text.includes("FMOD_WEB_OK") || text.includes("FMOD_WEB_FAIL")) {
      result = text;
      resolve();
    }
  });
  page.on("pageerror", (error) => console.log(`[browser pageerror] ${error.message}`));
});

console.log(`Loading ${url}`);
await page.goto(url);
await Promise.race([finished, new Promise((resolve) => setTimeout(resolve, timeoutMs))]);

await browser.close();
server.close();

if (result && result.includes("FMOD_WEB_OK")) {
  console.log(`Smoke test passed: ${result}`);
  process.exit(0);
}
console.error(`Smoke test failed: ${result ?? "no result before timeout"}`);
process.exit(1);
