#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${RTSP_LINUX_BUILD_DIR:-${project_dir}/build}"
build_type="${RTSP_BUILD_TYPE:-Release}"
build_tests="${RTSP_BUILD_TESTS:-ON}"
integration_tests="${RTSP_BUILD_INTEGRATION_TESTS:-OFF}"
parallel_jobs="${RTSP_BUILD_JOBS:-$(nproc)}"

cmake --fresh -S "${project_dir}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DRTSP_BUILD_TESTS="${build_tests}" \
    -DRTSP_BUILD_INTEGRATION_TESTS="${integration_tests}"

cmake --build "${build_dir}" --parallel "${parallel_jobs}"

if [[ "${build_tests}" == "ON" ]]; then
    ctest --test-dir "${build_dir}" --output-on-failure
fi

echo "Linux executable: ${build_dir}/RTMPTimeShiftProxy"
