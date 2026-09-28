# XerahS (native)

Greenfield, contract-first native XerahS for Windows, macOS, and Linux.

This repository is **not a fork** of [ShareX/XerahS](https://github.com/ShareX/XerahS). It does not inherit that tree's history, Avalonia UI, or project layout. ShareX/XerahS remains the production Avalonia application and a behavioral reference. This repository implements the same product from a versioned Product Contract, natively on each operating system.

| Layer | Lives here |
|---|---|
| Product truth | `product-contract/` |
| Platform truth | `platforms/windows`, `platforms/macos`, `platforms/linux` |
| Conformance truth | `conformance/` |

Application identity: `com.xerahs.app` / "XerahS". The native application replaces Avalonia XerahS after full-parity acceptance, with explicit settings migration and recovery safeguards.

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

Contract drafting phase. The Product Contract (`product-contract/manifest.yaml`,
version 0.2.0, lifecycle `draft`) lists 15 capabilities with human approval
records, and every parity-ledger row from the pinned `kova-0.29.0` baseline
links to a stable requirement ID. See
[product-contract/backlog/STATUS.md](product-contract/backlog/STATUS.md) for the
generated completion view. Native implementations and conformance runners have
not landed yet.

## Verification

Governance and contract checks use only the Python standard library:

```sh
python tools/contract-linter/lint.py
python -m unittest discover tools/contract-linter/tests -v
python tools/contract-backlog/tracker.py --check
python -m unittest discover tools/contract-backlog/tests -v
python tools/contract-linter/lint.py --changed-since origin/main   # effective instructions
```
