# Solarpunk Trainer Local Compatibility System

## Goal

Keep the trainer compatible with an installed Solarpunk build without a
network service, downloaded offset pack, or remote code. The loader analyzes
the game already on this machine, and the DLL consumes only the schema
generated for that exact executable.

This updates runtime knowledge about Unreal and Solarpunk. It does not
download or replace the loader or trainer binary.

## Runtime flow

```text
Solarpunk starts
  |
  v
loader fingerprints the installed PE
  |  timestamp + SizeOfImage + SHA-256(.text)
  v
exact schema cached?
  | yes                         | no
  v                             v
validate cache           inject local schema probe
                                |
                                v
                         schema probe discovers UE runtime
                                |
                                v
                         write schema atomically
                                |
                                v
                         validate generated schema
  \_____________________________/
                |
                v
        enable "Load trainer"
                |
                v
 trainer repeats exact fingerprint check
                |
                v
 resolve layouts and feature contracts
                |
                v
 install hooks only after validation succeeds
```

There are no HTTP requests in this flow.

## Build artifacts

`Release|x64` produces two files in `x64/Release`:

- `SolarpunkLoader.exe`
- `SolarpunkTrainer.dll`

The public repository does not include or build the optional schema probe. A
compatible `SolarpunkSchemaProbe.dll` may be supplied beside the loader to
analyze an unknown game build. If neither that probe nor an exact validated
cache exists, the loader stays disabled rather than using stale data.

## Exact build identity

The loader and trainer independently calculate:

```text
<PE timestamp>-<SizeOfImage>-<SHA-256 of on-disk .text>
```

The fingerprint selects:

```text
%LOCALAPPDATA%\Solarpunk Trainer\SchemaCache\
  <fingerprint>\runtime-schema.json
```

Hashing the code section prevents a reused marketing version, timestamp, or
Steam label from selecting the wrong schema.

## Local analyzer

A compatible `SolarpunkSchemaProbe.dll` is a short-lived local analyzer. The
original development version used a modified Dumper-7 build to:

1. Locates `GUObjectArray`.
2. Locates and initializes the UE5 name pool.
3. Discovers UObject, UField, UStruct, FField, FProperty, and UFunction
   layouts.
4. Locates `ProcessEvent` and `GWorld`.
5. Enumerates the live reflection graph.
6. Writes a temporary JSON file and atomically activates it.
7. Unloads itself.

The Solarpunk-specific hard-coded `GNames` RVA was removed from Dumper-7. A
generic UE5.7 `GetNamePool` accessor signature is now a fallback discovery
path, and the probe derives the live name-entry stride and block geometry.

The loader does not equate successful probe injection with successful
analysis. It waits for the exact cache file and validates its contents.

## Validation

Before enabling the trainer button, the loader requires:

- schema format version `1`;
- an exact build-fingerprint match;
- a complete reflected type array and matching `type_count`;
- nonzero `GObjects`, `GNames`, `GWorld`, and `ProcessEvent`;
- a valid `ProcessEvent` vtable index;
- usable object-array and name-pool geometry;
- usable UObject, UField, UStruct, and FProperty layouts.

Malformed or incomplete cache files are removed and regenerated locally.

The trainer independently fingerprints the game and validates that core RVAs
fall within the current image. It installs no render or input hook if this
fails, so direct DLL injection cannot bypass the compatibility gate.

## Runtime reflection and class updates

Generated SDK C++ is no longer the runtime source of truth for changing core
addresses or reflected member offsets. `GameSchema` indexes every dumped:

- class, struct, and function path;
- super type, type size, and alignment;
- property name, kind, C++ type, offset, size, array dimension, and flags;
- bool byte offset and mask;
- referenced object or struct type.

Properties resolve by owner and exact property name, then through the dumped
super chain. Blueprint struct members with generated GUID suffixes can resolve
by a unique stable prefix; ambiguous prefixes fail.

The migrated contracts cover:

- world, local player, controller, pawn, coordinates, camera cache, and POV;
- GObjects enumeration, FName decoding, class traversal, and ProcessEvent;
- inventory system, inventory array, and declared size;
- player stats and damage flags;
- third-person components and held-item references;
- free building, crafting, and research layouts;
- day/night and game speed;
- airship movement, battery, damage, boost, autopilot, and controller fields;
- resource enumeration core layouts.

Missing or mismatched contracts disable their feature path instead of using an
old offset.

## What cannot be universally automatic

No dumper can prove semantic equivalence after arbitrary game-code changes.
Examples include:

- a function is renamed and its behavior changes;
- a reflected property becomes a computed native value;
- a private non-UPROPERTY member moves without a stable code reference;
- a function keeps its name but changes the meaning of its parameters.

Reflection updates layout facts, not intent. The safe response to ambiguity is
to disable the feature and report its failed contract in Diagnostics.
Guessing the nearest field is not an auto-update strategy.

Non-reflected native data requires one of:

1. an engine-code signature with an independent runtime validator;
2. a deterministic invariant-based discovery routine;
3. a locally audited build-specific native contract.

The schema foundation supports these contract types without network access.

## Cache lifecycle

- A known exact build uses its validated cache immediately.
- A new build automatically runs the local probe once.
- Multiple builds may coexist in the cache.
- A corrupt schema is regenerated.
- Analysis runs off the ImGui render thread.
- Closing the game cancels active analysis.

## Development rule

New code must not add another raw `GWorld`, `GObjects`, `GNames`, or
`ProcessEvent` constant. Reflected fields belong in a named feature contract
resolved through `GameSchema`. Native offsets need a documented local
discovery routine and validator before they become write-capable.
