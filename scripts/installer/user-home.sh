#!/bin/bash
# Resolve the recipient independently of the privileged installer's HOME.
ksm_resolve_user() {
    local candidate="${INSTALLER_USER:-}"
    if [[ -z "$candidate" && "$(/usr/bin/id -u)" != 0 ]]; then
        candidate="$(/usr/bin/id -un)"
    fi
    if [[ -z "$candidate" || "$candidate" == root ]]; then
        candidate="$(/usr/bin/stat -f '%Su' /dev/console)"
    fi
    case "$candidate" in ''|root|loginwindow|_mbsetupuser) return 1 ;; esac
    KSM_USER="$candidate"
    KSM_UID="$(/usr/bin/id -u "$KSM_USER")" || return 1
    KSM_HOME="$(/usr/bin/dscl . -read "/Users/$KSM_USER" NFSHomeDirectory)" || return 1
    KSM_HOME="${KSM_HOME#NFSHomeDirectory: }"
    [[ "$KSM_UID" != 0 && "$KSM_HOME" == /* && "$KSM_HOME" != / && -d "$KSM_HOME" ]]
}
