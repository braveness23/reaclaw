# tools/

## Native action catalog

REAPER's **native (built-in) actions** cannot be enumerated through the public
SDK: `kbd_getTextFromCmd()` and `kbd_enumerateActions()` only return actions
registered through the plugin/gaccel API (SWS, ReaPack, ReaScripts, …). To make
native actions name-searchable, ReaClaw bundles a static ID→name table.

- **`native_actions.tsv`** — the data: `<command_id>\t<name>`, one native action
  per line (lines starting with `#` are comments). Native command IDs are stable
  across REAPER versions and platforms, so a single table serves all builds.
- **`gen_native_actions.py`** — generates `src/reaper/native_actions.gen.h`
  (compiled into the plugin) from the TSV.

### Regenerating the data (`native_actions.tsv`)

The TSV was produced from a running REAPER (7.74) using the SWS extension's
`CF_EnumerateActions`, classifying each action as native when
`ReverseNamedCommandLookup()` returns empty (i.e. it is not a named
extension/script command). To refresh for a newer REAPER:

1. Enable SWS, then register + run this ReaScript via ReaClaw (or REAPER):
   ```lua
   local f = io.open("/tmp/reaper_actions_dump.tsv", "w")
   local idx = 0
   while true do
     local cmd, name = reaper.CF_EnumerateActions(0, idx, "")
     if cmd == 0 then break end
     f:write(cmd.."\t"..(reaper.ReverseNamedCommandLookup(cmd) or "").."\t"..(name or "").."\n")
     idx = idx + 1
   end
   f:close()
   ```
2. Keep rows whose middle column is empty (native), write `<id>\t<name>` sorted
   by id into `native_actions.tsv`.

### Regenerating the header

```sh
python3 tools/gen_native_actions.py
```

Run after any change to `native_actions.tsv`; commit the regenerated
`src/reaper/native_actions.gen.h`.
