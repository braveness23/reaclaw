// The chat's only capability surface: a small, curated set of ReaClaw REST
// endpoints, wrapped as tools. TECH_DECISIONS §28: read tools run freely,
// tools that change the project ask first -- `mutates` is what the approval
// broker (approvals.ts) keys on, so a tool's danger level lives in exactly
// one place instead of being duplicated between "what it does" and "whether
// to ask".
import { z } from "zod";
import type { ReaClawClient } from "../reaclawClient.js";
import { writeSeedMidiFile } from "./seedMidiFile.js";

export interface ToolSpec<Args = any> {
  name: string;
  description: string;
  /** Zod raw shape (plain object of zod types), matching the SDK's `tool()` signature. */
  schema: z.ZodRawShape;
  mutates: boolean;
  run: (client: ReaClawClient, args: Args) => Promise<unknown>;
}

// Kept deliberately small for this first pass: enough to prove the read/
// mutate split end to end (list tracks, tweak one, run an arbitrary action)
// without trying to wrap ReaClaw's full ~90-endpoint surface yet.
export const REACLAW_TOOLS: ToolSpec[] = [
  {
    name: "get_tracks",
    description: "List every track in the current REAPER project (index, name, volume, pan, mute/solo state).",
    schema: {},
    mutates: false,
    run: (client) => client.get("/state/tracks"),
  },
  {
    name: "get_track",
    description: "Get one track's full state by index (0-based).",
    schema: { index: z.number().int().min(0) },
    mutates: false,
    // No single-track GET exists in ReaClaw's API (only the /state/tracks
    // collection, docs/API.md#get-statetracks) -- filter client-side so the
    // model still gets a "just this one track" tool without a wrong request.
    run: async (client, args: { index: number }) => {
      const { tracks } = (await client.get("/state/tracks")) as {
        tracks: Array<{ index: number }>;
      };
      const track = tracks.find((t) => t.index === args.index);
      if (!track) throw new Error(`no track at index ${args.index}`);
      return track;
    },
  },
  {
    name: "set_track",
    description:
      "Change a track's volume (dB), pan (-1..1), mute or name. Only pass the fields you want to change.",
    schema: {
      index: z.number().int().min(0),
      volume_db: z.number().optional(),
      pan: z.number().min(-1).max(1).optional(),
      muted: z.boolean().optional(),
      name: z.string().optional(),
    },
    mutates: true,
    run: (client, args: { index: number; [k: string]: unknown }) => {
      const { index, ...body } = args;
      return client.post(`/state/tracks/${index}`, body);
    },
  },
  {
    name: "execute_action",
    description:
      "Run a REAPER action by its command ID (integer native ID, or a named/string ID for scripts). " +
      "Use only when no more specific tool covers what's being asked -- this can do anything REAPER's " +
      "action list can do.",
    schema: { id: z.union([z.number().int(), z.string()]) },
    mutates: true,
    run: (client, args: { id: number | string }) =>
      client.post("/execute/action", { id: args.id }),
  },
  {
    name: "create_tracks",
    description: "Create one or more new tracks, in order, with the given names.",
    schema: { names: z.array(z.string()).min(1) },
    mutates: true,
    run: (client, args: { names: string[] }) =>
      client.post("/state/tracks", { create: args.names.map((name) => ({ name })) }),
  },
  {
    name: "add_fx",
    description:
      "Add an FX or virtual instrument to a track by name (e.g. \"ReaSynth\", \"ReaComp\", \"ReaEQ\" -- " +
      "REAPER's own bundled plugins are always available; other names must already be installed). " +
      "This cannot install a plugin that isn't already on the machine.",
    schema: { track: z.number().int().min(0), name: z.string() },
    mutates: true,
    run: (client, args: { track: number; name: string }) =>
      client.post(`/state/tracks/${args.track}/fx`, { name: args.name }),
  },
  {
    name: "set_fx_param",
    description:
      "Set one parameter (by name) on an FX already added to a track. Value is normalized 0..1 " +
      "(0 = the parameter's minimum, 1 = its maximum), not the real unit.",
    schema: {
      track: z.number().int().min(0),
      slot: z.number().int().min(0),
      param: z.string(),
      value: z.number().min(0).max(1),
    },
    mutates: true,
    run: (client, args: { track: number; slot: number; param: string; value: number }) =>
      client.post(`/state/tracks/${args.track}/fx/${args.slot}`, {
        params: [{ name: args.param, value: args.value }],
      }),
  },
  {
    name: "create_midi_item",
    description:
      "Create an empty MIDI item on a track, ready for insert_midi_notes. `position` and `length` are " +
      "in seconds.",
    schema: {
      track: z.number().int().min(0),
      position: z.number().min(0).default(0),
      length: z.number().min(0.1).default(4),
    },
    mutates: true,
    // ReaClaw's create-item endpoint only gives an item a real MIDI take when
    // loaded from an actual file (an item created with no `file` has
    // take:null and can't take notes -- see seedMidiFile.ts) -- so this
    // seeds one from a tiny throwaway .mid file, confirmed live.
    run: async (client, args: { track: number; position: number; length: number }) => {
      const file = writeSeedMidiFile();
      const result = (await client.post("/state/items", {
        create: [{ track: args.track, position: args.position, length: args.length, file }],
      })) as { created: unknown[] };
      return result.created[0];
    },
  },
  {
    name: "insert_midi_notes",
    description:
      "Add notes to a MIDI item (from create_midi_item). Timing is in quarter notes from the start of " +
      "the item, independent of tempo -- start_quarter 0 is the item's first beat, 1.0 is the next beat, " +
      "etc. `replace: true` clears any existing notes first (create_midi_item's placeholder note counts).",
    schema: {
      item: z.number().int().min(0),
      notes: z
        .array(
          z.object({
            pitch: z.number().int().min(0).max(127),
            start_quarter: z.number().min(0),
            length_quarter: z.number().min(0.0625).default(1),
            velocity: z.number().int().min(1).max(127).default(100),
          }),
        )
        .min(1),
      replace: z.boolean().default(false),
    },
    mutates: true,
    run: (
      client,
      args: {
        item: number;
        notes: Array<{ pitch: number; start_quarter: number; length_quarter: number; velocity: number }>;
        replace: boolean;
      },
    ) =>
      client.post(`/state/items/${args.item}/midi`, {
        replace: args.replace,
        notes: args.notes.map((n) => ({
          pitch: n.pitch,
          velocity: n.velocity,
          start_ppq: n.start_quarter * 480,
          end_ppq: (n.start_quarter + n.length_quarter) * 480,
        })),
      }),
  },
  {
    name: "get_transport",
    description: "Get the current transport state: playing/paused/recording, position, loop range.",
    schema: {},
    mutates: false,
    run: (client) => client.get("/transport"),
  },
  {
    name: "transport",
    description: "Start or stop playback or recording.",
    schema: { action: z.enum(["play", "stop", "pause", "record"]) },
    mutates: true,
    run: (client, args: { action: "play" | "stop" | "pause" | "record" }) =>
      client.post("/transport", { action: args.action }),
  },
  {
    name: "set_loop",
    description: "Set the loop/time-selection range and/or turn looping on or off. All fields optional.",
    schema: {
      start: z.number().min(0).optional(),
      end: z.number().min(0).optional(),
      enabled: z.boolean().optional(),
    },
    mutates: true,
    run: (client, args: Record<string, unknown>) => client.post("/transport/loop", args),
  },
  {
    name: "render",
    description:
      "Render (bounce) the project to an audio file. Always renders the whole project offline -- fast, " +
      "no audio hardware needed, and doesn't affect playback.",
    schema: {
      output: z.string(),
      format: z.enum(["wav", "flac", "mp3", "ogg"]).default("wav"),
    },
    mutates: true,
    run: (client, args: { output: string; format: string }) => client.post("/render", args),
  },
];

export function findTool(name: string): ToolSpec | undefined {
  return REACLAW_TOOLS.find((t) => t.name === name);
}

// Some models (small local ones especially) don't reliably respect a tool's
// declared JSON types -- confirmed live: llama3.2:3b called set_track with
// {index:"0", muted:"true"} (strings). zod's plain .parse() does NOT coerce
// those, so this rejects them with a clear message the model can act on,
// instead of letting a wrong-shaped value reach ReaClaw's API, where it can
// be silently ignored (muted:"true" satisfies no validation there and the
// track is left unchanged, while the tool call still reports success).
export function validateToolArgs(spec: ToolSpec, rawArgs: unknown): Record<string, unknown> {
  const result = z.object(spec.schema).safeParse(rawArgs);
  if (!result.success) {
    throw new Error(`invalid arguments for ${spec.name}: ${result.error.message}`);
  }
  return result.data;
}
