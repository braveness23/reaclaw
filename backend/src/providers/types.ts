// A provider owns one chat session's conversation with a model. Kept
// deliberately narrow (one method) so AnthropicProvider and FakeProvider are
// interchangeable from the server's point of view -- the LiteLLM/OpenRouter
// provider planned for later (TECH_DECISIONS §28) is another implementation
// of this same interface, not a special case.
export type ChatEvent =
  | { type: "assistant_text"; text: string }
  | { type: "tool_call"; toolUseId: string; name: string; input: Record<string, unknown> }
  | { type: "tool_result"; toolUseId: string; ok: boolean; output?: unknown; error?: string }
  | { type: "turn_done" }
  | { type: "error"; message: string };

export interface Provider {
  /** Send one user message and emit events until the turn is over ('turn_done' or 'error'). */
  sendMessage(text: string, emit: (event: ChatEvent) => void): Promise<void>;
}
