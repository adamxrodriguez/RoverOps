#!/bin/sh
set -eu
public_origin="${PUBLIC_ORIGIN:-${RENDER_EXTERNAL_URL:-}}"
if [ -z "$public_origin" ]; then
  echo 'Set PUBLIC_ORIGIN to the public HTTPS origin (or use Render RENDER_EXTERNAL_URL).' >&2
  exit 1
fi
exec ./roverops --config config/rover.json --host 0.0.0.0 \
  --port "${PORT:-8080}" --public-url "$public_origin" --static-root dashboard/dist
