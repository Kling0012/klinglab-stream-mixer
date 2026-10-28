#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mac_host="${KSM_MAC_HOST:-}"
if [[ "${1:-}" == --on-mac ]]; then
    shift
elif [[ "$(uname -s)" != Darwin || -n "$mac_host" ]]; then
    if [[ $# -gt 1 || ( $# == 1 && "${1:-}" != --build-only ) ]]; then
        printf 'Usage: bash scripts/obs-build.sh [--build-only]\n' >&2
        exit 2
    fi
    if [[ -z "$mac_host" ]]; then
        printf 'KSM_MAC_HOST=ユーザー@Macのホスト を指定してください\n' >&2
        exit 2
    fi
    printf -v remote_command '%q ' /bin/bash -s -- "${KSM_MAC_DIR:-~/dev/ksm}"
    mac_dir="$(ssh -o BatchMode=yes -o ConnectTimeout=15 "$mac_host" "$remote_command" <<'MAC_DIR'
set -euo pipefail
mac_dir="$1"
case "$mac_dir" in
    '~') mac_dir="$HOME" ;;
    '~/'*) mac_dir="$HOME/${mac_dir#\~/}" ;;
esac
mkdir -p "$mac_dir/obs-plugin" "$mac_dir/scripts"
cd -- "$mac_dir"
pwd
MAC_DIR
)"
    printf -v rsync_dir '%q' "$mac_dir/obs-plugin/"
    rsync -a --exclude '.git' --exclude 'build/' \
        -e 'ssh -o BatchMode=yes -o ConnectTimeout=15' \
        "$repo_dir/obs-plugin/" "$mac_host:$rsync_dir"
    printf -v rsync_dir '%q' "$mac_dir/scripts/"
    rsync -a -e 'ssh -o BatchMode=yes -o ConnectTimeout=15' \
        "$repo_dir"/scripts/obs-*.sh "$mac_host:$rsync_dir"
    printf -v remote_command '%q ' env "KSM_MAC_DIR=$mac_dir" "KSM_CMAKE=${KSM_CMAKE:-}" \
        /bin/bash "$mac_dir/scripts/obs-build.sh" --on-mac "$@"
    ssh -o BatchMode=yes -o ConnectTimeout=15 "$mac_host" "$remote_command"
    exit $?
fi

[[ "$(uname -s)" == Darwin ]]
build_only=false
if [[ "${1:-}" == --build-only ]]; then build_only=true; shift; fi
[[ $# == 0 ]]
mac_dir="${KSM_MAC_DIR:-$repo_dir}"
case "$mac_dir" in
    '~') mac_dir="$HOME" ;;
    '~/'*) mac_dir="$HOME/${mac_dir#\~/}" ;;
esac
source_dir="$mac_dir/obs-plugin"
deps_dir="$mac_dir/build-obs-deps"
build_dir="$mac_dir/build-obs"
cmake_bin="${KSM_CMAKE:-}"
if [[ -z "$cmake_bin" ]]; then
    cmake_bin="$HOME/opt/cmake/CMake.app/Contents/bin/cmake"
    if [[ ! -x "$cmake_bin" ]]; then cmake_bin=cmake; fi
fi
case "$cmake_bin" in
    '~/'*) cmake_bin="$HOME/${cmake_bin#\~/}" ;;
esac
if ! command -v "$cmake_bin" > /dev/null; then
    printf 'CMake is missing: %s\n' "$cmake_bin" >&2
    exit 1
fi
if ! "$build_only" && pgrep -x OBS >/dev/null; then
    printf 'OBS is running. Close OBS before building and installing the plugin.\n' >&2
    exit 1
fi
mkdir -p "$deps_dir" "$HOME/Applications"

fetch_dmg() {
    local version="$1" architecture="$2"
    local image="$deps_dir/OBS-Studio-$version-macOS-$architecture.dmg"
    if [[ ! -f "$image.downloaded" ]]; then
        curl --connect-timeout 15 --max-time 240 -fL \
            "https://github.com/obsproject/obs-studio/releases/download/$version/OBS-Studio-$version-macOS-$architecture.dmg" \
            -o "$image"
        touch "$image.downloaded"
    fi
}

mounted=""
cleanup_mount() {
    if [[ -n "$mounted" ]]; then
        hdiutil detach "$mounted" || true
    fi
}
trap cleanup_mount EXIT

if [[ -d /Applications/OBS.app ]]; then
    obs_app=/Applications/OBS.app
elif [[ -d "$HOME/Applications/OBS.app" ]]; then
    obs_app="$HOME/Applications/OBS.app"
elif "$build_only"; then
    printf 'OBS 30/31 is required in /Applications or ~/Applications to build the plugin.\n' >&2
    exit 1
else
    version=31.1.2
    if [[ "$(uname -m)" == arm64 ]]; then native_arch=Apple; else native_arch=Intel; fi
    fetch_dmg "$version" "$native_arch"
    mounted="$deps_dir/mount-$native_arch"
    mkdir -p "$mounted"
    hdiutil attach -readonly -nobrowse -mountpoint "$mounted" \
        "$deps_dir/OBS-Studio-$version-macOS-$native_arch.dmg"
    ditto "$mounted/OBS.app" "$HOME/Applications/OBS.app"
    hdiutil detach "$mounted"
    mounted=""
    obs_app="$HOME/Applications/OBS.app"
fi
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$obs_app/Contents/Info.plist")"
case "$version" in
    30.*|31.*) ;;
    *) printf 'Supported OBS releases: 30/31; found %s\n' "$version" >&2; exit 1 ;;
esac
framework="$obs_app/Contents/Frameworks/libobs.framework"
[[ -f "$framework/libobs" ]]

headers_dir="$deps_dir/obs-studio-$version"
if [[ ! -f "$headers_dir/libobs/obs-module.h" ]]; then
    curl --connect-timeout 15 --max-time 180 -fL \
        "https://api.github.com/repos/obsproject/obs-studio/tarball/$version" \
        -o "$deps_dir/obs-studio-$version.tar.gz"
    mkdir -p "$headers_dir"
    tar -xzf "$deps_dir/obs-studio-$version.tar.gz" -C "$headers_dir" --strip-components=1
fi

# Official DMGs are single-architecture. Combine only libobs in a build-only
# framework; do not modify OBS.app or ship a private copy of libobs.
universal="$deps_dir/universal-$version/libobs.framework"
mkdir -p "$(dirname "$universal")"
ditto "$framework" "$universal"
archs="$(lipo -archs "$framework/libobs")"
if [[ "$archs" == *arm64* && "$archs" == *x86_64* ]]; then
    :
else
    if [[ "$archs" == *arm64* ]]; then other_arch=Intel; else other_arch=Apple; fi
    fetch_dmg "$version" "$other_arch"
    mounted="$deps_dir/mount-$other_arch"
    mkdir -p "$mounted"
    hdiutil attach -readonly -nobrowse -mountpoint "$mounted" \
        "$deps_dir/OBS-Studio-$version-macOS-$other_arch.dmg"
    lipo -create "$framework/Versions/A/libobs" \
        "$mounted/OBS.app/Contents/Frameworks/libobs.framework/Versions/A/libobs" \
        -output "$universal/Versions/A/libobs"
    hdiutil detach "$mounted"
    mounted=""
fi
"$cmake_bin" -S "$source_dir" -B "$build_dir" -G 'Unix Makefiles' \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4 \
    '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
    "-DOBS_SOURCE_DIR=$headers_dir" "-DOBS_LIBRARY=$universal/libobs"
"$cmake_bin" --build "$build_dir" --parallel 4
bundle="$build_dir/klinglab-stream-mixer-source.plugin"
/usr/bin/codesign --force --sign - "$bundle"
/usr/bin/codesign --verify --strict "$bundle"
lipo -archs "$bundle/Contents/MacOS/klinglab-stream-mixer-source"
lipo -archs "$build_dir/ksm-shm-test"
if "$build_only"; then
    printf '\nOBS: %s (%s)\nBuilt plugin: %s\n' "$obs_app" "$version" "$bundle"
    exit 0
fi
install_dir="$HOME/Library/Application Support/obs-studio/plugins"
mkdir -p "$install_dir"
if [[ -e "$install_dir/klinglab-stream-mixer-source.plugin" ]]; then
    archive="$deps_dir/archive/$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$archive"
    ditto "$install_dir/klinglab-stream-mixer-source.plugin" "$archive/klinglab-stream-mixer-source.plugin"
fi
ditto "$bundle" "$install_dir/klinglab-stream-mixer-source.plugin"
/usr/bin/codesign --verify --strict "$install_dir/klinglab-stream-mixer-source.plugin"
printf '\nOBS: %s (%s)\nPlugin: %s\nSender: %s\n' \
    "$obs_app" "$version" "$install_dir/klinglab-stream-mixer-source.plugin" "$build_dir/ksm-shm-test"
