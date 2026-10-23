#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$here/backend_ai_bridge.py" --queue-dir "$here/../ai" "$@"
