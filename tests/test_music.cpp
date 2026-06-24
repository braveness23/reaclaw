#include "util/music.h"

#include <array>
#include <cmath>

#include <gtest/gtest.h>

using ReaClaw::music::estimate_key;
using ReaClaw::music::hz_to_note;
using ReaClaw::music::Key;
using ReaClaw::music::Note;

TEST(Music, A440IsA4) {
    Note n = hz_to_note(440.0);
    EXPECT_EQ(n.name, "A");
    EXPECT_EQ(n.octave, 4);
    EXPECT_EQ(n.midi, 69);
    EXPECT_NEAR(n.cents, 0.0, 1e-6);
}

TEST(Music, MiddleCIsC4) {
    Note n = hz_to_note(261.6256);  // C4
    EXPECT_EQ(n.name, "C");
    EXPECT_EQ(n.octave, 4);
    EXPECT_EQ(n.midi, 60);
    EXPECT_NEAR(n.cents, 0.0, 0.5);
}

TEST(Music, SlightlySharpReportsPositiveCents) {
    Note n = hz_to_note(445.0);  // a touch above A4
    EXPECT_EQ(n.name, "A");
    EXPECT_GT(n.cents, 0.0);
    EXPECT_LT(n.cents, 50.0);
}

TEST(Music, NonPositiveHzIsSafe) {
    Note n = hz_to_note(0.0);
    EXPECT_EQ(n.hz, 0.0);  // no crash, no NaN
}

TEST(Music, KrumhanslMajorProfileScoresItsOwnKey) {
    // Feed the C-major profile itself — the estimator must return C major.
    std::array<double, 12> cmajor = {
            6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    Key k = estimate_key(cmajor);
    ASSERT_TRUE(k.ok);
    EXPECT_EQ(k.tonic, "C");
    EXPECT_EQ(k.mode, "major");
    EXPECT_GT(k.correlation, 0.99);
}

TEST(Music, KeyRotatesWithChroma) {
    // The A-minor profile must resolve to A minor.
    std::array<double, 12> aminor_profile = {
            6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    // Rotate so the tonic sits at pitch-class A (=9).
    std::array<double, 12> chroma{};
    for (int i = 0; i < 12; i++)
        chroma[(i + 9) % 12] = aminor_profile[i];
    Key k = estimate_key(chroma);
    ASSERT_TRUE(k.ok);
    EXPECT_EQ(k.tonic, "A");
    EXPECT_EQ(k.mode, "minor");
}

TEST(Music, SilentChromaIsNotOk) {
    std::array<double, 12> zero{};
    Key k = estimate_key(zero);
    EXPECT_FALSE(k.ok);
}
