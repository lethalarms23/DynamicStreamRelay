#include "ingest/TimestampNormalizer.h"
#include <gtest/gtest.h>
using namespace rtsp;
TEST(TimestampNormalizer, ReconnectNeverMovesTimelineBackward) { TimestampNormalizer n;n.beginSession(1);EXPECT_EQ(n.normalize(1000),0);EXPECT_EQ(n.normalize(2000),1000);n.beginSession(2);EXPECT_EQ(n.normalize(0),1001);EXPECT_EQ(n.normalize(500),1501); }
TEST(TimestampNormalizer, ReorderedInputStaysMonotonic) { TimestampNormalizer n;n.beginSession(1);auto a=n.normalize(100);auto b=n.normalize(90);EXPECT_GT(b,a); }

