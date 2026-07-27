# Linux x86_64 packaging

The CPack `TGZ` is the portable baseline. To produce an AppImage, install into an
AppDir, copy `packaging/linux/AppRun` to its root, deploy Qt/FFmpeg with
`linuxdeploy`, and include `resources/mediamtx/linux/mediamtx` under
`usr/share/rtmp-timeshift-proxy/mediamtx/linux/`.

MediaMTX must retain its executable bit. The application stores rotating logs under
the platform application-data directory, never beside the executable.

