#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v python3 >/dev/null 2>&1; then
    if command -v apt-get >/dev/null 2>&1; then
        if [ "$(id -u)" -eq 0 ]; then
            sudo_prefix=()
        elif command -v sudo >/dev/null 2>&1; then
            sudo_prefix=(sudo)
        else
            echo "Python 3 is missing and sudo is not available for automatic installation." >&2
            exit 1
        fi
        "${sudo_prefix[@]}" apt-get update
        "${sudo_prefix[@]}" apt-get install -y python3 ca-certificates
    else
        echo "Python 3 is required before Ludus can initialize this host." >&2
        exit 1
    fi
fi

# Apple's Python launcher can inject SDKROOT even when the shell did not set it.
# Preserve the caller's choice before entering Python so automatic SDK selection
# is not mistaken for an explicit override.
export LUDUS_INIT_SHELL_SDKROOT="${SDKROOT-}"
exec python3 "${repo_dir}/scripts/python/init_launcher.py" "$@"
