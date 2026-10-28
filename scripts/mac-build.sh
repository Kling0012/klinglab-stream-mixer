#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mac_host="${KSM_MAC_HOST:-}"
local_build=false
if [[ "$(uname -s)" == Darwin && -z "$mac_host" ]]; then
    local_build=true
    mac_dir="${KSM_MAC_DIR:-$repo_dir}"
    case "$mac_dir" in
        '~') mac_dir="$HOME" ;;
        '~/'*) mac_dir="$HOME/${mac_dir#\~/}" ;;
    esac
else
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
mkdir -p "$mac_dir"
cd -- "$mac_dir"
pwd
MAC_DIR
)"
fi

include_streamdeck="${KSM_INCLUDE_STREAMDECK:-1}"
[[ "$include_streamdeck" == 0 || "$include_streamdeck" == 1 ]]
if [[ "$include_streamdeck" == 1 ]]; then
    pack_log="$(mktemp -t ksm-streamdeck-pack.XXXXXX)"
    if ! bash "$repo_dir/streamdeck/scripts/pack.sh" > "$pack_log" 2>&1; then
        printf 'Stream Deck packaging failed; see %s\n' "$pack_log" >&2
        exit 1
    fi
    package="$(python3 -c 'import pathlib, sys; print(next(line for line in pathlib.Path(sys.argv[1]).read_text().splitlines() if line.startswith("/") and line.endswith(".streamDeckPlugin")))' "$pack_log")"
    [[ -f "$package" ]]
    mkdir -p "$repo_dir/build/resources"
    cp "$package" "$repo_dir/build/resources/site.klinglab.streammixer.streamDeckPlugin"
    if "$local_build"; then
        if [[ "$(cd -- "$mac_dir" && pwd)" != "$repo_dir" ]]; then
            mkdir -p "$mac_dir/build/resources"
            cp "$package" "$mac_dir/build/resources/site.klinglab.streammixer.streamDeckPlugin"
        fi
    else
        printf -v remote_command '%q ' mkdir -p "$mac_dir/build/resources"
        ssh -o BatchMode=yes -o ConnectTimeout=15 "$mac_host" "$remote_command"
        scp -q "$repo_dir/build/resources/site.klinglab.streammixer.streamDeckPlugin" "$mac_host:$mac_dir/build/resources/"
    fi
fi
if "$local_build"; then
    mac_command=(/bin/bash -s -- "$mac_dir" "${KSM_CMAKE:-}" "$include_streamdeck")
else
    printf -v rsync_dir '%q' "$mac_dir/"
    rsync -a \
        --exclude '/.git/' \
        --exclude '/.claude/' \
        --exclude 'build/' \
        --exclude 'build-*/' \
        --exclude '/.cache/' \
        --exclude '/archive/' \
        --exclude 'node_modules/' \
        --exclude '/streamdeck/dist/' \
        --exclude '/dist/' \
        -e 'ssh -o BatchMode=yes -o ConnectTimeout=15' \
        "$repo_dir/" "$mac_host:$rsync_dir"
    printf -v remote_command '%q ' /bin/bash -s -- "$mac_dir" "${KSM_CMAKE:-}" "$include_streamdeck"
    mac_command=(ssh -o BatchMode=yes -o ConnectTimeout=15 "$mac_host" "$remote_command")
fi

"${mac_command[@]}" <<'MAC_BUILD'
set -euo pipefail
source_dir="$1"
case "$source_dir" in
    '~') source_dir="$HOME" ;;
    '~/'*) source_dir="$HOME/${source_dir#\~/}" ;;
esac
cmake_bin="$2"
include_streamdeck="$3"
if [[ -z "$cmake_bin" ]]; then
    cmake_bin="$HOME/opt/cmake/CMake.app/Contents/bin/cmake"
    if [[ ! -x "$cmake_bin" ]]; then cmake_bin=cmake; fi
fi
case "$cmake_bin" in
    '~/'*) cmake_bin="$HOME/${cmake_bin#\~/}" ;;
esac
build_dir="$source_dir/build"
if ! command -v "$cmake_bin" > /dev/null; then
    printf 'CMake is missing: %s\n' "$cmake_bin" >&2
    exit 1
fi
obs_bundle="$source_dir/build-obs/klinglab-stream-mixer-source.plugin"
if [[ ! -x "$obs_bundle/Contents/MacOS/klinglab-stream-mixer-source" ]]; then
    printf 'Build the OBS plugin first with scripts/obs-build.sh.\n' >&2
    exit 1
fi
mkdir -p "$build_dir/resources"
/usr/bin/ditto -c -k --keepParent "$obs_bundle" "$build_dir/resources/klinglab-stream-mixer-source.zip"
streamdeck_package=""
if [[ "$include_streamdeck" == 1 ]]; then
    streamdeck_package="$build_dir/resources/site.klinglab.streammixer.streamDeckPlugin"
fi
"$cmake_bin" -S "$source_dir" -B "$build_dir" \
    -G 'Unix Makefiles' \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4 \
    '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
    "-DKSM_STREAMDECK_PACKAGE=$streamdeck_package" \
    "-DKSM_OBS_PACKAGE=$build_dir/resources/klinglab-stream-mixer-source.zip"
"$cmake_bin" --build "$build_dir" --config Release --parallel 4
app_path="$build_dir/app/KlinglabStreamMixer_artefacts/Release/Klinglab Stream Mixer.app"
[[ -x "$app_path/Contents/MacOS/Klinglab Stream Mixer" ]]
/usr/bin/codesign --force --sign - "$app_path"
/usr/bin/codesign --verify --strict "$app_path"
printf '\nApplication: %s\n' "$app_path"
MAC_BUILD
