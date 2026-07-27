#!/usr/bin/env bash
set -euo pipefail

make_archive=true
if [[ "${1:-}" == "--no-archive" ]]; then
    make_archive=false
elif [[ $# -gt 0 ]]; then
    echo "Usage: $0 [--no-archive]" >&2
    exit 2
fi

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
sdk_root="${RTSP_WINDOWS_SDK_ROOT:-${HOME}/.cache/rtsp-windows-sdk}"
qt_root="${RTSP_WINDOWS_QT_ROOT:-${sdk_root}/Qt/6.8.3/mingw_64}"
qt_host_root="${RTSP_WINDOWS_QT_HOST_ROOT:-${sdk_root}/QtHost/6.8.3/gcc_64}"
mingw_root="${RTSP_MINGW_ROOT:-${sdk_root}/Qt/Tools/mingw1310_64}"
ffmpeg_root="${RTSP_WINDOWS_FFMPEG_ROOT:-${sdk_root}/ffmpeg/ffmpeg-n8.1-latest-win64-gpl-shared-8.1}"
build_dir="${RTSP_WINDOWS_BUILD_DIR:-${project_dir}/build_windows}"
portable_dir="${build_dir}/portable"
stage_dir="${portable_dir}/RTMPTimeShiftProxy"

for required in "${qt_root}/lib/cmake/Qt6/Qt6Config.cmake" "${qt_host_root}/libexec/moc" \
    "${mingw_root}/bin/g++.exe" "${ffmpeg_root}/lib/libavcodec.dll.a"; do
    if [[ ! -e "${required}" ]]; then
        echo "Missing Windows SDK component: ${required}" >&2
        echo "See packaging/windows/README.md for setup instructions." >&2
        exit 1
    fi
done

export RTSP_MINGW_ROOT="${mingw_root}"
export PKG_CONFIG_PATH="${ffmpeg_root}/lib/pkgconfig"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEPATH="$(winepath -w "${mingw_root}/bin")"

cmake --fresh -S "${project_dir}" -B "${build_dir}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${project_dir}/cmake/windows/mingw-wine-toolchain.cmake" \
    -DCMAKE_PREFIX_PATH="${qt_root}" \
    -DQT_HOST_PATH="${qt_host_root}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DRTSP_BUILD_TESTS=OFF \
    -DRTSP_BUILD_INTEGRATION_TESTS=OFF \
    -DRTSP_ENABLE_KEYCHAIN=OFF
cmake --build "${build_dir}" --parallel
cmake -E remove_directory "${stage_dir}"
cmake --install "${build_dir}" --prefix "${stage_dir}"

wine "${qt_root}/bin/windeployqt.exe" --release --no-translations --compiler-runtime \
    "${stage_dir}/bin/RTMPTimeShiftProxy.exe"
cp "${ffmpeg_root}"/bin/avcodec-*.dll "${ffmpeg_root}"/bin/avformat-*.dll \
    "${ffmpeg_root}"/bin/avutil-*.dll "${ffmpeg_root}"/bin/swresample-*.dll \
    "${ffmpeg_root}"/bin/swscale-*.dll "${stage_dir}/bin/"
cp "${mingw_root}/bin/libgcc_s_seh-1.dll" "${mingw_root}/bin/libstdc++-6.dll" \
    "${mingw_root}/bin/libwinpthread-1.dll" "${stage_dir}/bin/"
if [[ "${make_archive}" == true ]]; then
    cmake -E chdir "${portable_dir}" cmake -E tar cf \
        "${build_dir}/RTMPTimeShiftProxy-Windows-x64.zip" --format=zip RTMPTimeShiftProxy
    echo "Windows portable archive: ${build_dir}/RTMPTimeShiftProxy-Windows-x64.zip"
else
    echo "Windows runnable directory: ${stage_dir}"
fi
