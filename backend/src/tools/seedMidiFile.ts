// ReaClaw's item-create endpoint only gets a MIDI take from a real .mid file
// on disk (confirmed live: an item created with no `file` has `take: null`,
// and POST .../midi 404s on it rather than the documented 400 -- there's no
// "create an empty MIDI take" verb). So creating a MIDI item means writing a
// tiny throwaway Standard MIDI File first and loading it via the same `file`
// field the audio-item path uses (same pattern as the trailer work in
// demos/ -- see reaclaw-trailer-and-audio memory). One silent-ish note is
// enough; insert_midi_notes then replaces it with real content.
import { writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { randomUUID } from "node:crypto";

function writeVarLen(value: number): number[] {
  const bytes = [value & 0x7f];
  value >>= 7;
  while (value > 0) {
    bytes.unshift((value & 0x7f) | 0x80);
    value >>= 7;
  }
  return bytes;
}

/** A minimal, valid Format-0 SMF: one track, one short note, PPQ division 480. */
function buildSeedMidi(): Buffer {
  const PPQ = 480;
  const track: number[] = [
    ...writeVarLen(0), 0x90, 60, 1, // note-on C4, velocity 1 (near-silent placeholder)
    ...writeVarLen(PPQ / 4), 0x80, 60, 0, // note-off a sixteenth note later
    ...writeVarLen(0), 0xff, 0x2f, 0x00, // end of track
  ];
  const header = Buffer.from([
    0x4d, 0x54, 0x68, 0x64, // "MThd"
    0x00, 0x00, 0x00, 0x06, // header length
    0x00, 0x00, // format 0
    0x00, 0x01, // one track
    (PPQ >> 8) & 0xff, PPQ & 0xff,
  ]);
  const trackChunk = Buffer.concat([
    Buffer.from([0x4d, 0x54, 0x72, 0x6b]), // "MTrk"
    (() => {
      const len = Buffer.alloc(4);
      len.writeUInt32BE(track.length, 0);
      return len;
    })(),
    Buffer.from(track),
  ]);
  return Buffer.concat([header, trackChunk]);
}

/** Writes a fresh seed file each call and returns its path; ReaClaw reads it once at item-create time. */
export function writeSeedMidiFile(): string {
  const filePath = path.join(tmpdir(), `reaclaw-chat-seed-${randomUUID()}.mid`);
  writeFileSync(filePath, buildSeedMidi());
  return filePath;
}
