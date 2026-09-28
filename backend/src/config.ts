// Config the extension passes this process at spawn time (§31: env vars, not
// argv, so nothing sensitive shows up in `ps`). Every field has a dev-friendly
// default so the backend also runs standalone via `npm run dev`.
import { randomBytes } from "node:crypto";

export type ProviderKind = "anthropic" | "openai_compat" | "fake";

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
  provider: ProviderKind;
  anthropicApiKey?: string;
  anthropicModel?: string;
  /** One client, many providers: LiteLLM, OpenRouter and Ollama all speak the
   *  same OpenAI-compatible chat-completions format, so this is the base URL
   *  of whichever one the user pointed at -- no per-provider code needed. */
  openaiCompatBaseUrl?: string;
  openaiCompatApiKey?: string;
  openaiCompatModel?: string;
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): Config {
  const cfg: Config = {
    reaclawBase: env.REACLAW_BASE ?? "https://127.0.0.1:9091",
    reaclawKey: env.REACLAW_KEY ?? "",
    reaclawInsecureTls: env.REACLAW_INSECURE_TLS !== "0",
    port: env.PORT ? Number(env.PORT) : 0,
    token: env.BACKEND_TOKEN ?? randomBytes(24).toString("hex"),
    provider: pickProvider(env),
    anthropicApiKey: env.ANTHROPIC_API_KEY,
    anthropicModel: env.ANTHROPIC_MODEL,
    openaiCompatBaseUrl: env.OPENAI_COMPAT_BASE_URL,
    openaiCompatApiKey: env.OPENAI_COMPAT_API_KEY,
    openaiCompatModel: env.OPENAI_COMPAT_MODEL,
  };

  if (cfg.provider === "openai_compat" && !cfg.openaiCompatModel) {
    throw new Error(
      "OPENAI_COMPAT_BASE_URL is set but OPENAI_COMPAT_MODEL is not -- an OpenAI-" +
        "compatible server (LiteLLM/OpenRouter/Ollama) doesn't have one universal " +
        "default model, so this has to be explicit.",
    );
  }
  return cfg;
}

// Explicit opt-in beats guessing: REACLAW_CHAT_FAKE_PROVIDER always wins (tests
// force it), then whichever real provider has its config present, else fake --
// so `npm run dev` still works with nothing configured yet.
function pickProvider(env: NodeJS.ProcessEnv): ProviderKind {
  if (env.REACLAW_CHAT_FAKE_PROVIDER === "1") return "fake";
  if (env.OPENAI_COMPAT_BASE_URL) return "openai_compat";
  if (env.ANTHROPIC_API_KEY) return "anthropic";
  return "fake";
}
