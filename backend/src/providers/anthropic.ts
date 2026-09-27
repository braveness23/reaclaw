// Anthropic leg of the chat (TECH_DECISIONS §28): the Claude Agent SDK, API
// key only -- no claude.ai subscription login (the SDK's terms don't allow
// third-party products to offer that without Anthropic's approval; requested,
// not yet granted). The SDK's own built-in Bash/file tools are switched off
// (`tools: []`) so the model can only act through ReaClaw's REST API via the
// in-process MCP server registered below.
import {
  createSdkMcpServer,
  query,
  tool,
  type CanUseTool,
  type SDKMessage,
} from "@anthropic-ai/claude-agent-sdk";
import type { ApprovalBroker } from "../approvals.js";
import type { ReaClawClient } from "../reaclawClient.js";
import { findTool, REACLAW_TOOLS } from "../tools/reaclawTools.js";
import type { ChatEvent, Provider } from "./types.js";

function buildMcpServer(client: ReaClawClient) {
  const tools = REACLAW_TOOLS.map((spec) =>
    tool(spec.name, spec.description, spec.schema, async (args) => {
      const result = await spec.run(client, args);
      return { content: [{ type: "text" as const, text: JSON.stringify(result) }] };
    }),
  );
  return createSdkMcpServer({ name: "reaclaw", version: "0.1.0", tools });
}

// mcp__<server>__<tool> is the SDK's own naming for in-process MCP tools.
function bareToolName(mcpQualifiedName: string): string {
  const parts = mcpQualifiedName.split("__");
  return parts[parts.length - 1] ?? mcpQualifiedName;
}

function safeJsonParse(text: string): unknown {
  try {
    return JSON.parse(text);
  } catch {
    return text;
  }
}

export class AnthropicProvider implements Provider {
  private sessionId: string | undefined;
  private readonly mcpServer: ReturnType<typeof createSdkMcpServer>;

  constructor(
    private readonly apiKey: string,
    private readonly broker: ApprovalBroker,
    client: ReaClawClient,
    private readonly model?: string,
  ) {
    this.mcpServer = buildMcpServer(client);
  }

  private canUseTool: CanUseTool = async (toolName, input) => {
    const bare = bareToolName(toolName);
    const spec = findTool(bare);
    if (!spec?.mutates) return { behavior: "allow", updatedInput: input };
    try {
      await this.broker.requestApproval(bare, input);
      return { behavior: "allow", updatedInput: input };
    } catch (e) {
      return { behavior: "deny", message: e instanceof Error ? e.message : String(e) };
    }
  };

  async sendMessage(text: string, emit: (event: ChatEvent) => void): Promise<void> {
    try {
      const stream = query({
        prompt: text,
        options: {
          model: this.model,
          resume: this.sessionId,
          env: { ANTHROPIC_API_KEY: this.apiKey },
          tools: [], // no Bash/Read/Write/etc. -- ReaClaw's tools only
          mcpServers: { reaclaw: this.mcpServer },
          canUseTool: this.canUseTool,
          permissionMode: "default",
        },
      });

      for await (const message of stream as AsyncGenerator<SDKMessage>) {
        this.handleMessage(message, emit);
      }
      emit({ type: "turn_done" });
    } catch (e) {
      emit({ type: "error", message: e instanceof Error ? e.message : String(e) });
    }
  }

  private handleMessage(message: SDKMessage, emit: (event: ChatEvent) => void): void {
    switch (message.type) {
      case "system":
        if (message.subtype === "init") this.sessionId = message.session_id;
        break;
      case "assistant":
        for (const block of message.message.content) {
          if (block.type === "text") {
            emit({ type: "assistant_text", text: block.text });
          } else if (block.type === "tool_use") {
            emit({
              type: "tool_call",
              toolUseId: block.id,
              name: bareToolName(block.name),
              input: block.input as Record<string, unknown>,
            });
          }
        }
        break;
      case "user": {
        const content = message.message.content;
        if (Array.isArray(content)) {
          for (const block of content) {
            if (block.type === "tool_result") {
              const text = Array.isArray(block.content)
                ? block.content.map((c: any) => ("text" in c ? c.text : "")).join("")
                : String(block.content ?? "");
              emit({
                type: "tool_result",
                toolUseId: block.tool_use_id,
                ok: !block.is_error,
                output: block.is_error ? undefined : safeJsonParse(text),
                error: block.is_error ? text : undefined,
              });
            }
          }
        }
        break;
      }
      case "result":
        this.sessionId = message.session_id;
        if (message.subtype !== "success") {
          emit({ type: "error", message: `turn ended: ${message.subtype}` });
        }
        break;
      default:
        break;
    }
  }
}
