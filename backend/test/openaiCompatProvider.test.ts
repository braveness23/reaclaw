import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { ApprovalBroker } from "../src/approvals.js";
import { ReaClawClient } from "../src/reaclawClient.js";
import { OpenAiCompatProvider } from "../src/providers/openaiCompat.js";
import type { ChatEvent } from "../src/providers/types.js";
import { startStubReaClaw, type StubReaClaw } from "./testReaclaw.js";
import { startStubOpenAi, type StubOpenAi } from "./testOpenAiServer.js";

describe("OpenAiCompatProvider", () => {
  let reaclaw: StubReaClaw;
  let client: ReaClawClient;

  beforeEach(async () => {
    reaclaw = await startStubReaClaw();
    client = new ReaClawClient({ reaclawBase: reaclaw.base, reaclawKey: "k", reaclawInsecureTls: false });
  });
  afterEach(() => reaclaw.close());

  it("answers directly with no tool call when the model doesn't ask for one", async () => {
    const ai = await startStubOpenAi([{ role: "assistant", content: "hello there" }]);
    const broker = new ApprovalBroker(() => {
      throw new Error("should not ask for approval");
    });
    const provider = new OpenAiCompatProvider(ai.base, "key", "test-model", broker, client);
    const events: ChatEvent[] = [];
    await provider.sendMessage("hi", (e) => events.push(e));
    await ai.close();

    expect(events).toEqual([{ type: "assistant_text", text: "hello there" }, { type: "turn_done" }]);
    expect(ai.requests[0].messages.at(-1)).toEqual({ role: "user", content: "hi" });
    expect(ai.requests[0].tools.map((t: any) => t.function.name)).toContain("get_tracks");
  });

  it("runs a read-only tool call without asking for approval, then continues the turn", async () => {
    const ai = await startStubOpenAi([
      {
        role: "assistant",
        content: null,
        tool_calls: [{ id: "call_1", type: "function", function: { name: "get_tracks", arguments: "{}" } }],
      },
      { role: "assistant", content: "there is one track" },
    ]);
    const broker = new ApprovalBroker(() => {
      throw new Error("get_tracks is read-only, should never ask");
    });
    const provider = new OpenAiCompatProvider(ai.base, "key", "test-model", broker, client);
    const events: ChatEvent[] = [];
    await provider.sendMessage("list tracks", (e) => events.push(e));
    await ai.close();

    expect(events.map((e) => e.type)).toEqual(["tool_call", "tool_result", "assistant_text", "turn_done"]);
    // Second request must carry the assistant's tool_calls message and the tool result.
    const secondRequestMessages = ai.requests[1].messages;
    expect(secondRequestMessages.at(-2)).toMatchObject({ role: "assistant", tool_calls: expect.any(Array) });
    expect(secondRequestMessages.at(-1)).toMatchObject({ role: "tool", tool_call_id: "call_1" });
  });

  it("blocks a mutating tool call on approval and only calls ReaClaw once approved", async () => {
    const ai = await startStubOpenAi([
      {
        role: "assistant",
        content: null,
        tool_calls: [
          {
            id: "call_2",
            type: "function",
            function: { name: "set_track", arguments: JSON.stringify({ index: 0, muted: true }) },
          },
        ],
      },
      { role: "assistant", content: "done" },
    ]);
    const broker = new ApprovalBroker((req) => broker.resolve(req.id, "allow_once"));
    const provider = new OpenAiCompatProvider(ai.base, "key", "test-model", broker, client);
    const events: ChatEvent[] = [];
    await provider.sendMessage("mute track 0", (e) => events.push(e));
    await ai.close();

    expect(reaclaw.calls).toContainEqual({ method: "POST", path: "/state/tracks/0", body: { muted: true } });
    const result = events.find((e) => e.type === "tool_result") as any;
    expect(result.ok).toBe(true);
  });

  it("feeds a denial back to the model as a tool error, without calling ReaClaw", async () => {
    const ai = await startStubOpenAi([
      {
        role: "assistant",
        content: null,
        tool_calls: [
          { id: "call_3", type: "function", function: { name: "execute_action", arguments: '{"id":40001}' } },
        ],
      },
      { role: "assistant", content: "okay, not doing that" },
    ]);
    const broker = new ApprovalBroker((req) => broker.resolve(req.id, "deny"));
    const provider = new OpenAiCompatProvider(ai.base, "key", "test-model", broker, client);
    const events: ChatEvent[] = [];
    await provider.sendMessage("run action 40001", (e) => events.push(e));
    await ai.close();

    expect(reaclaw.calls).toHaveLength(0);
    const result = events.find((e) => e.type === "tool_result") as any;
    expect(result.ok).toBe(false);
    expect(result.error).toMatch(/not approved/);
    const toolMessage = ai.requests[1].messages.at(-1);
    expect(toolMessage.role).toBe("tool");
    expect(JSON.parse(toolMessage.content).error).toMatch(/not approved/);
  });

  it("rejects wrongly-typed arguments instead of forwarding them to ReaClaw (confirmed live: llama3.2:3b sends strings)", async () => {
    const ai = await startStubOpenAi([
      {
        role: "assistant",
        content: null,
        tool_calls: [
          {
            id: "call_bad_types",
            type: "function",
            function: { name: "set_track", arguments: JSON.stringify({ index: "0", muted: "true" }) },
          },
        ],
      },
      { role: "assistant", content: "oops" },
    ]);
    const broker = new ApprovalBroker(() => {
      throw new Error("should fail validation before ever asking for approval");
    });
    const provider = new OpenAiCompatProvider(ai.base, "key", "test-model", broker, client);
    const events: ChatEvent[] = [];
    await provider.sendMessage("mute track 0", (e) => events.push(e));
    await ai.close();

    expect(reaclaw.calls).toHaveLength(0);
    const result = events.find((e) => e.type === "tool_result") as any;
    expect(result.ok).toBe(false);
    expect(result.error).toMatch(/invalid arguments/);
  });

  it("stops and reports an error if the model calls tools past the round limit", async () => {
    const alwaysCallsATool = {
      role: "assistant",
      content: null,
      tool_calls: [{ id: "loop", type: "function", function: { name: "get_tracks", arguments: "{}" } }],
    };
    const ai = await startStubOpenAi([alwaysCallsATool]); // repeats forever, per startStubOpenAi's contract
    const broker = new ApprovalBroker(() => {});
    const provider = new OpenAiCompatProvider(ai.base, "key", "test-model", broker, client);
    const events: ChatEvent[] = [];
    await provider.sendMessage("go", (e) => events.push(e));
    await ai.close();

    expect(events.at(-1)).toMatchObject({ type: "error" });
    expect((events.at(-1) as any).message).toMatch(/tool-call rounds/);
  });

  it("carries conversation history across separate sendMessage calls", async () => {
    const ai = await startStubOpenAi([
      { role: "assistant", content: "first" },
      { role: "assistant", content: "second" },
    ]);
    const broker = new ApprovalBroker(() => {});
    const provider = new OpenAiCompatProvider(ai.base, "key", "test-model", broker, client);
    await provider.sendMessage("one", () => {});
    await provider.sendMessage("two", () => {});
    await ai.close();

    const secondRequestMessages = ai.requests[1].messages;
    expect(secondRequestMessages.some((m: any) => m.role === "user" && m.content === "one")).toBe(true);
    expect(secondRequestMessages.some((m: any) => m.role === "assistant" && m.content === "first")).toBe(true);
    expect(secondRequestMessages.at(-1)).toEqual({ role: "user", content: "two" });
  });
});
