# Release builds

The `Release binaries` GitHub Actions workflow builds and tests a Linux x86-64
release bundle on Ubuntu 24.04. It uses Node.js 22 and the official Steinberg
VST3 SDK 3.8.0 tag, verified against its pinned commit.

## Snapshot artifact

Run the workflow manually from **Actions → Release binaries → Run workflow**.
The resulting `.tar.gz` and `.sha256` files are stored as workflow artifacts
for inspection without creating a GitHub Release.

## Tagged release

Create and push a semantic version tag:

```sh
git tag v0.1.0
git push origin v0.1.0
```

A matching GitHub Release is created with generated notes, the binary archive,
and its SHA-256 checksum. The workflow derives the bundle version from the tag;
manual builds use `snapshot-<commit>`.

Release builds intentionally target one known platform baseline. GTK and JACK
remain dynamic system dependencies, while the production Node modules are
included in the bundle. The VST3 SDK is not included, but its MIT license is
included because SDK-derived code is linked into the native binaries.
