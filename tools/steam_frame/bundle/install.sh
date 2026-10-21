#!/bin/sh
set -eu
bundle=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$bundle/install.py" "$@"
