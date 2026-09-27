import { describe, expect, it, vi } from "vitest";
import { ApprovalBroker, ApprovalDenied } from "../src/approvals.js";

describe("ApprovalBroker", () => {
  it("resolves once the matching id is approved", async () => {
    const onRequest = vi.fn((req: { id: string }) => {
      // Simulate the UI responding asynchronously.
      queueMicrotask(() => broker.resolve(req.id, "allow_once"));
    });
    const broker = new ApprovalBroker(onRequest);
    await expect(broker.requestApproval("set_track", { index: 0 })).resolves.toBeUndefined();
    expect(onRequest).toHaveBeenCalledWith(
      expect.objectContaining({ toolName: "set_track", input: { index: 0 } }),
    );
  });

  it("throws ApprovalDenied when the human denies", async () => {
    const broker = new ApprovalBroker((req) => broker.resolve(req.id, "deny"));
    await expect(broker.requestApproval("execute_action", { id: 40001 })).rejects.toThrow(
      ApprovalDenied,
    );
  });

  it("remembers 'allow_always' and skips future prompts for that tool", async () => {
    let requests = 0;
    const broker = new ApprovalBroker((req) => {
      requests++;
      broker.resolve(req.id, "allow_always");
    });
    await broker.requestApproval("set_track", { index: 0 });
    await broker.requestApproval("set_track", { index: 1 });
    expect(requests).toBe(1);
  });

  it("denies everything still pending when the session ends", async () => {
    const broker = new ApprovalBroker(() => {});
    const pending = broker.requestApproval("execute_action", { id: 1 });
    broker.denyAllPending();
    await expect(pending).rejects.toThrow(ApprovalDenied);
  });

  it("resolve() on an unknown id is a no-op, not a throw", () => {
    const broker = new ApprovalBroker(() => {});
    expect(broker.resolve("does-not-exist", "deny")).toBe(false);
  });
});
