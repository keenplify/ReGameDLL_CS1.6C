#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../../.." && pwd)
if [ ! -x "$root/hlds_linux" ]; then
 echo "Install the Linux HLDS dedicated-server executable in $root first." >&2
 exit 1
fi
cd "$root"
export LD_LIBRARY_PATH="$root${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_PRELOAD="$here/../libregamedll_query.so${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$root/hlds_linux" -game cstrike -console "$@"
