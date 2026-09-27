import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { ReaClawClient } from "../src/reaclawClient.js";
import { findTool, REACLAW_TOOLS } from "../src/tools/reaclawTools.js";
import { startStubReaClaw, type StubReaClaw } from "./testReaclaw.js";

describe("reaclawTools", () => {
  let stub: StubReaClaw;
  let client: ReaClawClient;

  beforeEach(async () => {
    stub = await startStubReaClaw();
    client = new ReaClawClient({ reaclawBase: stub.base, reaclawKey: "k", reaclawInsecureTls: false });
  });
  afterEach(() => stub.close());

  it("declares get_tracks and get_track as read-only, set_track and execute_action as mutating", () => {
    expect(findTool("get_tracks")?.mutates).toBe(false);
    expect(findTool("get_track")?.mutates).toBe(false);
    expect(findTool("set_track")?.mutates).toBe(true);
    expect(findTool("execute_action")?.mutates).toBe(true);
    expect(REACLAW_TOOLS).toHaveLength(4);
  });

  it("get_tracks calls GET /state/tracks and returns ReaClaw's body", async () => {
    const result = await findTool("get_tracks")!.run(client, {});
    expect(result).toEqual({ tracks: [{ index: 0, name: "Kick", volume_db: -7.2 }] });
    expect(stub.calls[0]).toMatchObject({ method: "GET", path: "/state/tracks" });
  });

  it("get_track filters the /state/tracks collection client-side (no per-track GET exists)", async () => {
    const track = await findTool("get_track")!.run(client, { index: 0 });
    expect(track).toEqual({ index: 0, name: "Kick", volume_db: -7.2 });
    expect(stub.calls[0]).toMatchObject({ method: "GET", path: "/state/tracks" });
  });

  it("get_track throws for an index that doesn't exist", async () => {
    await expect(findTool("get_track")!.run(client, { index: 99 })).rejects.toThrow(/no track/);
  });

  it("set_track posts only the fields it was given, alongside the index in the path", async () => {
    await findTool("set_track")!.run(client, { index: 2, volume_db: -4.2 });
    expect(stub.calls[0]).toMatchObject({
      method: "POST",
      path: "/state/tracks/2",
      body: { volume_db: -4.2 },
    });
  });

  it("execute_action posts the given id verbatim", async () => {
    await findTool("execute_action")!.run(client, { id: 40001 });
    expect(stub.calls[0]).toMatchObject({ method: "POST", path: "/execute/action", body: { id: 40001 } });
  });

  it("surfaces a non-2xx ReaClaw response as a rejected promise", async () => {
    await expect(findTool("execute_action")!.run(client, { id: 1 })).rejects.toThrow(/400/);
  });
});
