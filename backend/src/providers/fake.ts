// Deterministic stand-in for AnthropicProvider: same Provider interface, same
// tool registry, same approval broker -- just no network call and no API key,
// so tests (and `npm run dev` before you've set ANTHROPIC_API_KEY) can
// exercise the full read/mutate/approval path for free. Understands a tiny
// fixed vocabulary; it is not a language model.
import type { ApprovalBroker } from "../approvals.js";
import type { ReaClawClient } from "../reaclawClient.js";
import { findTool } from "../tools/reaclawTools.js";
import type { ChatEvent, Provider } from "./types.js";

const MUTE_PATTERN = /mute track (\d+)/i;

export class FakeProvider implements Provider {
  private turn = 0;

  constructor(
    private readonly broker: ApprovalBroker,
    private readonly client: ReaClawClient,
  ) {}

  async sendMessage(text: string, emit: (event: ChatEvent) => void): Promise<void> {
    try {
      if (/list tracks/i.test(text)) {
        await this.callTool("get_tracks", {}, emit);
      } else {
        const mute = text.match(MUTE_PATTERN);
        if (mute) {
          await this.callTool("set_track", { index: Number(mute[1]), muted: true }, emit);
        } else {
          emit({ type: "assistant_text", text: `(fake provider) heard: ${text}` });
        }
      }
      emit({ type: "turn_done" });
    } catch (e) {
      emit({ type: "error", message: e instanceof Error ? e.message : String(e) });
    }
  }

  private async callTool(
    name: string,
    input: Record<string, unknown>,
    emit: (event: ChatEvent) => void,
  ): Promise<void> {
    const toolUseId = `fake-${++this.turn}`;
    const spec = findTool(name);
    if (!spec) throw new Error(`unknown tool: ${name}`);
    emit({ type: "tool_call", toolUseId, name, input });
    try {
      if (spec.mutates) await this.broker.requestApproval(name, input);
      const output = await spec.run(this.client, input);
      emit({ type: "tool_result", toolUseId, ok: true, output });
    } catch (e) {
      emit({ type: "tool_result", toolUseId, ok: false, error: e instanceof Error ? e.message : String(e) });
    }
  }
}
