# Transmission binary bundle

This archive contains an Ubuntu 24.04 x86-64 build of Transmission. It includes
the GTK/JACK/VST3 desktop host, command-line VST3 tools, the native Node addon,
the JavaScript control plane, production Node dependencies, profiles, and
example patches.

## Runtime requirements

- Ubuntu 24.04 or a compatible Linux distribution
- x86-64 CPU
- Node.js 20 or newer
- GTK 3 runtime libraries
- JACK2 or PipeWire with JACK compatibility
- `ffmpeg` for MP3 export

On Ubuntu, install the system runtime libraries with:

```sh
sudo apt update
sudo apt install libgtk-3-0t64 libjack-jackd2-0 ffmpeg
```

Install Node.js 20 or newer using a supported package for your distribution.

## Run the desktop host

Extract the archive and run:

```sh
./transmission
```

Transmission uses GTK's X11 backend because embedded VST3 editors currently
require X11. Install plugins in `~/.vst3`.

## Run the MCP control plane

Project-only mode:

```sh
npm run mcp:start -- --project-root "$PWD"
```

With the packaged native audio engine:

```sh
npm run mcp:start -- \
  --project-root "$PWD" \
  --native-addon lib/transmission_native.node \
  --jack
```

The binary is dynamically linked to the Ubuntu 24.04 GTK, JACK, C/C++, and
system runtime libraries. It does not bundle third-party VST3 plugins.
