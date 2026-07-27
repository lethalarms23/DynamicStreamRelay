#include "app/ConfigurationManager.h"
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace rtsp;

TEST(ProfileConfiguration, PersistsMultipleIndependentProfiles) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QSettings().clear();
    ConfigurationManager manager;
    AppConfig first;
    first.profileId="one";first.profileName="Girlfriend PC";first.inputProtocol="rtmp";
    first.localStreamKey="path-one";first.destinationUrl="rtmps://one.example/live";
    AppConfig second=first;
    second.profileId="two";second.profileName="Truck";second.inputProtocol="srt";
    second.localStreamKey="path-two";second.requestedDelaySeconds=60;second.videoEncoder="h264_nvenc";
    manager.saveProfiles({first,second});
    manager.setSelectedProfileId("two");
    const auto loaded=manager.loadProfiles();
    ASSERT_EQ(loaded.size(),2);
    EXPECT_EQ(loaded[0].profileName,"Girlfriend PC");
    EXPECT_EQ(loaded[1].profileName,"Truck");
    EXPECT_EQ(loaded[1].inputProtocol,"srt");
    EXPECT_EQ(loaded[1].requestedDelaySeconds,60);
    EXPECT_EQ(loaded[1].videoEncoder,"h264_nvenc");
    EXPECT_EQ(manager.selectedProfileId(),"two");
}

