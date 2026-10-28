#!/usr/bin/env bash
set -euo pipefail

duration="${1:-5}"
if [[ ! "$duration" =~ ^[0-9]+$ || ${#duration} -gt 2 ]]; then
    printf 'Usage: bash scripts/mac-run.sh [seconds: 1-60] [app options...]\n' >&2
    exit 2
fi
if (( $# > 0 )); then shift; fi
duration=$((10#$duration))
if (( duration < 1 || duration > 60 )); then
    printf 'Duration must be between 1 and 60 seconds.\n' >&2
    exit 2
fi

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mac_host="${KSM_MAC_HOST:-}"
if [[ "$(uname -s)" == Darwin && -z "$mac_host" ]]; then
    mac_command=(/bin/bash -s -- "${KSM_MAC_DIR:-$repo_dir}" "$duration" "$@")
else
    if [[ -z "$mac_host" ]]; then
        printf 'KSM_MAC_HOST=ユーザー@Macのホスト を指定してください\n' >&2
        exit 2
    fi
    printf -v remote_command '%q ' /bin/bash -s -- "${KSM_MAC_DIR:-~/dev/ksm}" "$duration" "$@"
    mac_command=(ssh -o BatchMode=yes -o ConnectTimeout=15 "$mac_host" "$remote_command")
fi

"${mac_command[@]}" <<'MAC_RUN'
set -euo pipefail
source_dir="$1"
case "$source_dir" in
    '~') source_dir="$HOME" ;;
    '~/'*) source_dir="$HOME/${source_dir#\~/}" ;;
esac
shift
app_path="$source_dir/build/app/KlinglabStreamMixer_artefacts/Release/Klinglab Stream Mixer.app"
executable="$app_path/Contents/MacOS/Klinglab Stream Mixer"
if [[ ! -x "$executable" ]]; then
    printf 'Build the application first: %s\n' "$app_path" >&2
    exit 1
fi
if /usr/bin/pgrep -x 'Klinglab Stream Mixer' > /dev/null; then
    printf 'The mixer is already running. Close it before the timed run.\n' >&2
    exit 1
fi

export KSM_RUN_SECONDS="$1"
shift
/usr/bin/python3 - "$executable" "$@" <<'RUN_APP'
import os
import subprocess
import sys

seconds = int(os.environ["KSM_RUN_SECONDS"])
try:
    result = subprocess.run(
        [sys.argv[1], "--silent-preview", f"--quit-after={seconds}", "--snapshot", *sys.argv[2:]],
        timeout=seconds + 15,
        check=False,
        capture_output=True,
        text=True,
    )
except subprocess.TimeoutExpired:
    print("Timed run exceeded its deadline; the launched process was stopped.", file=sys.stderr)
    sys.exit(124)
print(result.stdout, end="")
if result.stderr:
    print(result.stderr, end="", file=sys.stderr)
if result.returncode:
    sys.exit(result.returncode)
for marker in ("STARTUP Klinglab Stream Mixer", "WINDOW visible", "SNAPSHOT ",
               f"AUTOQUIT after {seconds} seconds", "SHUTDOWN Klinglab Stream Mixer"):
    if marker not in result.stdout:
        print(f"Missing run marker: {marker}", file=sys.stderr)
        sys.exit(1)
if "SNAPSHOT ERROR" in result.stdout:
    sys.exit(1)
RUN_APP

if /usr/bin/pgrep -x 'Klinglab Stream Mixer' > /dev/null; then
    printf 'A mixer process remains after the timed run.\n' >&2
    exit 1
fi
log_file="$HOME/Library/Application Support/Klinglab Stream Mixer/logs/startup.log"
printf '\nStartup log (last 20 lines):\n'
/usr/bin/python3 - "$log_file" <<'READ_LOG'
from pathlib import Path
import sys

path = Path(sys.argv[1])
if not path.is_file():
    print(f"Startup log not found: {path}", file=sys.stderr)
    sys.exit(1)
print("\n".join(path.read_text().splitlines()[-20:]))
READ_LOG
MAC_RUN
