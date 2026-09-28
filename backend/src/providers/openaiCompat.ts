// One client for LiteLLM, OpenRouter, and Ollama: all three speak the same
// OpenAI chat-completions wire format, so there's no per-provider code here --
// just a base URL, a key, and a model name (TECH_DECISIONS §28: "standards,
// not one-off libraries per provider"). Unlike the Anthropic leg, this format
// has no server-side agent loop, so the multi-turn "model asks for a tool ->
// we run it -> tell it the result -> it continues" loop is written out below
// by hand.
import { findTool, validateToolArgs, REACLAW_TOOLS } from "../tools/reaclawTools.js";
import { toOpenAiTool } from "../tools/openaiSchema.js";
import type { ApprovalBroker } from "../approvals.js";
import type { ReaClawClient } from "../reaclawClient.js";
import type { ChatEvent, Provider } from "./types.js";

// Safety valve against a model stuck calling tools forever; a real turn
// rarely needs more than a couple of round trips.
const MAX_TOOL_ROUNDS = 8;

type Role = "system" | "user" | "assistant" | "tool";

interface ToolCall {
  id: string;
  type: "function";
  function: { name: string; arguments: string };
}

interface Message {
  role: Role;
  content: string | null;
  tool_calls?: ToolCall[];
  tool_call_id?: string;
}

const SYSTEM_PROMPT =
  "You are an assistant embedded in REAPER (a digital audio workstation) via " +
  "ReaClaw. Use the provided tools to inspect and change the current project. " +
  "Only call a tool when it's actually needed to answer or act on the request.";

export class OpenAiCompatProvider implements Provider {
  private readonly tools = REACLAW_TOOLS.map(toOpenAiTool);
  private readonly messages: Message[] = [{ role: "system", content: SYSTEM_PROMPT }];
  private readonly endpoint: string;

  constructor(
    baseUrl: string,
    private readonly apiKey: string | undefined,
    private readonly model: string,
    private readonly broker: ApprovalBroker,
    private readonly client: ReaClawClient,
  ) {
    this.endpoint = `${baseUrl.replace(/\/+$/, "")}/chat/completions`;
  }

  async sendMessage(text: string, emit: (event: ChatEvent) => void): Promise<void> {
    this.messages.push({ role: "user", content: text });
    try {
      for (let round = 0; round < MAX_TOOL_ROUNDS; round++) {
        const message = await this.complete();
        this.messages.push(message);

        if (message.content) emit({ type: "assistant_text", text: message.content });

        if (!message.tool_calls?.length) {
          emit({ type: "turn_done" });
          return;
        }
        for (const call of message.tool_calls) {
          await this.runToolCall(call, emit);
        }
      }
      emit({ type: "error", message: `stopped after ${MAX_TOOL_ROUNDS} tool-call rounds` });
    } catch (e) {
      emit({ type: "error", message: e instanceof Error ? e.message : String(e) });
    }
  }

  private async complete(): Promise<Message> {
    const res = await fetch(this.endpoint, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        ...(this.apiKey ? { Authorization: `Bearer ${this.apiKey}` } : {}),
      },
      body: JSON.stringify({
        model: this.model,
        messages: this.messages,
        tools: this.tools,
        tool_choice: "auto",
      }),
    });
    const body = await res.text();
    if (!res.ok) throw new Error(`${this.endpoint} ${res.status}: ${body}`);
    const parsed = JSON.parse(body) as { choices: Array<{ message: Message }> };
    const message = parsed.choices[0]?.message;
    if (!message) throw new Error(`no choices in response: ${body}`);
    return message;
  }

  private async runToolCall(call: ToolCall, emit: (event: ChatEvent) => void): Promise<void> {
    const name = call.function.name;
    const input = parseArgs(call.function.arguments);
    emit({ type: "tool_call", toolUseId: call.id, name, input });

    const spec = findTool(name);
    let feedback: unknown;
    let ok: boolean;
    let error: string | undefined;
    let output: unknown;
    if (!spec) {
      ok = false;
      error = `unknown tool: ${name}`;
    } else {
      try {
        const validated = validateToolArgs(spec, input);
        if (spec.mutates) await this.broker.requestApproval(name, validated);
        output = await spec.run(this.client, validated);
        ok = true;
      } catch (e) {
        ok = false;
        error = e instanceof Error ? e.message : String(e);
      }
    }
    feedback = ok ? output : { error };
    emit({ type: "tool_result", toolUseId: call.id, ok, output, error });
    // The model needs this fed back as a 'tool' message before it can continue.
    this.messages.push({ role: "tool", tool_call_id: call.id, content: JSON.stringify(feedback) });
  }
}

function parseArgs(raw: string): Record<string, unknown> {
  if (!raw) return {};
  try {
    return JSON.parse(raw);
  } catch {
    throw new Error(`tool call arguments were not valid JSON: ${raw}`);
  }
}
