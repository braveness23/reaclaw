// Thin wrapper around ReaClaw's REST API. This is the ONLY thing the backend
// talks to besides the chosen model provider (TECH_DECISIONS §28) -- every
// REAPER-facing tool goes through here, never a private channel.
import { Agent, fetch as undiciFetch } from "undici";
import type { Config } from "./config.js";

export class ReaClawError extends Error {
  constructor(
    public status: number,
    public body: string,
  ) {
    super(`ReaClaw ${status}: ${body}`);
  }
}

export class ReaClawClient {
  private readonly base: string;
  private readonly key: string;
  private readonly dispatcher?: Agent;

  constructor(cfg: Pick<Config, "reaclawBase" | "reaclawKey" | "reaclawInsecureTls">) {
    this.base = cfg.reaclawBase.replace(/\/+$/, "");
    this.key = cfg.reaclawKey;
    // ReaClaw's cert is self-signed by design (SECURITY.md "TLS / Certificate").
    // Scoped to this one client -- never used for the Anthropic/LiteLLM/
    // OpenRouter clients, which must verify normally.
    this.dispatcher = cfg.reaclawInsecureTls
      ? new Agent({ connect: { rejectUnauthorized: false } })
      : undefined;
  }

  async request(method: string, path: string, body?: unknown): Promise<unknown> {
    const res = await undiciFetch(`${this.base}${path}`, {
      method,
      dispatcher: this.dispatcher,
      headers: {
        ...(this.key ? { Authorization: `Bearer ${this.key}` } : {}),
        ...(body !== undefined ? { "Content-Type": "application/json" } : {}),
      },
      body: body !== undefined ? JSON.stringify(body) : undefined,
    });
    const text = await res.text();
    if (!res.ok) throw new ReaClawError(res.status, text);
    return text ? JSON.parse(text) : {};
  }

  get(path: string) {
    return this.request("GET", path);
  }
  post(path: string, body?: unknown) {
    return this.request("POST", path, body ?? {});
  }
}
