#!/usr/bin/env bash
# deploy/nginx/check.sh
#
# Validate deploy/nginx/vocab.conf here, in a throwaway container, rather than
# on the server. `nginx -t` on a live host is a bad place to learn that a
# fragment does not parse.
#
# It also checks the things nginx -t cannot see, because a syntax checker cannot
# read intent: a term must 303 rather than 200, the bare form without the
# trailing slash must be handled, and every add_header must say `always` or it
# is absent on error responses.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fragment="$here/vocab.conf"
image="${NGINX_IMAGE:-nginx:alpine}"

runtime=""
for candidate in podman docker; do
  if command -v "$candidate" >/dev/null 2>&1; then runtime="$candidate"; break; fi
done

if [ -n "$runtime" ]; then
  work="$(mktemp -d)"
  trap 'rm -rf "$work"' EXIT

  # The fragment is a set of location blocks, so it is wrapped in the smallest
  # server that makes it valid. TLS is left out: certbot owns those lines on the
  # server and they are not what this is checking.
  cat > "$work/nginx.conf" <<'CONF'
events { worker_connections 64; }
http {
  server {
    listen 8081;
    server_name hyperdata.it;
    include /etc/nginx/conf.d/vocab.conf;
  }
}
CONF
  cp "$fragment" "$work/vocab.conf"

  echo "check: validating with $runtime ($image)"
  output="$($runtime run --rm \
    -v "$work/nginx.conf:/etc/nginx/nginx.conf:ro,Z" \
    -v "$work/vocab.conf:/etc/nginx/conf.d/vocab.conf:ro,Z" \
    "$image" nginx -t 2>&1)" || {
      echo "$output" >&2
      echo "check: FAILED" >&2
      exit 1
    }
  echo "$output" | sed 's/^/  /'
  echo "check: syntax ok"
else
  echo "check: no podman or docker, so nginx syntax was NOT checked" >&2
  echo "check: the intent checks below still ran" >&2
fi

fail=0

# A term denotes a class or a property, not a page. A 200 would assert they are
# the same thing.
if ! grep -q 'return 303' "$fragment"; then
  echo "check: no 303 from a term to its document; a 200 would assert the term is the page" >&2
  fail=1
fi

# What a person types, as against what published data contains.
if ! grep -q 'location = /xmlns/transmissions {' "$fragment"; then
  echo "check: /xmlns/transmissions without its trailing slash is not handled" >&2
  fail=1
fi

if ! grep -q 'add_header Access-Control-Allow-Origin' "$fragment"; then
  echo "check: no CORS header; a browser cannot read the vocabulary cross-origin" >&2
  fail=1
fi

# Comments are stripped first: this file explains the `always` rule in prose,
# and matching its own explanation is how a check like this first cries wolf.
if grep -v '^[[:space:]]*#' "$fragment" | grep 'add_header' | grep -v 'always' | grep -q .; then
  echo "check: an add_header is missing 'always', so it is not sent on error responses" >&2
  fail=1
fi

# A types block would replace the whole mime map for the location rather than
# adding to it.
if grep -qE '^\s*types\s*\{' "$fragment"; then
  echo "check: a types block replaces the mime map for this location" >&2
  fail=1
fi

# The paths nginx will read from on the server. A fragment that points at a
# directory nobody deploys serves 404s that look like a config problem.
for path in /home/github/transmission/deploy/vocab; do
  if ! grep -q "$path" "$fragment"; then
    echo "check: $path is not referenced; the server checkout is where the files are" >&2
    fail=1
  fi
done

[ "$fail" -eq 0 ] || { echo "check: FAILED"; exit 1; }
echo "check: intent checks ok"
