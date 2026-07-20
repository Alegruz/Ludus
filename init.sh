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

exec python3 "${repo_dir}/scripts/python/engine.py" init "$@"
