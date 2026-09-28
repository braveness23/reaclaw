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
      const method = req.method!;
      const url = req.url!;
      calls.push({ method, path: url, body });
      const json = (status: number, payload: unknown) => {
        res.writeHead(status, { "content-type": "application/json" });
        res.end(JSON.stringify(payload));
      };

      if (method === "GET" && url === "/state/tracks") {
        return json(200, { tracks: [{ index: 0, name: "Kick", volume_db: -7.2 }] });
      }
      if (method === "POST" && url === "/state/tracks") {
        const created = (body?.create ?? []).map((t: any, i: number) => ({ index: i, ...t }));
        return json(200, { created, updated: [] });
      }
      if (method === "POST" && url.startsWith("/state/tracks/") && url.includes("/fx/")) {
        return json(200, { track: 0, slot: 0, guid: "{fx-guid}", ...body });
      }
      if (method === "POST" && url.match(/^\/state\/tracks\/\d+\/fx$/)) {
        if (body.name === "NoSuchPlugin") return json(400, { error: `FX not found: ${body.name}` });
        return json(200, { track: 0, slot: 0, guid: "{fx-guid}", name: body.name, enabled: true });
      }
      if (method === "POST" && url.startsWith("/state/tracks/")) {
        return json(200, { updated: true, ...body });
      }
      if (method === "POST" && url === "/state/items") {
        const created = (body?.create ?? []).map((it: any, i: number) => ({
          index: i,
          take: it.file ? { name: "" } : null,
          ...it,
        }));
        return json(200, { created, updated: [] });
      }
      if (method === "POST" && url.match(/^\/state\/items\/\d+\/midi$/)) {
        return json(200, {
          ok: true,
          notes_inserted: (body?.notes ?? []).length,
          cc_inserted: 0,
          notes_deleted: body.replace ? 1 : 0,
          cc_deleted: 0,
          warnings: [],
        });
      }
      if (method === "GET" && url === "/transport") {
        return json(200, { playing: false, paused: false, recording: false, position: 0, loop_enabled: false, loop_start: 0, loop_end: 0 });
      }
      if (method === "POST" && url === "/transport") {
        return json(200, { action: body.action, transport: { playing: body.action === "play" } });
      }
      if (method === "POST" && url === "/transport/loop") {
        return json(200, { start: 0, end: 8, enabled: true, ...body });
      }
      if (method === "POST" && url === "/render") {
        return json(200, { output_path: body.output, render_seconds: 0.1, offline_ratio: 20 });
      }
      if (method === "POST" && url === "/undo") {
        return json(200, { undone: true });
      }
      if (method === "POST" && url === "/execute/action") {
        // id 1 simulates an unknown/rejected action, everything else succeeds.
        if (body?.id === 1) return json(400, { error: "unknown action id" });
        return json(200, { executed: true });
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
