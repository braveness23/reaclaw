import { describe, expect, it } from "vitest";
import { findTool } from "../src/tools/reaclawTools.js";
import { toOpenAiTool } from "../src/tools/openaiSchema.js";

describe("toOpenAiTool", () => {
  it("converts a tool with no arguments", () => {
    const tool = toOpenAiTool(findTool("get_tracks")!);
    expect(tool).toEqual({
      type: "function",
      function: {
        name: "get_tracks",
        description: findTool("get_tracks")!.description,
        parameters: { type: "object", properties: {}, additionalProperties: false },
      },
    });
  });

  it("marks only non-optional fields as required", () => {
    const tool = toOpenAiTool(findTool("set_track")!);
    expect(tool.function.parameters.required).toEqual(["index"]);
    expect(tool.function.parameters.properties).toHaveProperty("volume_db");
    expect(tool.function.parameters.properties).toHaveProperty("muted");
  });

  it("converts a union type to anyOf", () => {
    const tool = toOpenAiTool(findTool("execute_action")!);
    const idSchema = (tool.function.parameters.properties as any).id;
    expect(idSchema.anyOf).toHaveLength(2);
  });

  it("never leaks zod's own $schema key into the OpenAI schema", () => {
    const tool = toOpenAiTool(findTool("set_track")!);
    expect(tool.function.parameters).not.toHaveProperty("$schema");
  });
});
