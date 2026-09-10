#!/bin/sh
# Force regular-file writes to fail while keeping diagnostic pipes writable.
ulimit -c 0 || exit 2
trap '' XFSZ || exit 2
ulimit -f 0 || exit 2
exec "$@"
