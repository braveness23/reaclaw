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

  it("declares the right read/mutate split for every tool", () => {
    const readOnly = ["get_tracks", "get_track", "get_transport"];
    const mutating = [
      "set_track",
      "execute_action",
      "create_tracks",
      "add_fx",
      "set_fx_param",
      "create_midi_item",
      "insert_midi_notes",
      "transport",
      "set_loop",
      "render",
    ];
    for (const name of readOnly) expect(findTool(name)?.mutates).toBe(false);
    for (const name of mutating) expect(findTool(name)?.mutates).toBe(true);
    expect(REACLAW_TOOLS).toHaveLength(readOnly.length + mutating.length);
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

  it("create_tracks posts one create entry per name, in order", async () => {
    const result: any = await findTool("create_tracks")!.run(client, { names: ["Kick", "Snare"] });
    expect(stub.calls[0]).toMatchObject({
      method: "POST",
      path: "/state/tracks",
      body: { create: [{ name: "Kick" }, { name: "Snare" }] },
    });
    expect(result.created).toHaveLength(2);
  });

  it("add_fx posts the plugin name to the track's FX list", async () => {
    await findTool("add_fx")!.run(client, { track: 2, name: "ReaSynth" });
    expect(stub.calls[0]).toMatchObject({ method: "POST", path: "/state/tracks/2/fx", body: { name: "ReaSynth" } });
  });

  it("add_fx surfaces a not-found plugin as a rejected promise", async () => {
    await expect(findTool("add_fx")!.run(client, { track: 0, name: "NoSuchPlugin" })).rejects.toThrow(/400/);
  });

  it("set_fx_param wraps the name/value pair in ReaClaw's params array shape", async () => {
    await findTool("set_fx_param")!.run(client, { track: 0, slot: 1, param: "Pitch", value: 0.5 });
    expect(stub.calls[0]).toMatchObject({
      method: "POST",
      path: "/state/tracks/0/fx/1",
      body: { params: [{ name: "Pitch", value: 0.5 }] },
    });
  });

  it("create_midi_item seeds a real .mid file so the item gets an actual MIDI take", async () => {
    const item: any = await findTool("create_midi_item")!.run(client, { track: 0, position: 2, length: 4 });
    expect(stub.calls[0].method).toBe("POST");
    expect(stub.calls[0].path).toBe("/state/items");
    const created = (stub.calls[0].body as any).create[0];
    expect(created).toMatchObject({ track: 0, position: 2, length: 4 });
    expect(typeof created.file).toBe("string");
    expect(created.file).toMatch(/\.mid$/);
    expect(item.take).not.toBeNull();
  });

  it("insert_midi_notes converts quarter-note timing to PPQ (480 per quarter)", async () => {
    await findTool("insert_midi_notes")!.run(client, {
      item: 0,
      replace: true,
      notes: [{ pitch: 60, start_quarter: 0, length_quarter: 1, velocity: 100 }],
    });
    expect(stub.calls[0]).toMatchObject({
      method: "POST",
      path: "/state/items/0/midi",
      body: { replace: true, notes: [{ pitch: 60, velocity: 100, start_ppq: 0, end_ppq: 480 }] },
    });
  });

  it("get_transport calls GET /transport", async () => {
    const result: any = await findTool("get_transport")!.run(client, {});
    expect(stub.calls[0]).toMatchObject({ method: "GET", path: "/transport" });
    expect(result).toHaveProperty("playing");
  });

  it("transport posts the requested action", async () => {
    await findTool("transport")!.run(client, { action: "play" });
    expect(stub.calls[0]).toMatchObject({ method: "POST", path: "/transport", body: { action: "play" } });
  });

  it("set_loop only sends the fields it was given", async () => {
    await findTool("set_loop")!.run(client, { enabled: true });
    expect(stub.calls[0]).toMatchObject({ method: "POST", path: "/transport/loop", body: { enabled: true } });
  });

  it("render posts to /render with the output path", async () => {
    const result: any = await findTool("render")!.run(client, { output: "/tmp/song.wav", format: "wav" });
    expect(stub.calls[0]).toMatchObject({ method: "POST", path: "/render", body: { output: "/tmp/song.wav", format: "wav" } });
    expect(result.output_path).toBe("/tmp/song.wav");
  });
});
