#include "media/DelayController.h"
#include <gtest/gtest.h>
using namespace rtsp;
TEST(DelayController, IncreaseRequestsFillerAndTracksProgress) { DelayController d(300000000); d.setCursor(90000000,100000000);auto r=d.request(30000000,100000000,90000000,50000000);EXPECT_EQ(r.action,DelayAction::InsertFiller);EXPECT_EQ(r.fillerDurationUs,20000000);d.advanceFiller(5000000);EXPECT_EQ(d.effectiveDelayUs(),15000000);EXPECT_EQ(d.remainingFillerUs(),15000000); }
TEST(DelayController, DecreaseRequestsForwardJump) { DelayController d(300000000);auto r=d.request(5000000,100000000,70000000,40000000);EXPECT_EQ(r.action,DelayAction::JumpForward);EXPECT_EQ(r.desiredSourceTimeUs,95000000); }
TEST(DelayController, EffectiveDelayAfterAlignedJump) { DelayController d(300000000);d.completeJump(94000000,100000000);EXPECT_EQ(d.effectiveDelayUs(),6000000); }
TEST(DelayController, CannotCancelStartedIncrease) { DelayController d(300000000);d.request(10000000,0,0,0);EXPECT_TRUE(d.cancelIncrease());d.request(10000000,0,0,0);d.advanceFiller(1);EXPECT_FALSE(d.cancelIncrease()); }

