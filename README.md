# Solarpunk Trainer

[![Build](https://github.com/chase-irql/SolarpunkTrainer/actions/workflows/build.yml/badge.svg)](https://github.com/chase-irql/SolarpunkTrainer/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A Windows x64 trainer and local loader for the single-player game
[Solarpunk](https://store.steampowered.com/app/1805110/Solarpunk/). The trainer
uses a Dear ImGui overlay and validates a build-specific runtime schema before
installing hooks or enabling memory-writing features.

> [!WARNING]
> This project injects a DLL into a running game process and changes local game
> state. Antivirus software may flag that behavior. Review the source and build
> it yourself. Use it only with a copy of the game you own, in offline or
> single-player play, and at your own risk. The project is not affiliated with
> or endorsed by Cyberwave or the game's publishers.

## Features

- Player options such as god mode, survival-stat controls, and free crafting,
  building, and research
- Flight, noclip, third-person camera, coordinate HUD, and waypoints
- Inventory editing and optional value locks
- Resource markers and world diagnostics
- Airship battery, damage, boost, and autopilot controls
- Exact executable fingerprinting and fail-closed schema validation

Game updates can rename or change fields. When a feature contract cannot be
validated, that feature is disabled instead of falling back to a stale offset.
See [the compatibility architecture](docs/AUTO_UPDATE_ARCHITECTURE.md) for the
design and its limits.

## Requirements

- Windows 10 or 11, x64
- Visual Studio 2022 with **Desktop development with C++** and the Windows SDK
- Git with submodule support
- Internet access on the first build to fetch a pinned Dumper-7 revision
- A legitimately installed copy of Solarpunk

## Build

Clone the repository, then run the build helper from PowerShell:

```powershell
git clone https://github.com/chase-irql/SolarpunkTrainer.git
cd SolarpunkTrainer
.\build.ps1
```

The helper locates Visual Studio, initializes the pinned JSON submodule, and
builds the complete x64 solution. Release outputs are written to `x64/Release`:

```text
SolarpunkLoader.exe
SolarpunkSchemaProbe.dll
SolarpunkTrainer.dll
```

The loader build automatically fetches a pinned Dumper-7 revision, applies the
project's schema-only overlay, and builds `SolarpunkSchemaProbe.dll`. Upstream
source is cached under the ignored `.deps` directory and is not vendored in
this repository. Set `/p:BuildSchemaProbe=false` only when intentionally
building without automatic unknown-build analysis. See
[schema_probe/README.md](schema_probe/README.md) for provenance and maintenance
details.

## Use

1. Keep `SolarpunkLoader.exe` and `SolarpunkTrainer.dll` in the same folder.
2. Start Solarpunk, then run the loader. If the game is elevated, the loader
   must run at the same privilege level.
3. Wait for compatibility validation and select **Load trainer**.
4. Press `Insert` to show or hide the menu, `F2` to toggle the coordinate HUD,
   and `End` to unload the trainer.

Settings and validated schemas are stored under `%LOCALAPPDATA%`, not in the
repository.

## Contributing and security

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. Please
report security issues according to [SECURITY.md](SECURITY.md), not through a
public issue.

## License

Project code is available under the [MIT License](LICENSE). Bundled and pinned
dependencies retain their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
