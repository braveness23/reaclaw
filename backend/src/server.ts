// The backend process the extension spawns on demand (TECH_DECISIONS §31):
// loopback only, random port reported back over stdout (same shape as the
// dock-webview spike's "XID <n>" line), a fresh token required on every
// request. Serves the chat page and speaks it over a WebSocket.
import { createServer } from "node:http";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";
import express from "express";
import { WebSocketServer, type WebSocket } from "ws";
import { loadConfig } from "./config.js";
import { ReaClawClient } from "./reaclawClient.js";
import { ApprovalBroker, type ApprovalDecision } from "./approvals.js";
import { AnthropicProvider } from "./providers/anthropic.js";
import { FakeProvider } from "./providers/fake.js";
import type { Provider } from "./providers/types.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export function createApp(cfg: ReturnType<typeof loadConfig>) {
  const app = express();
  app.use(express.json());

  const checkToken = (req: { query: any; headers: any }): boolean => {
    const header = req.headers.authorization;
    const bearer = typeof header === "string" ? header.replace(/^Bearer\s+/i, "") : undefined;
    const supplied = bearer ?? req.query.token;
    return supplied === cfg.token;
  };

  app.get("/health", (_req, res) => res.json({ status: "ok" }));

  app.get("/", (req, res) => {
    if (!checkToken(req)) return res.status(401).send("missing or bad token");
    res.type("html").send(readFileSync(path.join(__dirname, "ui", "index.html"), "utf8"));
  });

  app.post("/api/undo", async (req, res) => {
    if (!checkToken(req)) return res.status(401).json({ error: "missing or bad token" });
    try {
      const client = new ReaClawClient(cfg);
      res.json(await client.post("/undo"));
    } catch (e) {
      res.status(502).json({ error: e instanceof Error ? e.message : String(e) });
    }
  });

  return { app, checkToken };
}

function makeProvider(cfg: ReturnType<typeof loadConfig>, broker: ApprovalBroker, client: ReaClawClient): Provider {
  if (!cfg.useFakeProvider && cfg.anthropicApiKey) {
    return new AnthropicProvider(cfg.anthropicApiKey, broker, client);
  }
  return new FakeProvider(broker, client);
}

type ClientMessage =
  | { type: "user_message"; text: string }
  | { type: "approval_response"; id: string; decision: ApprovalDecision };

function attachSession(ws: WebSocket, cfg: ReturnType<typeof loadConfig>): void {
  const client = new ReaClawClient(cfg);
  const send = (obj: unknown) => {
    if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj));
  };
  const broker = new ApprovalBroker((req) => send({ type: "approval_request", ...req }));
  const provider = makeProvider(cfg, broker, client);

  ws.on("message", (raw) => {
    let msg: ClientMessage;
    try {
      msg = JSON.parse(raw.toString());
    } catch {
      send({ type: "error", message: "malformed message" });
      return;
    }
    if (msg.type === "approval_response") {
      broker.resolve(msg.id, msg.decision);
    } else if (msg.type === "user_message") {
      void provider.sendMessage(msg.text, send);
    }
  });

  ws.on("close", () => broker.denyAllPending());
}

export function startServer(cfg = loadConfig()) {
  const { app, checkToken } = createApp(cfg);
  const server = createServer(app);
  const wss = new WebSocketServer({ noServer: true });

  server.on("upgrade", (req, socket, head) => {
    const url = new URL(req.url ?? "", "http://internal");
    if (!checkToken({ query: Object.fromEntries(url.searchParams), headers: req.headers })) {
      socket.write("HTTP/1.1 401 Unauthorized\r\n\r\n");
      socket.destroy();
      return;
    }
    wss.handleUpgrade(req, socket, head, (ws) => attachSession(ws, cfg));
  });

  server.listen(cfg.port, "127.0.0.1", () => {
    const addr = server.address();
    const port = typeof addr === "object" && addr ? addr.port : cfg.port;
    // Protocol the extension reads on startup (§31): exactly these two lines.
    console.log(`PORT ${port}`);
    console.log(`TOKEN ${cfg.token}`);
  });

  return server;
}

// Only auto-start when run directly (`node dist/server.js` / `tsx src/server.ts`),
// not when imported by tests.
if (process.argv[1] === fileURLToPath(import.meta.url)) {
  startServer();
}
