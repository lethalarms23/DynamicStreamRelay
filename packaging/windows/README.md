# Windows x64 packaging

The reproducible Linux-to-Windows build uses Qt 6.8.3 MinGW, MinGW 13.1, and a
shared FFmpeg 8.1 Windows SDK. Set `RTSP_WINDOWS_SDK_ROOT` if the SDK is not in
`~/.cache/rtsp-windows-sdk`, then run:

```bash
./build-windows.sh
```

The result is `build_windows/RTMPTimeShiftProxy-Windows-x64.zip`. It includes
the application, MediaMTX, Qt plugins, MinGW runtime, and FFmpeg DLLs.

Native Windows builds remain supported with Visual Studio 2022 x64, shared Qt
6, and shared FFmpeg. Place the official `mediamtx.exe` in
`resources/mediamtx/windows/`, then build the `package` target.

Before packaging, use Qt's `windeployqt --release --no-translations` against the
installed executable and copy the FFmpeg shared DLLs into the same `bin` folder.
The NSIS installer does not silently download runtimes; CI must stage the approved
Qt, FFmpeg, MediaMTX, and MSVC redistributable artifacts.
