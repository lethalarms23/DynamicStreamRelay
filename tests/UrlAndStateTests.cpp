#include "media/UrlUtils.h"
#include "app/ApplicationState.h"
#include <gtest/gtest.h>
using namespace rtsp;
TEST(UrlUtils, JoinsSlashesAndEscapesKey) { EXPECT_EQ(joinDestination("rtmps://host/live/","/my key"),"rtmps://host/live/my%20key"); }
TEST(UrlUtils, ValidatesSchemeAndHost) { EXPECT_TRUE(validateRtmpUrl("rtmps://host/live").valid);EXPECT_FALSE(validateRtmpUrl("https://host/live").valid);EXPECT_FALSE(validateRtmpUrl("rtmp:///live").valid); }
TEST(StateMachine, AllowsNormalStartupAndRejectsUnsafeJump) { EXPECT_TRUE(canTransition(ApplicationState::Stopped,ApplicationState::StartingIngestServer));EXPECT_TRUE(canTransition(ApplicationState::StartingIngestServer,ApplicationState::WaitingForSource));EXPECT_FALSE(canTransition(ApplicationState::Stopped,ApplicationState::Relaying)); }
