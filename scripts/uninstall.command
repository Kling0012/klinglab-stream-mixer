#!/bin/bash
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
source "$script_dir/user-home.sh"
if ! ksm_resolve_user; then
    printf 'Cannot resolve the recipient account. Run this script from your signed-in account.\n' >&2
    exit 1
fi
if [[ $# -gt 1 || ( $# == 1 && "$1" != --apply && "$1" != --dry-run ) ]]; then
    printf 'Usage: bash uninstall.command [--dry-run | --apply]\n' >&2
    exit 2
fi
apply=false
if [[ "${1:-}" == --apply ]]; then apply=true; fi
archive="$KSM_HOME/Library/Application Support/Klinglab Stream Mixer/archive/uninstall-$(date +%Y%m%d-%H%M%S)-$$"
paths=(
    '/Applications/Klinglab Stream Mixer.app'
    "$KSM_HOME/Library/Application Support/obs-studio/plugins/klinglab-stream-mixer-source.plugin"
    "$KSM_HOME/Library/Application Support/com.elgato.StreamDeck/Plugins/site.klinglab.streammixer.sdPlugin"
)
if "$apply"; then
    for process in 'Klinglab Stream Mixer' OBS 'Stream Deck'; do
        if /usr/bin/pgrep -x "$process" >/dev/null; then
            printf 'Close %s before uninstalling. No running process will be stopped.\n' "$process" >&2
            exit 1
        fi
    done
    for path in "${paths[@]}"; do
        if [[ -e "$path" && ! -w "$(dirname -- "$path")" ]]; then
            printf 'Cannot move %s; use Finder with an administrator account.\n' "$path" >&2
            exit 1
        fi
    done
fi
for path in "${paths[@]}"; do
    if [[ -e "$path" ]]; then
        printf '%s -> %s/%s\n' "$path" "$archive" "$(basename -- "$path")"
        if "$apply"; then
            /bin/mkdir -p "$archive"
            /bin/mv "$path" "$archive/"
        fi
    fi
done
if "$apply"; then
    printf 'Uninstalled by archiving bundles. Settings, projects, sounds and logs remain in place.\n'
else
    printf 'Preview only. Run with --apply to archive these bundles. Settings will remain in place.\n'
fi
