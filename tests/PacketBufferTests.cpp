#include "media/PacketBuffer.h"
#include <gtest/gtest.h>
using namespace rtsp;
static BufferedPacket pkt(StreamType type, qint64 time, bool key=false, int bytes=10) { BufferedPacket p; p.type=type;p.dtsUs=p.ptsUs=time;p.durationUs=1000;p.keyframe=key;p.data=QByteArray(bytes,'x');return p; }
TEST(PacketBuffer, InsertionRequiresDtsOrdering) { PacketBuffer b(1000000,1000); EXPECT_TRUE(b.append(pkt(StreamType::Video,100,true))); EXPECT_FALSE(b.append(pkt(StreamType::Audio,99))); EXPECT_EQ(b.size(),1); }
TEST(PacketBuffer, DurationUsesMediaTimestamps) { PacketBuffer b(1000000,1000); b.append(pkt(StreamType::Video,100,true));b.append(pkt(StreamType::Audio,250)); EXPECT_EQ(b.durationUs(),150); }
TEST(PacketBuffer, EnforcesMaximumMemory) { PacketBuffer b(1000000,25); b.append(pkt(StreamType::Video,0,true,20)); b.append(pkt(StreamType::Video,10,true,20)); EXPECT_LE(b.memoryBytes(),25); EXPECT_GT(b.overflowCount(),0); }
TEST(PacketBuffer, FindsKeyframeAtOrBefore) { PacketBuffer b(1000000,1000); b.append(pkt(StreamType::Video,0,true));b.append(pkt(StreamType::Video,50));b.append(pkt(StreamType::Video,100,true)); auto p=b.nearestKeyframeAtOrBefore(75); ASSERT_TRUE(p);EXPECT_EQ(p->dtsUs,0); }
TEST(PacketBuffer, AlignsAudioAtOrAfterSelectedVideo) { PacketBuffer b(1000000,1000); b.append(pkt(StreamType::Video,0,true));b.append(pkt(StreamType::Audio,10));b.append(pkt(StreamType::Video,100,true));b.append(pkt(StreamType::Audio,105)); auto s=b.selectAt(100); ASSERT_TRUE(s); auto a=b.packet(s->audioSequence);ASSERT_TRUE(a);EXPECT_EQ(a->ptsUs,105); }
TEST(PacketBuffer, DisabledStorageAcceptsButDoesNotRetainPackets) {
    PacketBuffer b(1000000, 1000);
    b.setStoragePolicy(false, 0);
    EXPECT_TRUE(b.append(pkt(StreamType::Video, 0, true)));
    EXPECT_EQ(b.size(), 0);
    EXPECT_EQ(b.memoryBytes(), 0);
}
TEST(PacketBuffer, IdleRetentionKeepsOnlyRequestedWindow) {
    PacketBuffer b(10000000, 10000);
    b.setStoragePolicy(true, 2000000);
    for (qint64 second = 0; second <= 5; ++second)
        b.append(pkt(StreamType::Video, second * 1000000, true));
    EXPECT_LE(b.durationUs(), 2000000);
    EXPECT_GT(b.overflowCount(), 0);
}
