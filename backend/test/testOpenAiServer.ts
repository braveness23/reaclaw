// A minimal stand-in for an OpenAI-compatible chat-completions endpoint
// (what LiteLLM/OpenRouter/Ollama all actually implement), so provider tests
// exercise the real HTTP + JSON round trip instead of mocking fetch.
import { createServer, type Server } from "node:http";
import type { AddressInfo } from "node:net";

export interface StubOpenAi {
  base: string;
  requests: Array<{ messages: any[]; tools: any[] }>;
  close: () => Promise<void>;
}

/** `script` is returned one entry per request, in order; the last entry repeats if exhausted. */
export function startStubOpenAi(script: Array<Record<string, unknown>>): Promise<StubOpenAi> {
  const requests: StubOpenAi["requests"] = [];
  let call = 0;
  const server = createServer((req, res) => {
    const chunks: Buffer[] = [];
    req.on("data", (c) => chunks.push(c));
    req.on("end", () => {
      const body = JSON.parse(Buffer.concat(chunks).toString("utf8"));
      requests.push({ messages: body.messages, tools: body.tools });
      const message = script[Math.min(call, script.length - 1)];
      call++;
      res.writeHead(200, { "content-type": "application/json" });
      res.end(JSON.stringify({ choices: [{ message, finish_reason: "stop" }] }));
    });
  });
  return new Promise((resolve) => {
    server.listen(0, "127.0.0.1", () => {
      const { port } = server.address() as AddressInfo;
      resolve({
        base: `http://127.0.0.1:${port}/v1`,
        requests,
        close: () => new Promise((r) => server.close(() => r())),
      });
    });
  });
}
