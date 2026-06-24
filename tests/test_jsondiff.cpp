#include "util/jsondiff.h"

#include <gtest/gtest.h>

using nlohmann::json;
using ReaClaw::jsondiff::diff;

TEST(JsonDiff, IdenticalIsEmpty) {
    json a = {{"x", 1}, {"y", {1, 2, 3}}};
    EXPECT_TRUE(diff(a, a).empty());
}

TEST(JsonDiff, ChangedScalarCarriesFromTo) {
    json a = {{"volume_db", -6.0}};
    json b = {{"volume_db", -3.0}};
    auto d = diff(a, b);
    ASSERT_EQ(d.size(), 1u);
    EXPECT_EQ(d[0]["path"], "volume_db");
    EXPECT_EQ(d[0]["op"], "changed");
    EXPECT_EQ(d[0]["from"], -6.0);
    EXPECT_EQ(d[0]["to"], -3.0);
}

TEST(JsonDiff, AddedAndRemovedKeys) {
    json a = {{"keep", 1}, {"gone", 2}};
    json b = {{"keep", 1}, {"fresh", 3}};
    auto d = diff(a, b);
    ASSERT_EQ(d.size(), 2u);
    // a-first ordering: removed then added
    EXPECT_EQ(d[0]["op"], "removed");
    EXPECT_EQ(d[0]["path"], "gone");
    EXPECT_EQ(d[1]["op"], "added");
    EXPECT_EQ(d[1]["path"], "fresh");
    EXPECT_EQ(d[1]["to"], 3);
}

TEST(JsonDiff, NestedPathJoins) {
    json a = {{"tracks", {{{"name", "Kick"}, {"muted", false}}}}};
    json b = {{"tracks", {{{"name", "Kick"}, {"muted", true}}}}};
    auto d = diff(a, b);
    ASSERT_EQ(d.size(), 1u);
    EXPECT_EQ(d[0]["path"], "tracks/0/muted");
    EXPECT_EQ(d[0]["from"], false);
    EXPECT_EQ(d[0]["to"], true);
}

TEST(JsonDiff, ArrayGrewAndShrank) {
    json a = {{"fx", {"ReaEQ"}}};
    json b = {{"fx", {"ReaEQ", "ReaComp"}}};
    auto grew = diff(a, b);
    ASSERT_EQ(grew.size(), 1u);
    EXPECT_EQ(grew[0]["op"], "added");
    EXPECT_EQ(grew[0]["path"], "fx/1");
    EXPECT_EQ(grew[0]["to"], "ReaComp");

    auto shrank = diff(b, a);
    ASSERT_EQ(shrank.size(), 1u);
    EXPECT_EQ(shrank[0]["op"], "removed");
    EXPECT_EQ(shrank[0]["path"], "fx/1");
    EXPECT_EQ(shrank[0]["from"], "ReaComp");
}

TEST(JsonDiff, MultipleTrackChanges) {
    json a = {{"tracks", {{{"vol", -6.0}, {"pan", 0.0}}, {{"vol", 0.0}, {"pan", 0.0}}}}};
    json b = {{"tracks", {{{"vol", -3.0}, {"pan", 0.0}}, {{"vol", 0.0}, {"pan", -0.5}}}}};
    auto d = diff(a, b);
    ASSERT_EQ(d.size(), 2u);
    EXPECT_EQ(d[0]["path"], "tracks/0/vol");
    EXPECT_EQ(d[1]["path"], "tracks/1/pan");
}
