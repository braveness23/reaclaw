// Converts a tool's zod shape into an OpenAI-style function-calling schema.
// zod ships its own JSON Schema converter (z.toJSONSchema, stable since 3.24)
// -- no need for a separate zod-to-json-schema dependency for the handful of
// primitive types ReaClaw's tools actually use.
import { z } from "zod";
import type { ToolSpec } from "./reaclawTools.js";

export interface OpenAiFunctionTool {
  type: "function";
  function: {
    name: string;
    description: string;
    parameters: Record<string, unknown>;
  };
}

export function toOpenAiTool(spec: ToolSpec): OpenAiFunctionTool {
  const schema = z.toJSONSchema(z.object(spec.schema)) as Record<string, unknown>;
  delete schema.$schema; // OpenAI's schema has no place for this key
  return {
    type: "function",
    function: { name: spec.name, description: spec.description, parameters: schema },
  };
}
