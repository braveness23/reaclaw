#pragma once
#include <string>

namespace ReaClaw::midi_util {

// Convert a MIDI pitch number (0-127) to its standard note name.
// Pitch 60 = C4 (middle C), pitch 69 = A4 (440 Hz).
// Returns an empty string for out-of-range inputs.
inline std::string pitch_to_note_name(int pitch) {
    static const char* const names[] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (pitch < 0 || pitch > 127)
        return "";
    return std::string(names[pitch % 12]) + std::to_string(pitch / 12 - 1);
}

}  // namespace ReaClaw::midi_util
