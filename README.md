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
- A legitimately installed copy of Solarpunk

## Build

Clone the repository and its pinned JSON dependency:

```powershell
git clone --recurse-submodules https://github.com/chase-irql/SolarpunkTrainer.git
cd SolarpunkTrainer
msbuild SolarpunkTrainer.sln /m /p:Configuration=Release /p:Platform=x64
```

If you cloned without submodules, run `git submodule update --init --recursive`
before building. Release outputs are written to `x64/Release`:

```text
SolarpunkLoader.exe
SolarpunkTrainer.dll
```

The public repository does not include a runtime schema generator. To analyze
an unknown game build automatically, place a compatible
`SolarpunkSchemaProbe.dll` beside the two build outputs. Without a probe or an
already validated local schema, the loader remains disabled. The expected
schema and validation rules are documented in
[docs/AUTO_UPDATE_ARCHITECTURE.md](docs/AUTO_UPDATE_ARCHITECTURE.md).

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
