#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
probe="$repo_dir/native/build-jack/transmission_jack_engine_probe"
source="$repo_dir/native/build-jack/transmission_jack_midi_source"
if [[ ! -x "$probe" || ! -x "$source" ]]; then
  echo "Build the JACK tools first: cmake --build native/build-jack" >&2
  exit 2
fi

log_file="$(mktemp)"
probe_pid=""
cleanup() {
  if [[ -n "$probe_pid" ]] && kill -0 "$probe_pid" 2>/dev/null; then
    kill "$probe_pid" 2>/dev/null || true
    wait "$probe_pid" 2>/dev/null || true
  fi
  rm -f "$log_file"
}
trap cleanup EXIT

"$probe" >"$log_file" 2>&1 &
probe_pid=$!
target=""
for _ in {1..50}; do
  target="$(jack_lsp 2>/dev/null | awk '/:midi_in_1$/ { print $1; exit }')"
  [[ -n "$target" ]] && break
  sleep 0.05
done
if [[ -z "$target" ]]; then
  echo "Transmission MIDI port did not appear. Is JACK running?" >&2
  cat "$log_file" >&2 || true
  exit 1
fi

echo "Connecting generated MIDI to $target"
"$source" "$target"
wait "$probe_pid"
probe_pid=""
cat "$log_file"
events="$(awk -F= '/^midiEvents=/ { print $2 }' "$log_file")"
if [[ -z "$events" || "$events" -eq 0 ]]; then
  echo "No MIDI events were observed" >&2
  exit 1
fi
echo "MIDI test passed: $events event(s) observed"
