// Serves a Godot web export, runs it in Chromium and waits for the FMOD smoke test result printed by
// demo/test/web/web_smoke.gd.
//
// Usage: node smoke_test.mjs <export dir> [timeout seconds]
// Environment:
//   CHROMIUM_PATH  Chromium/Chrome executable (default: google-chrome, chromium or chromium-browser from PATH).
//   HEADED=1       Show the browser window instead of running headless.
//
// The browser is driven over the DevTools protocol directly, without Playwright or Puppeteer: those attach to
// every worker and worklet target, which leaves AudioWorklet modules pending forever, so FMOD's mixer never
// starts under them. Only a page session with the Page, Runtime and Input domains is used here.
//
// FMOD's web output only starts from a click or touch listener on the window, so the test clicks the page once
// the smoke scene reports that it started. No autoplay flag replaces that gesture.
import { spawn } from "node:child_process";
import { accessSync, constants, mkdtempSync, rmSync } from "node:fs";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { extname, join, normalize } from "node:path";

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

// Injected before any page script. Tracks AudioWorklet setup, to tell FMOD problems apart from a browser without
// working audio, and the traffic between FMOD's mixer on the main thread and its worklet processor. FMOD
// double-buffers one DSP buffer per message: when the main thread answers late, the processor replays the
// previous buffer, which is audible as crackling, and the late requests reach the main thread in a burst.
const initScript = `(() => {
  const stats = { modules: 0, resolved: 0, rejected: 0, nodes: [] };
  window.__audioStats = stats;
  const addModule = AudioWorklet.prototype.addModule;
  AudioWorklet.prototype.addModule = function (...args) {
    stats.modules++;
    return addModule.apply(this, args).then(
      (value) => { stats.resolved++; return value; },
      (error) => { stats.rejected++; throw error; },
    );
  };
  const WorkletNode = window.AudioWorkletNode;
  window.AudioWorkletNode = class extends WorkletNode {
    constructor(context, name, options) {
      super(context, name, options);
      const channels = (options && options.outputChannelCount && options.outputChannelCount[0]) || 2;
      const node = { name, messages: 0, bufferFrames: 0, bufferMs: 0, stalls: 0, replayed: 0, maxGapMs: 0 };
      stats.nodes.push(node);
      let last = 0;
      this.port.addEventListener("message", () => {
        node.messages++;
        const now = performance.now();
        if (last && node.bufferMs) {
          const gap = now - last;
          if (gap > node.maxGapMs) node.maxGapMs = gap;
          if (gap > 1.5 * node.bufferMs) node.stalls++;
          if (gap < 1) node.replayed++;
        }
        last = now;
      });
      const postMessage = this.port.postMessage.bind(this.port);
      this.port.postMessage = (data, ...rest) => {
        if (!node.bufferFrames && data && typeof data.length === "number") {
          node.bufferFrames = data.length / channels;
          node.bufferMs = node.bufferFrames / context.sampleRate * 1000;
        }
        return postMessage(data, ...rest);
      };
    }
  };
})();`;

function findBrowser() {
  if (process.env.CHROMIUM_PATH) return process.env.CHROMIUM_PATH;
  for (const name of ["google-chrome", "google-chrome-stable", "chromium", "chromium-browser"]) {
    for (const dir of (process.env.PATH || "").split(":")) {
      const candidate = join(dir, name);
      try {
        accessSync(candidate, constants.X_OK);
        return candidate;
      } catch {}
    }
  }
  throw new Error("No Chromium found in PATH, set CHROMIUM_PATH");
}

const userDataDir = mkdtempSync(join(tmpdir(), "fmod-smoke-"));
const browserArgs = [
  `--user-data-dir=${userDataDir}`,
  "--remote-debugging-port=0",
  "--no-first-run",
  "--no-default-browser-check",
  "--no-sandbox",
  "--autoplay-policy=no-user-gesture-required",
  "--use-angle=swiftshader",
  "--enable-unsafe-swiftshader",
  "--window-size=1024,768",
];
if (!process.env.HEADED) browserArgs.push("--headless=new");
browserArgs.push("about:blank");

const browserPath = findBrowser();
console.log(`Using browser ${browserPath}${process.env.HEADED ? "" : " (headless)"}`);
const browser = spawn(browserPath, browserArgs, { stdio: ["ignore", "ignore", "pipe"] });
const browserWsUrl = await new Promise((resolve, reject) => {
  let stderr = "";
  browser.stderr.on("data", (chunk) => {
    stderr += chunk;
    const match = stderr.match(/DevTools listening on (ws:\/\/\S+)/);
    if (match) resolve(match[1]);
  });
  browser.on("exit", (code) => reject(new Error(`Browser exited with code ${code}\n${stderr}`)));
  setTimeout(() => reject(new Error(`No DevTools endpoint after 30 s\n${stderr}`)), 30000);
});

// Minimal DevTools protocol client: one WebSocket, commands optionally scoped to a session.
const ws = new WebSocket(browserWsUrl);
await new Promise((resolve, reject) => {
  ws.onopen = resolve;
  ws.onerror = () => reject(new Error("Cannot connect to the DevTools endpoint"));
});
let nextId = 0;
const pending = new Map();
const listeners = [];
ws.onmessage = (event) => {
  const message = JSON.parse(event.data);
  if (message.id !== undefined && pending.has(message.id)) {
    const { resolve, reject } = pending.get(message.id);
    pending.delete(message.id);
    if (message.error) reject(new Error(`${message.error.message} ${message.error.data ?? ""}`));
    else resolve(message.result);
  } else if (message.method) {
    for (const listener of listeners) listener(message);
  }
};
const send = (method, params = {}, sessionId) => new Promise((resolve, reject) => {
  const id = ++nextId;
  pending.set(id, { resolve, reject });
  ws.send(JSON.stringify({ id, method, params, sessionId }));
});

const { targetId } = await send("Target.createTarget", { url: "about:blank" });
const { sessionId } = await send("Target.attachToTarget", { targetId, flatten: true });
const page = (method, params) => send(method, params, sessionId);
await page("Page.enable");
await page("Runtime.enable");
await page("Page.addScriptToEvaluateOnNewDocument", { source: initScript });

// A trusted click in the middle of the page: FMOD registers click/touchend listeners on the window, and only
// that handler resumes its AudioContext and loads its worklet module.
async function clickPage() {
  const { cssVisualViewport } = await page("Page.getLayoutMetrics");
  const x = Math.round(cssVisualViewport.clientWidth / 2);
  const y = Math.round(cssVisualViewport.clientHeight / 2);
  await page("Input.dispatchMouseEvent", { type: "mouseMoved", x, y });
  await page("Input.dispatchMouseEvent", { type: "mousePressed", x, y, button: "left", clickCount: 1 });
  await page("Input.dispatchMouseEvent", { type: "mouseReleased", x, y, button: "left", clickCount: 1 });
  console.log(`Clicked the page at ${x},${y}`);
}

let result = null;
let clicked = false;
const finished = new Promise((resolve) => {
  listeners.push((message) => {
    if (message.sessionId !== sessionId) return;
    if (message.method === "Runtime.consoleAPICalled") {
      const text = message.params.args.map((arg) => arg.value ?? arg.description ?? "").join(" ");
      console.log(`[browser ${message.params.type}] ${text}`);
      if (text.includes("FMOD_WEB_SMOKE: started") && !clicked) {
        clicked = true;
        clickPage().catch((error) => console.log(`Click failed: ${error.message}`));
      }
      if (text.includes("FMOD_WEB_OK") || text.includes("FMOD_WEB_FAIL")) {
        result = text;
        resolve();
      }
    } else if (message.method === "Runtime.exceptionThrown") {
      const details = message.params.exceptionDetails;
      console.log(`[browser pageerror] ${details.exception?.description ?? details.text}`);
    }
  });
});

console.log(`Loading ${url}`);
await page("Page.navigate", { url });
await Promise.race([finished, new Promise((resolve) => setTimeout(resolve, timeoutMs))]);

let audioStats = null;
try {
  const { result: evaluated } = await page("Runtime.evaluate", { expression: "JSON.stringify(window.__audioStats)", returnByValue: true });
  audioStats = JSON.parse(evaluated.value);
  console.log(`AudioWorklet: ${JSON.stringify(audioStats)}`);
} catch (error) {
  console.log(`AudioWorklet: unavailable (${error.message})`);
}

ws.close();
server.close();
browser.kill("SIGKILL");
// Chrome keeps writing to its profile until it is gone, remove the directory only afterwards.
await new Promise((resolve) => {
  browser.once("exit", resolve);
  setTimeout(resolve, 5000);
});
try {
  rmSync(userDataDir, { recursive: true, force: true });
} catch (error) {
  console.log(`Could not remove ${userDataDir}: ${error.message}`);
}

const fmodNode = audioStats?.nodes.find((node) => node.name === "audio-processor");
if (fmodNode && fmodNode.messages) {
  const replayedPercent = (100 * fmodNode.replayed / fmodNode.messages).toFixed(1);
  console.log(`FMOD mixer: ${fmodNode.bufferFrames} frames per buffer (${fmodNode.bufferMs.toFixed(1)} ms), `
    + `${fmodNode.messages} buffers, ${fmodNode.stalls} stalls (longest gap ${fmodNode.maxGapMs.toFixed(0)} ms), `
    + `${fmodNode.replayed} replayed (${replayedPercent}%)`);
}

// Occasional replays happen on a busy machine, a mixer that misses a quarter of its deadlines is broken.
const maxReplayedPercent = 25;

if (result && result.includes("FMOD_WEB_OK")) {
  if (!fmodNode || !fmodNode.messages) {
    console.error("Smoke test failed: the timeline advanced but FMOD's AudioWorklet node never exchanged buffers");
    process.exit(1);
  }
  if (100 * fmodNode.replayed / fmodNode.messages > maxReplayedPercent) {
    console.error(`Smoke test failed: FMOD replayed more than ${maxReplayedPercent}% of its buffers, the audio crackles`);
    process.exit(1);
  }
  console.log(`Smoke test passed: ${result}`);
  process.exit(0);
}
console.error(`Smoke test failed: ${result ?? "no result before timeout"}`);
process.exit(1);
