// Config the extension passes this process at spawn time (§31: env vars, not
// argv, so nothing sensitive shows up in `ps`). Every field has a dev-friendly
// default so the backend also runs standalone via `npm run dev`.
import { randomBytes } from "node:crypto";

export interface Config {
  /** ReaClaw's own base URL, e.g. https://127.0.0.1:9091 */
  reaclawBase: string;
  /** ReaClaw's bearer key, for calling its REST API. */
  reaclawKey: string;
  /** ReaClaw's cert is self-signed (SECURITY.md) -- only skip verification
   *  for this one loopback client, never for calls to a model provider. */
  reaclawInsecureTls: boolean;
  /** 0 = let the OS pick a free port (the shipped default; the extension
   *  reads the real port back over stdout, see server.ts). */
  port: number;
  /** Required on every HTTP and WebSocket request. Fresh per launch. */
  token: string;
  anthropicApiKey?: string;
  /** No provider configured yet -- use the deterministic fake instead of
   *  failing outright, so `npm run dev` works before you've set a key. */
  useFakeProvider: boolean;
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): Config {
  return {
    reaclawBase: env.REACLAW_BASE ?? "https://127.0.0.1:9091",
    reaclawKey: env.REACLAW_KEY ?? "",
    reaclawInsecureTls: env.REACLAW_INSECURE_TLS !== "0",
    port: env.PORT ? Number(env.PORT) : 0,
    token: env.BACKEND_TOKEN ?? randomBytes(24).toString("hex"),
    anthropicApiKey: env.ANTHROPIC_API_KEY,
    useFakeProvider: env.REACLAW_CHAT_FAKE_PROVIDER === "1" || !env.ANTHROPIC_API_KEY,
  };
}
