#include "util/midi_util.h"

#include <gtest/gtest.h>

using ReaClaw::midi_util::pitch_to_note_name;

TEST(MidiUtil, MiddleCIsC4) {
    EXPECT_EQ(pitch_to_note_name(60), "C4");
}

TEST(MidiUtil, A4Is69) {
    EXPECT_EQ(pitch_to_note_name(69), "A4");
}

TEST(MidiUtil, LowestNoteIsC_neg1) {
    EXPECT_EQ(pitch_to_note_name(0), "C-1");
}

TEST(MidiUtil, HighestNoteIsG9) {
    EXPECT_EQ(pitch_to_note_name(127), "G9");
}

TEST(MidiUtil, OutOfRangeIsEmpty) {
    EXPECT_EQ(pitch_to_note_name(-1), "");
    EXPECT_EQ(pitch_to_note_name(128), "");
}

TEST(MidiUtil, SharpNotes) {
    EXPECT_EQ(pitch_to_note_name(61), "C#4");   // C#4 / Db4
    EXPECT_EQ(pitch_to_note_name(63), "D#4");
    EXPECT_EQ(pitch_to_note_name(70), "A#4");
}

TEST(MidiUtil, OctaveBoundaries) {
    EXPECT_EQ(pitch_to_note_name(12), "C0");   // one octave above C-1
    EXPECT_EQ(pitch_to_note_name(24), "C1");
    EXPECT_EQ(pitch_to_note_name(48), "C3");
    EXPECT_EQ(pitch_to_note_name(72), "C5");
}
