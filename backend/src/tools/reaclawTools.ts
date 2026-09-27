// The chat's only capability surface: a small, curated set of ReaClaw REST
// endpoints, wrapped as tools. TECH_DECISIONS §28: read tools run freely,
// tools that change the project ask first -- `mutates` is what the approval
// broker (approvals.ts) keys on, so a tool's danger level lives in exactly
// one place instead of being duplicated between "what it does" and "whether
// to ask".
import { z } from "zod";
import type { ReaClawClient } from "../reaclawClient.js";

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
];

export function findTool(name: string): ToolSpec | undefined {
  return REACLAW_TOOLS.find((t) => t.name === name);
}
