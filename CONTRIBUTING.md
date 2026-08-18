# Contributing

Contributions that improve reliability, compatibility validation,
documentation, or maintainability are welcome.

## Before opening a change

1. Search existing issues and pull requests.
2. Keep changes focused and explain the user-visible behavior.
3. Do not submit proprietary game files, generated SDK dumps, credentials,
   personal data, or binaries you do not have permission to redistribute.
4. Do not add unvalidated raw addresses for `GWorld`, `GObjects`, `GNames`, or
   `ProcessEvent`. Reflected fields belong in a named schema contract; native
   offsets need a documented discovery routine and validator.

## Build and verify

Build the release configuration from PowerShell:

```powershell
.\build.ps1
```

Describe any manual game-build testing in the pull request. Never test
trainer changes in multiplayer or against another person's process.

By contributing, you agree that your contribution is licensed under the
repository's MIT License.
