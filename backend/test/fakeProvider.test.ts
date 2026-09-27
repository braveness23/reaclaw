import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { ApprovalBroker } from "../src/approvals.js";
import { ReaClawClient } from "../src/reaclawClient.js";
import { FakeProvider } from "../src/providers/fake.js";
import type { ChatEvent } from "../src/providers/types.js";
import { startStubReaClaw, type StubReaClaw } from "./testReaclaw.js";

describe("FakeProvider", () => {
  let stub: StubReaClaw;
  let client: ReaClawClient;

  beforeEach(async () => {
    stub = await startStubReaClaw();
    client = new ReaClawClient({ reaclawBase: stub.base, reaclawKey: "k", reaclawInsecureTls: false });
  });
  afterEach(() => stub.close());

  it("runs a read-only tool without asking for approval", async () => {
    const broker = new ApprovalBroker(() => {
      throw new Error("should never ask for a read-only tool");
    });
    const provider = new FakeProvider(broker, client);
    const events: ChatEvent[] = [];
    await provider.sendMessage("list tracks", (e) => events.push(e));

    expect(events.map((e) => e.type)).toEqual(["tool_call", "tool_result", "turn_done"]);
    const result = events[1] as Extract<ChatEvent, { type: "tool_result" }>;
    expect(result.ok).toBe(true);
    expect(result.output).toEqual({ tracks: [{ index: 0, name: "Kick", volume_db: -7.2 }] });
  });

  it("blocks a mutating tool on approval, and proceeds once approved", async () => {
    const broker = new ApprovalBroker((req) => {
      expect(req.toolName).toBe("set_track");
      broker.resolve(req.id, "allow_once");
    });
    const provider = new FakeProvider(broker, client);
    const events: ChatEvent[] = [];
    await provider.sendMessage("mute track 3", (e) => events.push(e));

    expect(stub.calls).toContainEqual({
      method: "POST",
      path: "/state/tracks/3",
      body: { muted: true },
    });
    const result = events.find((e) => e.type === "tool_result") as Extract<
      ChatEvent,
      { type: "tool_result" }
    >;
    expect(result.ok).toBe(true);
  });

  it("never calls ReaClaw when the human denies", async () => {
    const broker = new ApprovalBroker((req) => broker.resolve(req.id, "deny"));
    const provider = new FakeProvider(broker, client);
    const events: ChatEvent[] = [];
    await provider.sendMessage("mute track 3", (e) => events.push(e));

    expect(stub.calls).toHaveLength(0);
    const result = events.find((e) => e.type === "tool_result") as Extract<
      ChatEvent,
      { type: "tool_result" }
    >;
    expect(result.ok).toBe(false);
    expect(result.error).toMatch(/not approved/);
  });

  it("falls back to a plain echo when nothing matches its tiny vocabulary", async () => {
    const broker = new ApprovalBroker(() => {
      throw new Error("no tool should be invoked");
    });
    const provider = new FakeProvider(broker, client);
    const events: ChatEvent[] = [];
    await provider.sendMessage("what's the weather", (e) => events.push(e));

    expect(events).toEqual([
      { type: "assistant_text", text: "(fake provider) heard: what's the weather" },
      { type: "turn_done" },
    ]);
  });
});
