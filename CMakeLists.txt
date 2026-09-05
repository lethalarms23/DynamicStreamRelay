cmake_minimum_required(VERSION 3.24)
project(RTMPTimeShiftProxy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(RTSP_BUILD_TESTS "Build unit tests" ON)
option(RTSP_ENABLE_KEYCHAIN "Use QtKeychain when available" ON)
option(RTSP_BUILD_INTEGRATION_TESTS "Build opt-in MediaMTX/FFmpeg integration tests" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
  libavformat libavcodec libavutil libswscale libswresample)

if(RTSP_ENABLE_KEYCHAIN)
  find_package(Qt6Keychain QUIET)
endif()

add_library(rtsp_core STATIC
  src/app/ApplicationState.cpp
  src/app/ConfigurationManager.cpp
  src/ingest/TimestampNormalizer.cpp
  src/media/PacketBuffer.cpp
  src/media/DelayController.cpp
  src/logging/SecretRedactor.cpp
  src/media/UrlUtils.cpp)
target_include_directories(rtsp_core PUBLIC src)
target_link_libraries(rtsp_core PUBLIC Qt6::Core)
target_compile_options(rtsp_core PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

add_executable(RTMPTimeShiftProxy WIN32
  src/main.cpp
  src/app/ApplicationController.cpp
  src/app/CapacityEstimator.cpp
  src/app/RelaySession.cpp
  src/app/CredentialStore.cpp
  src/ingest/MediaMTXManager.cpp
  src/ingest/IngestReader.cpp
  src/media/FFmpegRAII.cpp
  src/media/Decoder.cpp
  src/media/OutboundPublisher.cpp
  src/metrics/MetricsCollector.cpp
  src/logging/Logger.cpp
  src/panel/WebPanelServer.cpp
  src/ui/MainWindow.cpp)
target_include_directories(RTMPTimeShiftProxy PRIVATE src)
target_link_libraries(RTMPTimeShiftProxy PRIVATE
  rtsp_core Qt6::Core Qt6::Widgets Qt6::Network PkgConfig::FFMPEG)
if(WIN32)
  target_link_libraries(RTMPTimeShiftProxy PRIVATE advapi32)
endif()
if(TARGET Qt6Keychain::Qt6Keychain)
  target_link_libraries(RTMPTimeShiftProxy PRIVATE Qt6Keychain::Qt6Keychain)
  target_compile_definitions(RTMPTimeShiftProxy PRIVATE RTSP_HAS_KEYCHAIN=1)
endif()

install(TARGETS RTMPTimeShiftProxy RUNTIME DESTINATION bin)
install(FILES LICENSE README.md DESTINATION .)
if(WIN32)
  install(PROGRAMS resources/mediamtx/windows/mediamtx.exe DESTINATION bin)
else()
  install(DIRECTORY resources/mediamtx/ DESTINATION share/rtmp-timeshift-proxy/mediamtx)
endif()

if(RTSP_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  add_executable(rtsp_tests
    tests/PacketBufferTests.cpp
    tests/DelayControllerTests.cpp
    tests/TimestampNormalizerTests.cpp
    tests/SecretRedactorTests.cpp
    tests/ProfileConfigurationTests.cpp
    tests/UrlAndStateTests.cpp
    tests/MetricsCollectorTests.cpp
    src/metrics/MetricsCollector.cpp)
  target_link_libraries(rtsp_tests PRIVATE rtsp_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(rtsp_tests)
endif()

if(RTSP_BUILD_INTEGRATION_TESTS)
  add_executable(rtsp_integration_tests
    tests/IntegrationTestMain.cpp
    tests/IntegrationPipelineTests.cpp
    src/app/CapacityEstimator.cpp
    src/app/RelaySession.cpp
    src/ingest/IngestReader.cpp
    src/logging/Logger.cpp
    src/media/FFmpegRAII.cpp
    src/media/Decoder.cpp
    src/media/OutboundPublisher.cpp
    src/metrics/MetricsCollector.cpp)
  target_include_directories(rtsp_integration_tests PRIVATE src)
  target_link_libraries(rtsp_integration_tests PRIVATE
    rtsp_core Qt6::Core Qt6::Gui Qt6::Network PkgConfig::FFMPEG GTest::gtest)
  target_compile_definitions(rtsp_integration_tests PRIVATE RTSP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  gtest_discover_tests(rtsp_integration_tests)
endif()

set(CPACK_PACKAGE_NAME "RTMP TimeShift Proxy")
set(CPACK_PACKAGE_VENDOR "RTMP TimeShift Proxy")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "RTMP TimeShift Proxy")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
if(WIN32)
  set(CPACK_GENERATOR "ZIP;NSIS")
  set(CPACK_NSIS_DISPLAY_NAME "RTMP TimeShift Proxy")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
else()
  set(CPACK_GENERATOR "TGZ")
  install(FILES packaging/linux/rtmp-timeshift-proxy.desktop DESTINATION share/applications)
  install(FILES resources/icons/rtmp-timeshift-proxy.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()
include(CPack)
