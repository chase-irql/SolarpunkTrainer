# Solarpunk Loader

The loader is a compact x64 ImGui application that detects
`SolarpunkSteam-Win64-Shipping.exe`, analyzes unknown builds entirely on the
local machine, and loads the trainer only after an exact compatibility schema
passes validation.

## Build

Build `SolarpunkLoader` in `Release|x64`, or build the complete solution. The
release output contains:

```text
x64/Release/SolarpunkLoader.exe
x64/Release/SolarpunkTrainer.dll
```

The public repository does not include or build a schema probe. A compatible
`SolarpunkSchemaProbe.dll` can be placed beside these files to analyze unknown
game builds. Without a probe or an already validated cached schema, the loader
remains disabled.

## Runtime behavior

- Process detection is polled at a low rate.
- The installed executable is identified by PE metadata and SHA-256 of
  `.text`.
- Unknown builds automatically run the short-lived local schema probe.
- Generated schemas are cached per exact build under `%LOCALAPPDATA%`.
- No network request or offset download is performed.
- The trainer button remains disabled until the schema validates.
- Analysis and injection run on workers so ImGui remains responsive.
- The loader verifies both DLLs are x64 PE images.
- Remote `LoadLibraryW` is calculated from the target's `kernel32.dll` base.
- Duplicate trainer injection is prevented.
- Errors remain visible and retryable.
- The trainer independently validates the exact schema before installing
  hooks.

If the game is elevated, the loader must run at a matching privilege level.

The full local-only compatibility design and its safety boundaries are
documented in `../docs/AUTO_UPDATE_ARCHITECTURE.md`.
