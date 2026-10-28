#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
plugin="$root/site.klinglab.streammixer.sdPlugin"
if [[ ! -x "$root/node_modules/.bin/esbuild" || ! -x "$root/node_modules/.bin/streamdeck" ]]; then
    npm ci --prefix "$root" --no-audit --no-fund
fi
node "$root/scripts/build.mjs"
# Each invocation preserves earlier packages instead of overwriting them.
output="$root/dist/$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir -p "$output"
"$root/node_modules/.bin/streamdeck" pack "$plugin" --output "$output" --no-update-check --no-file-list
printf 'Package directory: %s\n' "$output"
