# Network Audio / Remote Plugin Hosting — Notes for ReaClaw

Context dump from a claude.ai chat (2026-07-02), exploring whether ReaClaw should
grow network-audio capability. Not scoped as committed work — this is background
for whoever (Claude Code included) picks up the idea next.

## Starting point: what REAPER already has

- **ReaStream** — free JSFX/VST2 plugin bundled in ReaPlugs. Streams audio and/or
  MIDI between REAPER instances (or any VST host) over **UDP**, not TCP. Point-to-
  point (IP-targeted) or UDP broadcast for one-to-many. No encryption, no
  compression, no error correction — protocol is deliberately bare to keep latency
  low. MIDI is sent as standard MIDI events with sample-offset timestamps, no
  SysEx support.
- **ReaRoute** — separate feature, Windows-only ASIO driver for local inter-app
  routing on the same machine. Not networked. Not relevant to the ideas below
  except as a "don't confuse these two" note.

## Idea 1: MIDI/audio ingestion into ReaClaw via the API

Motivating use case: right now if the agent writes a MIDI part via the ReaClaw
API, there's a file round-trip (render, reload) before it can be heard against
the live arrangement. Direct streaming would let the agent propose → hear →
adjust without that round-trip.

Recommended approach, in order of effort:

1. **MIDI, same machine**: write to an OS-level virtual MIDI port (ALSA virtual
   port on Linux). REAPER just sees it as a normal input device. No REAPER-side
   plugin needed. Cheapest win, covers the main use case.
2. **Audio, same machine**: PipeWire/PulseAudio null-sink or ALSA loopback that
   ReaClaw writes PCM into; REAPER track monitors that device as input.
3. **Audio, cross-machine**: ReaStream is UDP with a documented-enough wire
   format (raw MIDI events + sample-offset timestamps, no session negotiation)
   that emulating it directly in Go is plausible — bypasses REAPER-as-relay
   entirely. Alternative: keep ReaStream itself as the transport and have
   ReaClaw configure its JSFX parameters (IP, identifier, enable) via REAPER's
   control surface API rather than reimplementing the UDP protocol. This second
   option keeps the "two-family API" pattern intact — streaming becomes a
   mutation you trigger and get an inline hint back from, not a new protocol
   ReaClaw has to own.

## Idea 2: REAPER in a Kubernetes pod

Feasible, two things to solve that a normal desktop Docker setup doesn't need to:

- **No real display**: REAPER on Linux wants an X display even for headless
  batch work. Standard fix is Xvfb (virtual framebuffer) inside the pod — this
  is the established pattern for CI-style REAPER rendering.
- **No real soundcard**: use a dummy/null JACK or ALSA driver. Since the whole
  point is piping audio over the network rather than to speakers, a dummy
  driver isn't a workaround, it's the correct choice.
- Plugin formats (LV2, VST, VST3, CLAP) are all natively supported on Linux
  REAPER, so the instrument/effect stack isn't Windows-locked.
- Reference: `andreimatveyeu/reaper_docker` on GitHub — dockerized REAPER using
  pw-jack against host PipeWire/JACK, X forwarding for GUI, 70+ bundled
  open-source plugins, Carla included as a wrapper for LADSPA/DSSI formats
  REAPER doesn't natively support.

## Idea 3: Alternatives to REAPER for a stateless "audio routing microservice"

If the goal is just "pod takes MIDI/audio in over network, runs a plugin chain,
streams audio out" — REAPER may be the wrong tool. It earns its place when you
need project/arrangement semantics (tracks, automation, the two-family API
ReaClaw is built around), not for a stateless render node.

- **Carla** — standalone plugin host (VST/VST3/LV2/LADSPA/DSSI), controllable
  via OSC, no DAW required.
- **Sushi** (Elk Audio OS) — headless, real-time-safe plugin host built
  specifically for embedded/server hosting, with gRPC/OSC control. Better
  architectural fit than REAPER for a pure routing/hosting pod.

## Where this fits ReaClaw's actual scope

Explicitly *not* core to ReaClaw's stated purpose (agent-to-REAPER bridge for
action execution, script generation, workflow caching, audit trail). Worth
tracking as a "wishlist" item rather than a phase deliverable unless the
live-audition use case (Idea 1, option 1) proves valuable enough to pull
forward — that one's cheap enough it could just be built.

## Open questions for next session

- Does the live-audition MIDI loop (virtual ALSA port) actually get used, or
  is file-based round-trip fine in practice? Worth prototyping before building
  more.
- If cross-machine audio ever becomes real: emulate ReaStream's UDP protocol
  in Go, or drive REAPER's own ReaStream instance via control surface API?
  Leaning toward the latter to avoid owning a second network protocol.
- Is there an existing `docs/` or wishlist file in `braveness23/reclaw` this
  should be merged into, or does it start as its own file?
