#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
include_streamdeck=1
assemble=false
for argument in "$@"; do
    case "$argument" in
        --without-streamdeck) include_streamdeck=0 ;;
        --assemble) assemble=true ;;
        *) printf 'Usage: bash scripts/package.sh [--without-streamdeck]\n' >&2; exit 2 ;;
    esac
done

mac_host="${KSM_MAC_HOST:-}"
local_build=false
if [[ "$(uname -s)" == Darwin && -z "$mac_host" ]]; then
    local_build=true
fi
if "$local_build" || "$assemble"; then
    repo_dir="${KSM_MAC_DIR:-$repo_dir}"
    case "$repo_dir" in
        '~') repo_dir="$HOME" ;;
        '~/'*) repo_dir="$HOME/${repo_dir#\~/}" ;;
    esac
fi

if ! "$assemble"; then
    if ! "$local_build" && [[ -z "$mac_host" ]]; then
        printf 'KSM_MAC_HOST=ユーザー@Macのホスト を指定してください\n' >&2
        exit 2
    fi
    if ! "$local_build"; then
        printf 'Linux／macOS の操作元から SSH で Mac に同期してビルドします。\n'
    fi
    bash "$repo_dir/scripts/obs-build.sh" --build-only
    KSM_INCLUDE_STREAMDECK="$include_streamdeck" bash "$repo_dir/scripts/mac-build.sh"
    if ! "$local_build"; then
        printf -v remote_command '%q ' /bin/bash -s -- "${KSM_MAC_DIR:-~/dev/ksm}"
        mac_dir="$(ssh -o BatchMode=yes -o ConnectTimeout=15 "$mac_host" "$remote_command" <<'MAC_DIR'
set -euo pipefail
mac_dir="$1"
case "$mac_dir" in
    '~') mac_dir="$HOME" ;;
    '~/'*) mac_dir="$HOME/${mac_dir#\~/}" ;;
esac
cd -- "$mac_dir"
pwd
MAC_DIR
)"
        options=(--assemble)
        if [[ "$include_streamdeck" == 0 ]]; then options+=(--without-streamdeck); fi
        printf -v remote_command '%q ' env "KSM_MAC_DIR=$mac_dir" \
            /bin/bash "$mac_dir/scripts/package.sh" "${options[@]}"
        ssh -o BatchMode=yes -o ConnectTimeout=15 "$mac_host" "$remote_command"
        mkdir -p "$repo_dir/dist"
        version="$(python3 - "$repo_dir/CMakeLists.txt" <<'VERSION'
import re
import sys
from pathlib import Path
text = Path(sys.argv[1]).read_text()
match = re.search(r'project\(KlinglabStreamMixer\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', text)
if not match:
    raise SystemExit('PROJECT_VERSION not found')
print(match.group(1))
VERSION
)"
        output="$repo_dir/dist/KlinglabStreamMixer-$version.pkg"
        if [[ -e "$output" ]]; then
            archive="$repo_dir/dist/archive/$(date +%Y%m%d-%H%M%S)-$$"
            mkdir -p "$archive"
            mv "$output" "$archive/"
        fi
        scp -q "$mac_host:$mac_dir/dist/KlinglabStreamMixer-$version.pkg" "$output"
        printf '\nPackage: %s\n' "$output"
        exit 0
    fi
fi

[[ "$(uname -s)" == Darwin ]]
version="$(/usr/bin/python3 - "$repo_dir/build/CMakeCache.txt" <<'VERSION'
import sys
from pathlib import Path
for line in Path(sys.argv[1]).read_text().splitlines():
    if line.startswith('CMAKE_PROJECT_VERSION:STATIC='):
        print(line.split('=', 1)[1])
        break
else:
    raise SystemExit('CMake PROJECT_VERSION not found in build cache')
VERSION
)"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
mkdir -p "$repo_dir/build/package" "$repo_dir/dist"
work="$(mktemp -d "$repo_dir/build/package/run-XXXXXX")"
root="$work/root"
app="$root/Applications/Klinglab Stream Mixer.app"
mkdir -p "$root/Applications" "$work/scripts" "$work/packages" "$work/resources"
/usr/bin/ditto "$repo_dir/build/app/KlinglabStreamMixer_artefacts/Release/Klinglab Stream Mixer.app" "$app"
resources="$app/Contents/Resources"
if [[ "$include_streamdeck" == 0 && -e "$resources/site.klinglab.streammixer.streamDeckPlugin" ]]; then
    mkdir -p "$work/omitted"
    mv "$resources/site.klinglab.streammixer.streamDeckPlugin" "$work/omitted/"
fi
/usr/bin/ditto "$repo_dir/build-obs/klinglab-stream-mixer-source.plugin" "$work/obs/klinglab-stream-mixer-source.plugin"
obs="$work/obs/klinglab-stream-mixer-source.plugin"
for binary in "$app/Contents/MacOS/Klinglab Stream Mixer" "$obs/Contents/MacOS/klinglab-stream-mixer-source"; do
    architectures="$(/usr/bin/lipo -archs "$binary")"
    [[ " $architectures " == *' arm64 '* && " $architectures " == *' x86_64 '* ]]
    printf 'Universal: %s (%s)\n' "$binary" "$architectures"
done
/usr/bin/codesign --force --sign - "$obs"
/usr/bin/codesign --verify --strict "$obs"
/usr/bin/ditto -c -k --keepParent "$obs" "$resources/klinglab-stream-mixer-source.zip"
chmod 755 "$resources/uninstall.command" "$resources/user-home.sh"
/usr/bin/codesign --force --sign - "$app"
/usr/bin/codesign --verify --strict "$app"
cp "$repo_dir/scripts/installer/postinstall" "$repo_dir/scripts/installer/user-home.sh" "$work/scripts/"
chmod 755 "$work/scripts/postinstall" "$work/scripts/user-home.sh"
cp "$repo_dir/scripts/installer/Welcome.txt" "$repo_dir/scripts/installer/Conclusion.txt" "$work/resources/"
/usr/bin/pkgbuild --analyze --root "$root" "$work/components.plist"
/usr/bin/python3 - "$work/components.plist" <<'COMPONENTS'
import plistlib
import sys
from pathlib import Path
path = Path(sys.argv[1])
components = plistlib.loads(path.read_bytes())
def configure(items):
    for item in items:
        item['BundleIsRelocatable'] = False
        configure(item.get('ChildBundles', []))
configure(components)
path.write_bytes(plistlib.dumps(components))
COMPONENTS
/usr/bin/pkgbuild --root "$root" --component-plist "$work/components.plist" \
    --identifier site.klinglab.streammixer.pkg --version "$version" \
    --install-location / --ownership recommended --scripts "$work/scripts" \
    "$work/packages/mixer.pkg"
/usr/bin/python3 - "$work/Distribution.xml" "$version" <<'DISTRIBUTION'
from pathlib import Path
import sys
import xml.etree.ElementTree as ET
root = ET.Element('installer-gui-script', {'minSpecVersion': '2'})
ET.SubElement(root, 'title').text = 'Klinglab Stream Mixer'
ET.SubElement(root, 'welcome', {'file': 'Welcome.txt'})
ET.SubElement(root, 'conclusion', {'file': 'Conclusion.txt'})
ET.SubElement(root, 'options', {'customize': 'never', 'require-scripts': 'true', 'hostArchitectures': 'arm64,x86_64', 'rootVolumeOnly': 'true'})
ET.SubElement(root, 'domains', {'enable_anywhere': 'false', 'enable_currentUserHome': 'false', 'enable_localSystem': 'true'})
volume = ET.SubElement(root, 'volume-check')
allowed = ET.SubElement(volume, 'allowed-os-versions')
ET.SubElement(allowed, 'os-version', {'min': '14.4'})
outline = ET.SubElement(root, 'choices-outline')
ET.SubElement(outline, 'line', {'choice': 'mixer'})
choice = ET.SubElement(root, 'choice', {'id': 'mixer', 'visible': 'false', 'title': 'Klinglab Stream Mixer'})
ET.SubElement(choice, 'pkg-ref', {'id': 'site.klinglab.streammixer.pkg'})
ET.SubElement(root, 'pkg-ref', {'id': 'site.klinglab.streammixer.pkg', 'version': sys.argv[2], 'auth': 'Root'}).text = 'mixer.pkg'
ET.ElementTree(root).write(sys.argv[1], encoding='utf-8', xml_declaration=True)
DISTRIBUTION
output="$repo_dir/dist/KlinglabStreamMixer-$version.pkg"
if [[ -e "$output" ]]; then
    archive="$repo_dir/dist/archive/$(date +%Y%m%d-%H%M%S)-$$"
    mkdir -p "$archive"
    mv "$output" "$archive/"
fi
/usr/bin/productbuild --distribution "$work/Distribution.xml" --package-path "$work/packages" \
    --resources "$work/resources" "$output"
/usr/sbin/pkgutil --payload-files "$output" > "$work/payload-files.txt"
printf '\nPackage: %s\nBuild records: %s\n' "$output" "$work"
