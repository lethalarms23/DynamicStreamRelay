#include "logging/SecretRedactor.h"
#include <gtest/gtest.h>
using namespace rtsp;
TEST(SecretRedactor, RemovesRegisteredSecret) { SecretRedactor r;r.addSecret("super-secret");auto s=r.redact("URL super-secret done");EXPECT_FALSE(s.contains("super-secret"));EXPECT_TRUE(s.contains("[REDACTED]")); }
TEST(SecretRedactor, RemovesNamedAndEmbeddedCredentials) { SecretRedactor r;auto s=r.redact("stream_key=abc123 rtmp://user:pass@example.com/live");EXPECT_FALSE(s.contains("abc123"));EXPECT_FALSE(s.contains("user:pass")); }
TEST(SecretRedactor, RemovesSrtPassphrases) { SecretRedactor r;auto s=r.redact("srt://relay:8890?latency=2000000&passphrase=verysecret123&pbkeylen=32");EXPECT_FALSE(s.contains("verysecret123"));EXPECT_TRUE(s.contains("passphrase=[REDACTED]")); }
