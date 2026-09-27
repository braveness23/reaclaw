// A minimal stand-in for ReaClaw's REST server, so tests exercise the real
// HTTP path (ReaClawClient -> fetch -> this) without a live REAPER.
import { createServer, type Server } from "node:http";
import type { AddressInfo } from "node:net";

export interface StubReaClaw {
  server: Server;
  base: string;
  calls: Array<{ method: string; path: string; body: unknown }>;
  close: () => Promise<void>;
}

export function startStubReaClaw(): Promise<StubReaClaw> {
  const calls: StubReaClaw["calls"] = [];
  const server = createServer((req, res) => {
    const chunks: Buffer[] = [];
    req.on("data", (c) => chunks.push(c));
    req.on("end", () => {
      const raw = Buffer.concat(chunks).toString("utf8");
      const body = raw ? JSON.parse(raw) : undefined;
      calls.push({ method: req.method!, path: req.url!, body });

      if (req.url === "/state/tracks") {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ tracks: [{ index: 0, name: "Kick", volume_db: -7.2 }] }));
        return;
      }
      if (req.url?.startsWith("/state/tracks/") && req.method === "POST") {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ updated: true, ...body }));
        return;
      }
      if (req.url === "/undo") {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ undone: true }));
        return;
      }
      if (req.url === "/execute/action") {
        // id 1 simulates an unknown/rejected action, everything else succeeds.
        if (body?.id === 1) {
          res.writeHead(400, { "content-type": "application/json" });
          res.end(JSON.stringify({ error: "unknown action id" }));
          return;
        }
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ executed: true }));
        return;
      }
      res.writeHead(404);
      res.end("not found");
    });
  });

  return new Promise((resolve) => {
    server.listen(0, "127.0.0.1", () => {
      const { port } = server.address() as AddressInfo;
      resolve({
        server,
        base: `http://127.0.0.1:${port}`,
        calls,
        close: () => new Promise((r) => server.close(() => r())),
      });
    });
  });
}
