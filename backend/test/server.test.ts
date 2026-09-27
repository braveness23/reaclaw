import type { Server } from "node:http";
import type { AddressInfo } from "node:net";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import WebSocket from "ws";
import { loadConfig } from "../src/config.js";
import { startServer } from "../src/server.js";
import { startStubReaClaw, type StubReaClaw } from "./testReaclaw.js";

describe("server", () => {
  let reaclaw: StubReaClaw;
  let server: Server;
  let base: string;
  const token = "test-token-123";

  beforeEach(async () => {
    reaclaw = await startStubReaClaw();
    const cfg = loadConfig({
      REACLAW_BASE: reaclaw.base,
      REACLAW_KEY: "k",
      REACLAW_INSECURE_TLS: "0",
      PORT: "0",
      BACKEND_TOKEN: token,
      REACLAW_CHAT_FAKE_PROVIDER: "1",
    } as NodeJS.ProcessEnv);
    server = startServer(cfg);
    await new Promise<void>((resolve) => server.once("listening", resolve));
    const { port } = server.address() as AddressInfo;
    base = `http://127.0.0.1:${port}`;
  });

  afterEach(async () => {
    await reaclaw.close();
    await new Promise((r) => server.close(r));
  });

  it("answers /health without a token", async () => {
    const res = await fetch(`${base}/health`);
    expect(res.status).toBe(200);
  });

  it("refuses / without the token", async () => {
    const res = await fetch(`${base}/`);
    expect(res.status).toBe(401);
  });

  it("serves the chat page with the right token", async () => {
    const res = await fetch(`${base}/?token=${token}`);
    expect(res.status).toBe(200);
    expect(await res.text()).toContain("ReaClaw Chat");
  });

  it("refuses a WebSocket upgrade with the wrong token", async () => {
    const ws = new WebSocket(`ws://127.0.0.1:${new URL(base).port}/ws?token=wrong`);
    await new Promise<void>((resolve, reject) => {
      ws.on("unexpected-response", (_req, res) => {
        expect(res.statusCode).toBe(401);
        resolve();
      });
      ws.on("open", () => reject(new Error("should not have connected")));
    });
  });

  it("/api/undo requires the token and forwards to POST /undo", async () => {
    const denied = await fetch(`${base}/api/undo`, { method: "POST" });
    expect(denied.status).toBe(401);

    const ok = await fetch(`${base}/api/undo?token=${token}`, { method: "POST" });
    expect(ok.status).toBe(200);
    expect(await ok.json()).toEqual({ undone: true });
    expect(reaclaw.calls).toContainEqual({ method: "POST", path: "/undo", body: {} });
  });

  it("runs a full chat turn over the WebSocket, including an approval round trip", async () => {
    const ws = new WebSocket(`ws://127.0.0.1:${new URL(base).port}/ws?token=${token}`);
    const messages: any[] = [];
    await new Promise<void>((resolve, reject) => {
      ws.on("open", resolve);
      ws.on("error", reject);
    });
    ws.on("message", (raw) => messages.push(JSON.parse(raw.toString())));

    ws.send(JSON.stringify({ type: "user_message", text: "mute track 5" }));

    // Wait for the approval_request, then approve it.
    await vitestWaitFor(() => messages.some((m) => m.type === "approval_request"));
    const approvalId = messages.find((m) => m.type === "approval_request").id;
    ws.send(JSON.stringify({ type: "approval_response", id: approvalId, decision: "allow_once" }));

    await vitestWaitFor(() => messages.some((m) => m.type === "turn_done"));
    ws.close();

    expect(reaclaw.calls).toContainEqual({
      method: "POST",
      path: "/state/tracks/5",
      body: { muted: true },
    });
    const result = messages.find((m) => m.type === "tool_result");
    expect(result.ok).toBe(true);
  });
});

async function vitestWaitFor(cond: () => boolean, timeoutMs = 2000): Promise<void> {
  const start = Date.now();
  while (!cond()) {
    if (Date.now() - start > timeoutMs) throw new Error("timed out waiting for condition");
    await new Promise((r) => setTimeout(r, 10));
  }
}
