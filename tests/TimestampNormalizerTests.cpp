#include "ingest/TimestampNormalizer.h"
#include <gtest/gtest.h>
using namespace rtsp;
TEST(TimestampNormalizer, ReconnectNeverMovesTimelineBackward) { TimestampNormalizer n;n.beginSession(1,0);EXPECT_EQ(n.normalize(1000),0);EXPECT_EQ(n.normalize(2000),1000);n.beginSession(2,n.lastOutput()+1);EXPECT_EQ(n.normalize(0),1001);EXPECT_EQ(n.normalize(500),1501); }
TEST(TimestampNormalizer, ReorderedInputStaysMonotonic) { TimestampNormalizer n;n.beginSession(1,0);auto a=n.normalize(100);auto b=n.normalize(90);EXPECT_GT(b,a); }
// Regression test for the audio-corruption bug: feeding two independent
// streams' packets through one shared normalizer forced whichever stream
// advanced slower to have its timestamps clamped forward to match the
// faster stream, corrupting both. Each stream must get its own instance
// and must reproduce its own input spacing untouched by the other.
TEST(TimestampNormalizer, IndependentStreamsDoNotDistortEachOther) {
    TimestampNormalizer video, audio;
    const auto base = std::max(video.lastOutput(), audio.lastOutput()) + 1;
    video.beginSession(1, base); audio.beginSession(1, base);
    // Video ticks every ~33ms (30fps), audio every ~21ms (1024 samples @ 48kHz),
    // interleaved as they would arrive from a real muxed source.
    EXPECT_EQ(video.normalize(0), 0);
    EXPECT_EQ(audio.normalize(0), 0);
    EXPECT_EQ(audio.normalize(21333), 21333);
    EXPECT_EQ(video.normalize(33333), 33333);   // must not be clamped past by audio's 21333 step
    EXPECT_EQ(audio.normalize(42666), 42666);   // must not be clamped past by video's 33333 step
}

