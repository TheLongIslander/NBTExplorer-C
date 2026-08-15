#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-${root_dir}/build}"
output_dir="${2:-${root_dir}/dist}"
app_dir="${build_dir}/AppDir"
tool_dir="${build_dir}/appimage-tools"

if [[ ! -f "${build_dir}/CMakeCache.txt" ]]; then
  echo "Build directory is not configured: ${build_dir}" >&2
  exit 1
fi

cmake -E remove_directory "${app_dir}"
cmake -E make_directory "${app_dir}/usr" "${tool_dir}" "${output_dir}"
cmake --install "${build_dir}" --prefix "${app_dir}/usr" --config Release --component desktop

linuxdeploy="${tool_dir}/linuxdeploy-x86_64.AppImage"
qt_plugin="${tool_dir}/linuxdeploy-plugin-qt-x86_64.AppImage"
linuxdeploy_sha256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
qt_plugin_sha256="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"

download_tool() {
  local url="$1"
  local destination="$2"
  local expected_sha256="$3"
  local actual_sha256=""

  if [[ -f "${destination}" ]]; then
    actual_sha256="$(sha256sum "${destination}" | awk '{print $1}')"
  fi
  if [[ "${actual_sha256}" != "${expected_sha256}" ]]; then
    curl --fail --location --output "${destination}" "${url}"
    actual_sha256="$(sha256sum "${destination}" | awk '{print $1}')"
    if [[ "${actual_sha256}" != "${expected_sha256}" ]]; then
      echo "Checksum verification failed for ${destination}" >&2
      rm -f "${destination}"
      exit 1
    fi
  fi
  chmod +x "${destination}"
}

download_tool \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage" \
  "${linuxdeploy}" \
  "${linuxdeploy_sha256}"
download_tool \
  "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage" \
  "${qt_plugin}" \
  "${qt_plugin_sha256}"

export PATH="${tool_dir}:${PATH}"
export APPIMAGE_EXTRACT_AND_RUN=1
export OUTPUT="${output_dir}/C-NBT-Explorer-x86_64.AppImage"

"${linuxdeploy}" \
  --appdir "${app_dir}" \
  --executable "${app_dir}/usr/bin/cnbt-explorer" \
  --desktop-file "${root_dir}/packaging/io.github.cnbt-explorer.desktop" \
  --icon-file "${root_dir}/packaging/io.github.cnbt-explorer.svg" \
  --plugin qt \
  --output appimage

echo "Created ${OUTPUT}"
