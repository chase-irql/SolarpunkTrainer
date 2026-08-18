# Schema probe build

`SolarpunkSchemaProbe.dll` discovers the current game's Unreal Engine runtime
layout, serializes reflected types and properties, and writes the exact-build
schema consumed by the loader and trainer.

## Reproducible build recipe

The loader project invokes `scripts/build-schema-probe.ps1`, which:

1. Fetches Dumper-7 commit
   `3a849bb838422bea5cf417447d00a99549d932cf` directly from its upstream GitHub
   repository into `.deps/Dumper-7`.
2. Verifies the exact remote and commit.
3. Applies `dumper-7.patch` and copies the files under `overlay/Dumper`.
4. Builds the patched project as `SolarpunkSchemaProbe.dll` into the same
   output directory as the loader and trainer.

The recipe hashes its patch and overlay. If those inputs change, remove the
generated `.deps/Dumper-7` directory and rebuild. Nothing under `.deps` is
committed.

## Licensing boundary

The patch and overlay are Solarpunk Trainer project code and are MIT-licensed.
Dumper-7 is downloaded from upstream during the build and is not vendored
here. The pinned upstream revision does not declare a license, so do not assume
that this repository's MIT License covers Dumper-7 or a binary compiled from
it. Review the upstream project's terms before redistributing the probe binary.

To build only the loader and trainer without fetching or compiling the probe,
pass `/p:BuildSchemaProbe=false` to MSBuild. The loader will then require an
existing compatible probe or cached schema at runtime.
