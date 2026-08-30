# XerahS (native)

Greenfield, contract-first native XerahS for Windows, macOS, and Linux.

This repository is **not a fork** of [ShareX/XerahS](https://github.com/ShareX/XerahS). It does not inherit that tree's history, Avalonia UI, or project layout. ShareX/XerahS remains the production Avalonia application and a behavioral reference. This repository implements the same product from a versioned Product Contract, natively on each operating system.

| Layer | Lives here |
|---|---|
| Product truth | `product-contract/` |
| Platform truth | `platforms/windows`, `platforms/macos`, `platforms/linux` |
| Conformance truth | `conformance/` |

Pilot application identity: `com.xerahs.native` / "XerahS Native". It installs side by side with Avalonia XerahS. This repository does not retire Avalonia.

## Architecture

Start at [BXIP001](docs/proposals/BXIP001-contract-first-agent-native-platform-architecture/README.md). ShareX Team tracks the same proposal as XIP0086.

Pilot-binding defaults (see BXIP001 decisions):

- Windows: WinUI 3 + Win32/WinRT, C#, Windows 11 23H2+
- macOS: SwiftUI + AppKit, Swift, macOS 14+
- Linux: Qt 6, C++17, Wayland first
- No shared product runtime binary in the pilot
- No in-process .NET plugins

## Layout

```text
product-contract/   Product Contract (what XerahS does)
platforms/          Native applications (how each OS does it)
conformance/        Shared scenarios, vectors, and evidence
tools/              Contract linter and governance tooling
docs/proposals/     BXIP001 and later proposals
```

## License

GNU GPL v3. See [LICENSE](LICENSE). Copyright ShareX Team.

## Status

Phase 0 seed: proposal, constitution, and directory layout. Pilot contracts and native implementations have not landed yet.
