#include "metrics/MetricsCollector.h"
#include <gtest/gtest.h>

using namespace rtsp;

TEST(MetricsCollector, TracksReliabilityCounters) {
    MetricsCollector metrics;
    metrics.addDropped(4);
    metrics.incrementDecodeErrors();
    metrics.incrementEncodeErrors();
    metrics.incrementObsReconnects();
    metrics.incrementDestinationReconnects();

    const auto snapshot = metrics.snapshot();
    EXPECT_EQ(snapshot.droppedPackets, 4);
    EXPECT_EQ(snapshot.decodeErrors, 1);
    EXPECT_EQ(snapshot.encodeErrors, 1);
    EXPECT_EQ(snapshot.obsReconnects, 1);
    EXPECT_EQ(snapshot.destinationReconnects, 1);
}

TEST(MetricsCollector, TracksPacketsAndBufferState) {
    MetricsCollector metrics;
    metrics.packetIn(true, 1200);
    metrics.packetIn(false, 300);
    metrics.packetOut(900);
    metrics.setBuffer(42 * 1024, 3500000);

    const auto snapshot = metrics.snapshot();
    EXPECT_EQ(snapshot.incomingPackets, 2);
    EXPECT_EQ(snapshot.outgoingPackets, 1);
    EXPECT_EQ(snapshot.bufferBytes, 42 * 1024);
    EXPECT_EQ(snapshot.bufferDurationUs, 3500000);
}
